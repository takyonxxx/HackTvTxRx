#include "StudioWindow.h"
#include "ModePanel.h"
#include "TvRxPanel.h"
#include "RadioRxPanel.h"
#include "RadioTxPanel.h"
#include "TvTxPanel.h"
#include "widgets/glplotter.h"
#include "widgets/freqctrl.h"
#include "widgets/meter.h"

#include <QApplication>
#include <cmath>
#include <QCloseEvent>
#include <QDateTime>
#include <QDebug>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSettings>
#include <QScreen>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

namespace {

/* Tabs whose real implementation arrives in a later phase. */
class PlaceholderPanel : public ModePanel
{
public:
    PlaceholderPanel(const QString& title, const QString& text, bool tx, QWidget* parent = nullptr)
        : ModePanel(parent), m_title(title), m_tx(tx)
    {
        QVBoxLayout* l = new QVBoxLayout(this);
        QLabel* lbl = new QLabel(text, this);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("QLabel { color: #708090; font-size: 14pt; }");
        l->addStretch();
        l->addWidget(lbl);
        l->addStretch();
    }
    QString title() const override { return m_title; }
    bool isTransmit() const override { return m_tx; }

private:
    QString m_title;
    bool m_tx;
};

} // namespace

StudioWindow::StudioWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_device(new DeviceController(this))
    , m_engine(new RxEngine(this))
    , m_txEngine(new TxEngine(this))
{
#ifdef QT_NO_DEBUG
    setWindowTitle(QString("HackTvStudio - Qt %1").arg(qVersion()));
#else
    setWindowTitle(QString("HackTvStudio - Qt %1  [DEBUG BUILD - slow DSP]").arg(qVersion()));
#endif

    buildUi();

    // Producer (USB thread) -> consumer (engine thread) wake-up
    m_device->setRxDataNotify([this] { m_engine->notify(); });

    connect(m_engine, &RxEngine::spectrumReady, this, &StudioWindow::onSpectrum, Qt::QueuedConnection);
    m_device->setTxDataNotify([this] { m_txEngine->notify(); });
    connect(m_txEngine, &TxEngine::spectrumReady, this, &StudioWindow::onSpectrum, Qt::QueuedConnection);
    connect(m_txEngine, &TxEngine::sourceEnded, this, [this] {
        m_panelStatusLabel->setText("TX source ended (end of file)");
    }, Qt::QueuedConnection);
    connect(m_engine, &RxEngine::recordingFinished, this, &StudioWindow::onRecordingFinished, Qt::QueuedConnection);
    connect(m_device, &DeviceController::stateChanged, this, &StudioWindow::onDeviceState);
    connect(m_device, &DeviceController::errorOccurred, this, &StudioWindow::onDeviceError);
    connect(m_device, &DeviceController::logMessage, this, [](const QString& m) { qDebug() << "[HackRF]" << m; });

    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &StudioWindow::updateStatusBar);
    m_statusTimer->start(500);

    loadSettings();
    setFrequency(m_frequency, false);
}

StudioWindow::~StudioWindow()
{
    stopDevice();
}

void StudioWindow::closeEvent(QCloseEvent* e)
{
    saveSettings();
    stopDevice();
    QMainWindow::closeEvent(e);
}

// ============================================================================
// UI
// ============================================================================

void StudioWindow::buildUi()
{
    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout* root = new QVBoxLayout(central);
    root->setSpacing(6);
    root->setContentsMargins(8, 8, 8, 4);
#ifdef Q_OS_ANDROID
    // Head-unit / phone screens are short: tighten everything vertically so the
    // top strip + spectrum + mode panel all fit without clipping.
    root->setSpacing(3);
    root->setContentsMargins(4, 2, 4, 2);
#endif

    root->addWidget(buildTopStrip());

    // ---- Spectrum + waterfall with a vertical range slider
    QWidget* specWidget = new QWidget(this);
    m_specWidget = specWidget;
    QHBoxLayout* specLayout = new QHBoxLayout(specWidget);
    specLayout->setContentsMargins(0, 0, 0, 0);
    specLayout->setSpacing(4);

    m_plotter = new CPlotter(this);
    m_plotter->setSampleRate(static_cast<float>(m_sampleRate));
    m_plotter->setSpanFreq(m_sampleRate);
    m_plotter->setCenterFreq(m_frequency);
    m_plotter->setFftRange(-110.0f, 0.0f);
    m_plotter->setPandapterRange(-110.0f, 0.0f);
    m_plotter->setFreqUnits(m_sampleRate >= 8000000 ? 1000000 : 1000);   // MHz axis for wide (TV) spans
    m_plotter->setPercent2DScreen(62);         // 2D spectrum gets the larger share
    m_plotter->setFilterBoxEnabled(false);     // radio RX enables it (phase 2)
    m_plotter->setCenterLineEnabled(true);
    m_plotter->setClickResolution(1000);
    m_plotter->setFftPlotColor(QColor("#CEECF5"));
    m_plotter->setFftFill(true);
#ifdef Q_OS_ANDROID
    m_plotter->setMinimumHeight(140);
#else
    m_plotter->setMinimumHeight(180);
#endif
    m_plotter->setMinimumWidth(320);
    m_plotter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(m_plotter, &CPlotter::newDemodFreq, this, &StudioWindow::onPlotterDemodFreq);
    connect(m_plotter, &CPlotter::wheelFreqChange, this, &StudioWindow::onPlotterWheel);
    connect(m_plotter, &CPlotter::newFilterFreq, this, [this](int low, int high) {
        if (m_active) m_active->onFilterRangeChanged(low, high);
    });
    specLayout->addWidget(m_plotter, 1);

    m_plotterRange = new QSlider(Qt::Vertical, this);
    m_plotterRange->setRange(-130, -30);
    m_plotterRange->setValue(-110);
    m_plotterRange->setFixedWidth(22);
    m_plotterRange->setToolTip("Spectrum floor (dBFS)");
    connect(m_plotterRange, &QSlider::valueChanged, this, [this](int v) {
        m_plotter->setPandapterRange(static_cast<float>(v), 0.0f);
        m_plotter->setFftRange(static_cast<float>(v), 0.0f);
    });
    specLayout->addWidget(m_plotterRange);

    // ---- Mode tabs
    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    addPanel(new RadioRxPanel(this));
    m_radioRxTab = 0;
    addPanel(new RadioTxPanel(this));
    addPanel(new TvRxPanel(this));
    m_tvRxTab = 2;
    addPanel(new TvTxPanel(this));
    connect(m_tabs, &QTabWidget::currentChanged, this, &StudioWindow::onTabChanged);

    // Spectrum column / strip and the mode tabs share one splitter whose orientation
    // decides the layout: vertical = spectrum on top, horizontal = spectrum on the right.
    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->addWidget(specWidget);
    m_splitter->addWidget(m_tabs);
    m_splitter->setChildrenCollapsible(false);
    root->addWidget(m_splitter, 1);
    placeSpectrumOnTop();

    // ---- Status bar
    m_stateLabel = new QLabel("IDLE", this);
    m_stateLabel->setStyleSheet("QLabel { font-weight: bold; color: #708090; padding: 0 8px; }");
    m_overflowLabel = new QLabel("", this);
    m_overflowLabel->setStyleSheet("QLabel { color: #ff8800; padding: 0 8px; }");
    m_panelStatusLabel = new QLabel("", this);
    statusBar()->addWidget(m_stateLabel);
    statusBar()->addWidget(m_panelStatusLabel, 1);
    statusBar()->addPermanentWidget(m_overflowLabel);

    m_active = m_panels[m_tabs->currentIndex()];
    m_active->onActivated();
    dockSpectrum(m_active);

    // First-run size: fit the available screen (saved geometry overrides this later)
    const QRect avail = screen() ? screen()->availableGeometry() : QRect(0, 0, 1400, 900);
    resize(std::min(1400, avail.width() - 40), std::min(940, avail.height() - 60));
}

QWidget* StudioWindow::buildTopStrip()
{
    QWidget* strip = new QWidget(this);
    QHBoxLayout* l = new QHBoxLayout(strip);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(10);

    // Frequency dial + band / mode readout
    QGroupBox* tuneGroup = new QGroupBox("Frequency", strip);
    QVBoxLayout* tl = new QVBoxLayout(tuneGroup);
    tl->setSpacing(4);

    m_freqCtrl = new CFreqCtrl(tuneGroup);
    m_freqCtrl->setup(0, 0, static_cast<qint64>(BandPlan::HACKRF_MAX_HZ), 1, FCTL_UNIT_MHZ);
    m_freqCtrl->setFrequency(static_cast<qint64>(m_frequency));
#ifdef Q_OS_ANDROID
    m_freqCtrl->setMinimumHeight(46);
    m_freqCtrl->setMaximumHeight(56);
#else
    m_freqCtrl->setMinimumHeight(64);
    m_freqCtrl->setMaximumHeight(80);
#endif
    m_freqCtrl->setMinimumWidth(420);
    connect(m_freqCtrl, &CFreqCtrl::newFrequency, this, &StudioWindow::onFrequencyEdited);
    tl->addWidget(m_freqCtrl);

    QHBoxLayout* bandRow = new QHBoxLayout();
    m_bandLabel = new QLabel(tuneGroup);
    m_bandLabel->setStyleSheet("QLabel { color: #00ffcc; font-weight: bold; font-size: 11pt; }");
    m_modeLabel = new QLabel(tuneGroup);
    m_modeLabel->setStyleSheet("QLabel { color: #c8f0ff; font-size: 10pt; }");
    m_autoModeCheck = new QCheckBox("Auto mode", tuneGroup);
    m_autoModeCheck->setToolTip("Switch to the TV or Radio receiver automatically from the band plan");
    m_autoModeCheck->setChecked(true);
    bandRow->addWidget(m_bandLabel);
    bandRow->addWidget(m_modeLabel, 1);
    bandRow->addWidget(m_autoModeCheck);
    tl->addLayout(bandRow);
    l->addWidget(tuneGroup, 2);

    // S-meter: power inside the tuned channel (filter box / TV channel)
    QGroupBox* meterGroup = new QGroupBox("Channel power", strip);
    QVBoxLayout* ml = new QVBoxLayout(meterGroup);
    ml->setSpacing(2);
    m_meter = new CMeter(meterGroup);
#ifdef Q_OS_ANDROID
    m_meter->setMinimumHeight(40);
    m_meter->setMaximumHeight(56);
#else
    m_meter->setMinimumHeight(58);
    m_meter->setMaximumHeight(80);
#endif
    m_meter->setMinimumWidth(220);
    ml->addWidget(m_meter);
    QHBoxLayout* mi = new QHBoxLayout();
    m_meterInfo = new QLabel("", meterGroup);
    m_meterInfo->setStyleSheet("QLabel { color: #708090; font-size: 8pt; }");
    m_overloadLabel = new QLabel("", meterGroup);
    m_overloadLabel->setStyleSheet("QLabel { color: #ff4040; font-weight: bold; font-size: 9pt; }");
    m_overloadLabel->setToolTip("ADC samples hitting full scale: lower LNA/VGA or switch the RF amp off");
    mi->addWidget(m_meterInfo, 1);
    mi->addWidget(m_overloadLabel);
    ml->addLayout(mi);
    l->addWidget(meterGroup, 1);

    l->addWidget(buildDeviceGroup(), 2);
    return strip;
}

QWidget* StudioWindow::buildDeviceGroup()
{
    QGroupBox* g = new QGroupBox("HackRF One", this);
    QGridLayout* grid = new QGridLayout(g);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(3);
#ifdef Q_OS_ANDROID
    grid->setVerticalSpacing(1);
    grid->setContentsMargins(6, 2, 6, 2);
#endif

    m_startButton = new QPushButton("START", g);
    m_startButton->setMinimumHeight(40);
#ifdef Q_OS_ANDROID
    m_startButton->setMinimumHeight(30);
#endif
    m_startButton->setStyleSheet("QPushButton { background: #1f7a3f; color: white; font-size: 12pt; }");
    connect(m_startButton, &QPushButton::clicked, this, &StudioWindow::onStartStop);
    grid->addWidget(m_startButton, 0, 0, 2, 2);          // left block: START spans label + slider columns

    m_sampleRateCombo = new QComboBox(g);
    const int rates[] = { 2000000, 4000000, 6000000, 8000000, 10000000, 12000000,
                          12500000, 14000000, 16000000, 18000000, 20000000 };
    for (int r : rates)
        m_sampleRateCombo->addItem(QString("%1 MS/s").arg(r / 1e6, 0, 'g', 4), r);
    m_sampleRateCombo->setCurrentIndex(m_sampleRateCombo->findData(16000000));
    connect(m_sampleRateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StudioWindow::onSampleRateSelected);
    grid->addWidget(new QLabel("Rate:", g), 0, 3, Qt::AlignRight);
    grid->addWidget(m_sampleRateCombo, 0, 4, 1, 2);    // right block: rate above REC IQ

    m_ampCheck = new QCheckBox("RF Amp", g);
    m_ampCheck->setChecked(true);
    connect(m_ampCheck, &QCheckBox::toggled, this, [this](bool on) { m_device->setAmpEnable(on); });
    grid->addWidget(m_ampCheck, 0, 2, 2, 1, Qt::AlignCenter);   // next to START

    m_recButton = new QPushButton("REC IQ 0.5 s", g);
    m_recButton->setToolTip("Record 0.5 s of raw int8 IQ to iq_<freq>_<time>.raw");
    connect(m_recButton, &QPushButton::clicked, this, &StudioWindow::onRecordIq);
    grid->addWidget(m_recButton, 1, 4, 1, 2);
    m_recButton->setMinimumHeight(m_sampleRateCombo->sizeHint().height());
#ifdef Q_OS_ANDROID
    m_recButton->hide();   // IQ-to-file capture is a desktop/debug feature
#endif

    auto gainRow = [&](int row, int col, const QString& name, QSlider*& slider, QSpinBox*& spin,
                       int min, int max, int step, int value) {
        grid->addWidget(new QLabel(name, g), row, col, Qt::AlignRight);
        slider = new QSlider(Qt::Horizontal, g);
        slider->setRange(min, max);
        slider->setSingleStep(step);
        slider->setPageStep(step);
        slider->setValue(value);
        spin = new QSpinBox(g);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setValue(value);
        spin->setMaximumWidth(64);
        QSlider* s = slider; QSpinBox* sp = spin;
        connect(slider, &QSlider::valueChanged, g, [sp](int v) {
            sp->blockSignals(true); sp->setValue(v); sp->blockSignals(false);
        });
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), g, [s](int v) { s->setValue(v); });
        slider->setMinimumWidth(70);
        grid->addWidget(slider, row, col + 1);
        grid->addWidget(spin, row, col + 2);
    };

    // Two gains per row keeps the top strip short enough for a 1080p screen
    gainRow(2, 0, "LNA",    m_lnaSlider,   m_lnaSpin,   0, 40, 8, 32);
    gainRow(2, 3, "VGA",    m_vgaSlider,   m_vgaSpin,   0, 62, 2, 30);
    gainRow(3, 0, "AMP",    m_rxAmpSlider, m_rxAmpSpin, 0, 14, 14, 14);
    gainRow(3, 3, "TX VGA", m_txVgaSlider, m_txVgaSpin, 0, 47, 1, 20);
    m_txVgaSlider->setToolTip("Transmit gain (0-47 dB). Start low; add the RF amp only if needed.");
    connect(m_lnaSlider, &QSlider::valueChanged, this, [this](int v) { m_device->setLnaGain(v); });
    connect(m_vgaSlider, &QSlider::valueChanged, this, [this](int v) { m_device->setVgaGain(v); });
    connect(m_rxAmpSlider, &QSlider::valueChanged, this, [this](int v) { m_device->setRxAmpGain(v); });
    connect(m_txVgaSlider, &QSlider::valueChanged, this, [this](int v) { m_device->setTxVgaGain(v); });

    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(4, 1);
    return g;
}

void StudioWindow::addPanel(ModePanel* panel)
{
    m_panels.push_back(panel);

    // A vertical-only scroll area: on a small screen the panel scrolls instead
    // of pushing the window's minimum height past the screen.
    QScrollArea* scroll = new QScrollArea(m_tabs);
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    m_tabs->addTab(scroll, panel->title());
    connect(panel, &ModePanel::statusText, this, [this, panel](const QString& t) {
        if (panel == m_active) m_panelStatusLabel->setText(t);
    });
    connect(panel, &ModePanel::filterRangeRequested, this, [this, panel](int low, int high) {
        if (panel != m_active) return;
        m_plotter->setDemodRanges(low * 2, -1000, 1000, high * 2, true);
        m_plotter->setHiLowCutFrequencies(low, high);
    });
}

void StudioWindow::dockSpectrum(ModePanel* panel)
{
    QLayout* slot = panel ? panel->spectrumSlot() : nullptr;
    if (slot) {
        // Into the panel (the splitter notices the child leaving)
        if (m_splitter->indexOf(m_specWidget) >= 0 && isVisible())
            m_splitterStateV = m_splitter->saveState();
        slot->addWidget(m_specWidget);
        m_specWidget->show();
        m_specDocked = true;
        m_plotter->setPercent2DScreen(50);
    } else if (m_specDocked || m_splitter->indexOf(m_specWidget) < 0) {
        // Back to the strip above the tabs
        m_specDocked = false;
        m_splitter->insertWidget(0, m_specWidget);
        m_specWidget->show();
        placeSpectrumOnTop(m_splitterStateV);
    }
}

void StudioWindow::placeSpectrumOnTop(const QByteArray& state)
{
    if (!m_splitter || m_specDocked) return;
    m_splitter->insertWidget(0, m_specWidget);
    m_splitter->insertWidget(1, m_tabs);
    m_plotter->setPercent2DScreen(62);
    if (!state.isEmpty()) {
        m_splitter->restoreState(state);
    } else {
#ifdef Q_OS_ANDROID
        // Spectrum/waterfall gets ~45%% so the radio spectrum is usable; the
        // smaller Android font keeps the mode panel readable in the rest.
        m_splitter->setStretchFactor(0, 1);
        m_splitter->setStretchFactor(1, 1);
        m_splitter->setSizes({ 320, 380 });
#else
        m_splitter->setStretchFactor(0, 1);
        m_splitter->setStretchFactor(1, 2);
        m_splitter->setSizes({ 280, 620 });
#endif
    }
}

// ============================================================================
// Frequency
// ============================================================================

void StudioWindow::onFrequencyEdited(qint64 hz)
{
    setFrequency(static_cast<uint64_t>(std::max<qint64>(0, hz)), true);
}

void StudioWindow::onPlotterDemodFreq(qint64 freq, qint64)
{
    setFrequency(static_cast<uint64_t>(std::max<qint64>(0, freq)), false);
}

void StudioWindow::onPlotterWheel(int direction)
{
    const qint64 step = m_freqCtrl->getActiveStep();
    qint64 f = static_cast<qint64>(m_frequency) + direction * step;
    f = std::max<qint64>(0, std::min<qint64>(f, static_cast<qint64>(BandPlan::HACKRF_MAX_HZ)));
    setFrequency(static_cast<uint64_t>(f), false);
}

void StudioWindow::setFrequency(uint64_t hz, bool fromDial)
{
    m_frequency = hz;
    if (!fromDial) {
        m_freqCtrl->blockSignals(true);
        m_freqCtrl->setFrequency(static_cast<qint64>(hz));
        m_freqCtrl->blockSignals(false);
    }
    m_plotter->setCenterFreq(hz);
    m_plotter->setDemodCenterFreq(hz);
    m_device->setFrequency(hz);
    updateBandLabel();
    for (ModePanel* p : m_panels)
        p->onFrequencyChanged(hz);
}

void StudioWindow::updateBandLabel()
{
    const BandInfo band = BandPlan::lookup(m_frequency);
    QString name = band.name;
    if (band.mode == Modulation::TV) {
        const TvChannel ch = BandPlan::tvChannel(m_frequency);
        if (ch.valid)
            name += QString("  ·  Channel %1 (video %2)").arg(ch.name, BandPlan::formatFrequency(ch.videoCarrierHz));
    }
    m_bandLabel->setText(name);
    m_modeLabel->setText(band.mode == Modulation::None ? QString() : "·  " + BandPlan::modulationName(band.mode));

    if (m_autoModeCheck->isChecked())
        autoSelectMode(band);
    for (ModePanel* p : m_panels)
        p->onBandChanged(band);
}

void StudioWindow::autoSelectMode(const BandInfo& band)
{
    // Only hop between the two receiver tabs; never pull the user off a TX tab.
    if (m_active && m_active->isTransmit()) return;

    int target = -1;
    switch (band.mode) {
    case Modulation::TV:
        target = m_tvRxTab;
        break;
    case Modulation::AM: case Modulation::NFM: case Modulation::WFM:
    case Modulation::LSB: case Modulation::USB:
        target = m_radioRxTab;
        break;
    default:
        break;
    }
    if (target >= 0 && target != m_tabs->currentIndex())
        m_tabs->setCurrentIndex(target);
}

// ============================================================================
// Device
// ============================================================================

TxGains StudioWindow::currentTxGains() const
{
    TxGains g;
    g.txVga = m_txVgaSlider->value();
    g.amp   = m_ampCheck->isChecked();
    return g;
}

RxGains StudioWindow::currentGains() const
{
    RxGains g;
    g.lna   = m_lnaSlider->value();
    g.vga   = m_vgaSlider->value();
    g.rxAmp = m_rxAmpSlider->value();
    g.amp   = m_ampCheck->isChecked();
    return g;
}

void StudioWindow::onSampleRateSelected(int index)
{
    const uint32_t rate = static_cast<uint32_t>(m_sampleRateCombo->itemData(index).toInt());
    if (rate == m_sampleRate) return;
    m_sampleRate = rate;

    m_plotter->setSampleRate(static_cast<float>(rate));
    m_plotter->setSpanFreq(rate);
    m_plotter->setFreqUnits(rate >= 8000000 ? 1000000 : 1000);
    m_device->setSampleRate(rate);
    m_engine->setSampleRate(rate);
    for (ModePanel* p : m_panels)
        p->onSampleRateChanged(rate);
}

void StudioWindow::onStartStop()
{
    if (m_device->isRunning())
        stopDevice();
    else
        startDevice();
}

void StudioWindow::startDevice()
{
    if (m_device->isRunning() || !m_active) return;

    m_sampleRate = static_cast<uint32_t>(m_sampleRateCombo->currentData().toInt());

    if (m_active->isTransmit()) {
        ITxSource* src = m_active->txSource();
        if (!src) {
            QMessageBox::warning(this, "HackTvStudio", "This mode has no transmit source.");
            return;
        }
        if (!m_txEngine->start(&m_device->txRing(), m_sampleRate, src)) {
            QMessageBox::warning(this, "HackTvStudio",
                                 "The transmitter could not be prepared. Check the mode / source settings "
                                 "(details in the panel status line).");
            m_active->onDeviceStopped();
            return;
        }
        // Fill the ring before the USB stream starts so the first transfers are not zeros
        m_txEngine->waitPrefilled(3000);
        if (!m_device->startTx(m_sampleRate, m_frequency, currentTxGains())) {
            m_txEngine->stop();
            return;
        }
        m_active->onDeviceStarted();
        return;
    }

    m_engine->setSink(m_active->rxSink());
    m_engine->start(&m_device->rxRing(), m_sampleRate);

    if (!m_device->startRx(m_sampleRate, m_frequency, currentGains())) {
        m_engine->stop();
        m_engine->setSink(nullptr);
        return;   // errorOccurred already shown
    }
    m_active->onDeviceStarted();
}

void StudioWindow::stopDevice()
{
    if (!m_device->isRunning() && !m_engine->isRunning() && !m_txEngine->isRunning()) return;

    m_device->stop();          // USB stops (RX: no more writes, TX: no more reads)
    m_engine->stop();          // joins the RX DSP thread, drains the sink
    m_engine->setSink(nullptr);
    m_txEngine->stop();        // joins the TX thread, stops the source
    if (m_active) m_active->onDeviceStopped();
}

void StudioWindow::onDeviceState(DeviceController::State state)
{
    const bool running = state != DeviceController::State::Idle;
    m_startButton->setText(running ? "STOP" : "START");
    m_startButton->setStyleSheet(running
        ? "QPushButton { background: #a03030; color: white; font-size: 12pt; }"
        : "QPushButton { background: #1f7a3f; color: white; font-size: 12pt; }");
    m_sampleRateCombo->setEnabled(true);
    m_recButton->setEnabled(running);

    switch (state) {
    case DeviceController::State::Rx:
        m_stateLabel->setText("RX");
        m_stateLabel->setStyleSheet("QLabel { font-weight: bold; color: #00ffcc; padding: 0 8px; }");
        break;
    case DeviceController::State::Tx:
        m_stateLabel->setText("TX");
        m_stateLabel->setStyleSheet("QLabel { font-weight: bold; color: #ff4040; padding: 0 8px; }");
        break;
    default:
        m_stateLabel->setText("IDLE");
        m_stateLabel->setStyleSheet("QLabel { font-weight: bold; color: #708090; padding: 0 8px; }");
        m_meter->setLevel(-120.0f);
        break;
    }
}

void StudioWindow::onDeviceError(const QString& message)
{
    qCritical() << "[HackRF]" << message;
    QMessageBox::critical(this, "HackRF", message);
}

void StudioWindow::onSpectrum(const QVector<float>& bins, float levelDbfs)
{
    if (!m_device->isRunning()) return;
    QVector<float> data = bins;   // setNewFttData wants a non-const pointer
    m_plotter->setNewFttData(data.data(), data.data(), data.size());

    // Channel power: sum the bins inside the active panel's meter range.
    // Bins are dBFS per bin (|X|^2 / n^2); the Hann window mean(w^2) = 0.375
    // compensates the window loss so a full-scale tone reads ~0 dBFS.
    float level = levelDbfs;
    int lowHz = 0, highHz = 0;
    if (m_active && m_active->meterRange(m_frequency, lowHz, highHz) && !bins.isEmpty()) {
        const int n = bins.size();
        const double binHz = static_cast<double>(m_sampleRate) / n;
        int i0 = n / 2 + static_cast<int>(std::floor(lowHz / binHz));
        int i1 = n / 2 + static_cast<int>(std::ceil(highHz / binHz));
        i0 = std::max(0, std::min(i0, n - 1));
        i1 = std::max(i0, std::min(i1, n - 1));
        double sum = 0.0;
        for (int i = i0; i <= i1; ++i)
            sum += std::pow(10.0, bins[i] / 10.0);
        level = static_cast<float>(10.0 * std::log10(std::max(sum / 0.375, 1e-12)));
        m_meterInfo->setText(QString("%1 kHz wide").arg((highHz - lowHz) / 1000.0, 0, 'f', 0));
    } else {
        m_meterInfo->setText("whole span");
    }
    m_meter->setLevel(level);
}

void StudioWindow::onRecordIq()
{
    if (!m_device->isRunning()) return;
    if (m_engine->isRecording()) {
        m_engine->stopRecording();
        return;
    }
    const QString name = QString("iq_%1MHz_%2.raw")
                             .arg(m_frequency / 1e6, 0, 'f', 3)
                             .arg(QDateTime::currentDateTime().toString("HHmmss"));
    const int64_t bytes = static_cast<int64_t>(m_sampleRate) * 2 / 2;   // 0.5 s of int8 IQ
    if (m_engine->startRecording(name, bytes)) {
        m_recButton->setText("REC ...");
        m_recButton->setStyleSheet("QPushButton { background: #a03030; color: white; }");
        m_panelStatusLabel->setText("Recording " + name);
    }
}

void StudioWindow::onRecordingFinished(const QString& path, qint64 bytes)
{
    m_recButton->setText("REC IQ 0.5 s");
    m_recButton->setStyleSheet(QString());
    m_panelStatusLabel->setText(QString("Saved %1 (%2 MB)").arg(path).arg(bytes / 1048576.0, 0, 'f', 1));
}

// ============================================================================
// Tabs
// ============================================================================

void StudioWindow::onTabChanged(int index)
{
    if (index < 0 || index >= static_cast<int>(m_panels.size())) return;
    ModePanel* next = m_panels[index];
    if (next == m_active) return;

    const bool wasRunning = m_device->isRunning();
    if (wasRunning) stopDevice();

    if (m_active) m_active->onDeactivated();
    m_active = next;
    m_plotter->setFilterBoxEnabled(m_active->wantsFilterBox());
    dockSpectrum(m_active);
    m_active->onActivated();   // a channel panel re-requests its filter box here
    m_panelStatusLabel->clear();

    const uint32_t pref = m_active->preferredSampleRate();
    if (pref && pref != m_sampleRate) {
        const int i = m_sampleRateCombo->findData(static_cast<int>(pref));
        if (i >= 0) m_sampleRateCombo->setCurrentIndex(i);   // triggers onSampleRateSelected
    }

    // Keep receiving when hopping between RX modes
    if (wasRunning && !m_active->isTransmit())
        startDevice();
}

void StudioWindow::updateStatusBar()
{
    if (m_device->state() == DeviceController::State::Tx) {
        const uint64_t under = m_device->txUnderrunBytes();
        m_overflowLabel->setText(under ? QString("TX underrun %1 KB").arg(under / 1024) : QString());
    } else {
        const uint64_t ovf = m_device->rxOverflowBytes();
        m_overflowLabel->setText(ovf ? QString("overflow %1 KB").arg(ovf / 1024) : QString());
    }

    const float clip = m_device->isRunning() ? m_engine->clipRatio() : 0.0f;
    if (clip > 0.001f)
        m_overloadLabel->setText(QString("ADC OVERLOAD %1%").arg(clip * 100.0f, 0, 'f', 1));
    else
        m_overloadLabel->clear();
}

// ============================================================================
// Settings
// ============================================================================

void StudioWindow::saveSettings()
{
    QSettings s("MarenRobotics", "HackTvStudio");
    s.setValue("frequency",  static_cast<qulonglong>(m_frequency));
    s.setValue("sampleRate", static_cast<int>(m_sampleRate));
    s.setValue("lnaGain",    m_lnaSlider->value());
    s.setValue("vgaGain",    m_vgaSlider->value());
    s.setValue("rxAmpGain",  m_rxAmpSlider->value());
    s.setValue("txVgaGain",  m_txVgaSlider->value());
    s.setValue("ampEnable",  m_ampCheck->isChecked());
    s.setValue("autoMode",   m_autoModeCheck->isChecked());
    s.setValue("activeTab",  m_tabs->currentIndex());
    s.setValue("plotterFloor", m_plotterRange->value());
    if (!m_specDocked && m_splitter->count() == 2) m_splitterStateV = m_splitter->saveState();
    s.setValue("splitterV3", m_splitterStateV);
    s.setValue("geometry",   saveGeometry());
    for (ModePanel* p : m_panels)
        p->saveSettings(s);
}

void StudioWindow::loadSettings()
{
    QSettings s("MarenRobotics", "HackTvStudio");

    // One-time import of the tuned PALBDecoder settings (same keys, same meaning)
    if (!s.contains("migratedFromPALBDecoder")) {
        QSettings legacy("MarenRobotics", "PALBDecoder");
        if (legacy.contains("frequency")) {
            for (const QString& key : legacy.allKeys())
                s.setValue(key, legacy.value(key));
            qDebug() << "Imported settings from PALBDecoder";
        }
        s.setValue("migratedFromPALBDecoder", true);
    }

    m_frequency = s.value("frequency", static_cast<qulonglong>(479250000ULL)).toULongLong();
    const int rate = s.value("sampleRate", 16000000).toInt();
    const int ri = m_sampleRateCombo->findData(rate);
    if (ri >= 0) {
        m_sampleRateCombo->blockSignals(true);
        m_sampleRateCombo->setCurrentIndex(ri);
        m_sampleRateCombo->blockSignals(false);
        m_sampleRate = static_cast<uint32_t>(rate);
        m_plotter->setSampleRate(static_cast<float>(rate));
        m_plotter->setSpanFreq(static_cast<quint32>(rate));
        m_plotter->setFreqUnits(rate >= 8000000 ? 1000000 : 1000);
        for (ModePanel* p : m_panels) p->onSampleRateChanged(m_sampleRate);
    }
    m_lnaSlider->setValue(s.value("lnaGain", 32).toInt());
    m_vgaSlider->setValue(s.value("vgaGain", 30).toInt());
    m_rxAmpSlider->setValue(s.value("rxAmpGain", 14).toInt());
    m_txVgaSlider->setValue(s.value("txVgaGain", 20).toInt());
    m_ampCheck->setChecked(s.value("ampEnable", true).toBool());
    m_autoModeCheck->setChecked(s.value("autoMode", true).toBool());
    m_plotterRange->setValue(s.value("plotterFloor", -110).toInt());
    m_splitterStateV = s.value("splitterV3").toByteArray();
    placeSpectrumOnTop(m_splitterStateV);
    if (s.contains("geometry")) restoreGeometry(s.value("geometry").toByteArray());

    for (ModePanel* p : m_panels)
        p->loadSettings(s);

    const int tab = s.value("activeTab", m_tvRxTab).toInt();
    if (tab >= 0 && tab < m_tabs->count())
        m_tabs->setCurrentIndex(tab);
}
