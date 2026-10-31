#include "StudioWindow.h"
#include "ModePanel.h"
#include "RadioRxPanel.h"
#include "RadioTxPanel.h"
#ifndef Q_OS_ANDROID
#include "TvRxPanel.h"
#include "TvTxPanel.h"
#endif
#include "widgets/glplotter.h"
#include "widgets/freqctrl.h"
#include "widgets/meter.h"
#include "widgets/analogmeter.h"
#include "Theme.h"

#include <QApplication>
#include <algorithm>
#include <cmath>
#include <QCloseEvent>
#include <QDateTime>
#include <QDebug>
#include <QGroupBox>
#include <QGridLayout>
#include <QDialog>
#include <QLabel>
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

#ifdef Q_OS_ANDROID
constexpr const char* kSplitterKey = "splitterA1";   // vertical (spectrum on top) layout
constexpr uint64_t    kDefaultFrequency  = 100000000ULL;   // radio-only app: FM broadcast
constexpr uint32_t    kDefaultSampleRate = 2000000;
#else
constexpr const char* kSplitterKey = "splitterV4";   // vertical layout (spectrum on top) on the desktop too
constexpr uint64_t    kDefaultFrequency  = 479250000ULL;   // desktop: analogue TV channel (original)
constexpr uint32_t    kDefaultSampleRate = 16000000;
#endif

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
    QVBoxLayout* specOuter = new QVBoxLayout(specWidget);
    specOuter->setContentsMargins(0, 0, 0, 0);
    specOuter->setSpacing(4);
    if (m_meterGroup) m_meterGroup->hide();   // the bar meter is replaced by the needle S-meter (right of the spectrum)
    QWidget* specRow = new QWidget(specWidget);
    QHBoxLayout* specLayout = new QHBoxLayout(specRow);
    specLayout->setContentsMargins(0, 0, 0, 0);
    specLayout->setSpacing(4);
    specOuter->addWidget(specRow, 1);

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
    {
        const Theme& t = Themes::current();
        m_plotter->setThemeColors(t.plotBg, t.plotGrid, t.plotText, t.wfBg, t.fftLine, t.fillTop, t.fillBottom);
    }
#ifdef Q_OS_ANDROID
    m_plotter->setMinimumHeight(120);
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
    // Needle S-meter at the far right, same height as spectrum + waterfall
    m_analogMeter = new AnalogMeter(specRow);
    {
        const Theme& t = Themes::current();
        m_analogMeter->setThemeColors(t.plotBg, t.plotText, t.accent);
    }
    QVBoxLayout* meterCol = new QVBoxLayout();      // meter at the top, nothing drawn below it
    meterCol->setContentsMargins(0, 0, 0, 0);
    meterCol->addWidget(m_analogMeter, 0, Qt::AlignTop);
    meterCol->addStretch(1);
    specLayout->addLayout(meterCol);

    // ---- Mode tabs
    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    addPanel(new RadioRxPanel(this));
    m_radioRxTab = 0;
    addPanel(new RadioTxPanel(this));
#ifndef Q_OS_ANDROID
    addPanel(new TvRxPanel(this));
    m_tvRxTab = 2;
    addPanel(new TvTxPanel(this));
#endif
    connect(m_tabs, &QTabWidget::currentChanged, this, &StudioWindow::onTabChanged);

    // Spectrum column / strip and the mode tabs share one splitter whose orientation
    // decides the layout: vertical = spectrum on top, horizontal = spectrum on the right.
    // Controls on the left, spectrum + waterfall on the right half (head-unit friendly).
    // Spectrum + waterfall (+ S-meter) fill the width on top, the mode tabs sit under them.
    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->addWidget(specWidget);
    m_splitter->addWidget(m_tabs);
    m_splitter->setHandleWidth(4);
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
    m_freqCtrl->setMinimumHeight(74);        // big digits: tuned by finger on the head unit
    m_freqCtrl->setMaximumHeight(96);
#else
    m_freqCtrl->setMinimumHeight(64);
    m_freqCtrl->setMaximumHeight(80);
#endif
#ifdef Q_OS_ANDROID
    m_freqCtrl->setMinimumWidth(440);        // big digits (height-driven) but leaves room for HackRF at 1024px
#else
    m_freqCtrl->setMinimumWidth(420);
#endif
    connect(m_freqCtrl, &CFreqCtrl::newFrequency, this, &StudioWindow::onFrequencyEdited);
    tl->addWidget(m_freqCtrl);

    QHBoxLayout* bandRow = new QHBoxLayout();
    m_bandLabel = new QLabel(tuneGroup);
    m_bandLabel->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; font-size: 11pt; }").arg(Themes::current().accent.name()));
    m_modeLabel = new QLabel(tuneGroup);
    m_modeLabel->setStyleSheet("QLabel { font-size: 10pt; }");
    m_autoModeCheck = new QCheckBox("Auto mode", tuneGroup);
    m_autoModeCheck->setToolTip("Switch to the TV or Radio receiver automatically from the band plan");
    m_autoModeCheck->setChecked(true);
    bandRow->addWidget(m_bandLabel);
    bandRow->addWidget(m_modeLabel, 1);
    // Quick tuning: ±1 MHz / ±100 kHz buttons beside the band name. "Auto mode"
    // lives in the device box (desktop) / ⚙ settings dialog (Android), see buildDeviceGroup.
    auto stepBtn = [this, tuneGroup](const QString& text, qint64 delta) {
        QPushButton* b = new QPushButton(text, tuneGroup);
        b->setMinimumHeight(34);
        b->setMinimumWidth(68);
        b->setStyleSheet("QPushButton { font-size: 12pt; font-weight: bold; padding: 2px 6px; }");
        connect(b, &QPushButton::clicked, this, [this, delta] {
            qint64 f = static_cast<qint64>(m_frequency) + delta;
            f = std::max<qint64>(0, std::min<qint64>(f, static_cast<qint64>(BandPlan::HACKRF_MAX_HZ)));
            setFrequency(static_cast<uint64_t>(f), false);
        });
        return b;
    };
    bandRow->addWidget(stepBtn(QString::fromUtf8("−1 M"),   -1000000));
    bandRow->addWidget(stepBtn(QString::fromUtf8("−100 k"), -100000));
    bandRow->addWidget(stepBtn("+100 k", 100000));
    bandRow->addWidget(stepBtn("+1 M",   1000000));
    tl->addLayout(bandRow);
#ifdef Q_OS_ANDROID
    l->addWidget(tuneGroup, 3);          // big frequency, but 3:2 leaves the HackRF box real room (no right-edge clip on the head unit)
#else
    l->addWidget(tuneGroup, 2);
#endif

    // Big finger-sized + / - between the frequency and the HackRF box. The step
    // follows the channel width of the active panel (WFM 100 kHz, NFM 12.5 kHz, AM 10 kHz).
    QVBoxLayout* stepCol = new QVBoxLayout();
    stepCol->setSpacing(4);
    stepCol->setContentsMargins(0, 0, 0, 0);
    auto bigStep = [this, strip](const QString& sign, int dir) {
        QPushButton* b = new QPushButton(sign, strip);
        b->setMinimumWidth(78);
        b->setMaximumWidth(96);
        b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        b->setStyleSheet("QPushButton { font-size: 15pt; font-weight: bold; padding: 2px; }");
        b->setAutoRepeat(true);
        b->setAutoRepeatDelay(400);
        b->setAutoRepeatInterval(120);
        connect(b, &QPushButton::clicked, this, [this, dir] {
            qint64 f = static_cast<qint64>(m_frequency) + dir * fingerStepHz();
            f = std::max<qint64>(0, std::min<qint64>(f, static_cast<qint64>(BandPlan::HACKRF_MAX_HZ)));
            setFrequency(static_cast<uint64_t>(f), false);
        });
        return b;
    };
    m_stepUpBtn   = bigStep("+", +1);
    m_stepDownBtn = bigStep(QString::fromUtf8("−"), -1);
    stepCol->addWidget(m_stepUpBtn, 1);
    stepCol->addWidget(m_stepDownBtn, 1);
    l->addLayout(stepCol);

    // S-meter: power inside the tuned channel (filter box / TV channel)
    m_meterGroup = new QGroupBox("Channel power", strip);
    QGroupBox* meterGroup = m_meterGroup;
    QVBoxLayout* ml = new QVBoxLayout(meterGroup);
    ml->setSpacing(2);
#ifdef Q_OS_ANDROID
    ml->setContentsMargins(6, 2, 6, 2);      // compact: docked above the spectrum
#endif
    m_meter = new CMeter(meterGroup);
#ifdef Q_OS_ANDROID
    m_meter->setMinimumHeight(44);
    m_meter->setMaximumHeight(52);
#else
    m_meter->setMinimumHeight(58);
    m_meter->setMaximumHeight(80);
#endif
#ifdef Q_OS_ANDROID
    m_meter->setMinimumWidth(150);
#else
    m_meter->setMinimumWidth(220);
#endif
    ml->addWidget(m_meter);
    QHBoxLayout* mi = new QHBoxLayout();
    m_meterInfo = new QLabel("", meterGroup);
    m_meterInfo->setStyleSheet("QLabel { color: #708090; font-size: 8pt; }");
    m_overloadLabel = new QLabel("", meterGroup);
    m_overloadLabel->setStyleSheet("QLabel { color: #ff4040; font-weight: bold; font-size: 9pt; }");
    m_overloadLabel->setToolTip("ADC samples hitting full scale: lower LNA/VGA or switch the RF amp off");
    mi->addWidget(m_meterInfo, 1);
    mi->addWidget(m_overloadLabel);
    ml->addLayout(mi);                       // (group stays hidden: the needle S-meter replaced it)

#ifdef Q_OS_ANDROID
    l->addWidget(buildDeviceGroup(), 2);     // 3:2 with frequency: enough width that the box border + TX spin never clip
#else
    l->addWidget(buildDeviceGroup(), 2);
#endif
    return strip;
}

QWidget* StudioWindow::buildDeviceGroup()
{
    QGroupBox* g = new QGroupBox("HackRF One", this);
    QGridLayout* grid = new QGridLayout(g);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(3);
#ifdef Q_OS_ANDROID
    grid->setVerticalSpacing(6);   // vertical stack: keep rows from touching
    grid->setContentsMargins(6, 4, 8, 4);
#endif

    m_startButton = new QPushButton("START", g);
    m_startButton->setMinimumHeight(40);
#ifdef Q_OS_ANDROID
    m_startButton->setMinimumHeight(46);   // big, prominent START on top of the box
#endif
    m_startButton->setStyleSheet("QPushButton { background: #1f7a3f; color: white; font-size: 12pt; }");
    connect(m_startButton, &QPushButton::clicked, this, &StudioWindow::onStartStop);

    m_sampleRateCombo = new QComboBox(g);
    const int rates[] = { 2000000, 4000000, 6000000, 8000000, 10000000, 12000000,
                          12500000, 14000000, 16000000, 18000000, 20000000 };
    for (int r : rates)
        m_sampleRateCombo->addItem(QString("%1 MS/s").arg(r / 1e6, 0, 'g', 4), r);
    m_sampleRateCombo->setCurrentIndex(m_sampleRateCombo->findData(static_cast<int>(m_sampleRate)));
    connect(m_sampleRateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StudioWindow::onSampleRateSelected);

    m_ampCheck = new QCheckBox("RF Amp", g);
    m_ampCheck->setChecked(true);
    connect(m_ampCheck, &QCheckBox::toggled, this, [this](bool on) { m_device->setAmpEnable(on); });

    m_recButton = new QPushButton("REC IQ 0.5 s", g);
    m_recButton->setToolTip("Record 0.5 s of raw int8 IQ to iq_<freq>_<time>.raw");
    connect(m_recButton, &QPushButton::clicked, this, &StudioWindow::onRecordIq);
    m_recButton->setMinimumHeight(m_sampleRateCombo->sizeHint().height());

    // Build one "name [slider] [spin]" row into any grid (main strip or settings dialog).
    auto gainRow = [](QGridLayout* gg, QWidget* parent, int row, int col, const QString& name,
                      QSlider*& slider, QSpinBox*& spin, int min, int max, int step, int value) {
        gg->addWidget(new QLabel(name, parent), row, col, Qt::AlignRight);
        slider = new QSlider(Qt::Horizontal, parent);
        slider->setRange(min, max); slider->setSingleStep(step); slider->setPageStep(step);
        slider->setValue(value); slider->setMinimumWidth(70);
        spin = new QSpinBox(parent);
        spin->setRange(min, max); spin->setSingleStep(step); spin->setValue(value);
        spin->setMaximumWidth(64);
        QSlider* s = slider; QSpinBox* sp = spin;
        connect(slider, &QSlider::valueChanged, parent, [sp](int v) {
            sp->blockSignals(true); sp->setValue(v); sp->blockSignals(false);
        });
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), parent, [s](int v) { s->setValue(v); });
        gg->addWidget(slider, row, col + 1);
        gg->addWidget(spin, row, col + 2);
    };

#ifdef Q_OS_ANDROID
    // ---- Head unit: compact strip = START, RF Amp, TX gain, Settings button.
    //      Everything else (sample rate, RX gains) goes to a separate screen so the
    //      top strip fits a small in-dash display.
    // Row 0: big START (full width). Row 1: RF Amp (big) + long TX bar + big value box, all on
    // one line at the same height. Row 2: Settings. Columns: [RFAmp][TX lbl][slider][spin].
    grid->addWidget(m_startButton, 0, 0, 1, 4);                 // START (big) on top, full width
    m_ampCheck->setStyleSheet("QCheckBox { font-size: 13pt; spacing: 10px; }"
                              "QCheckBox::indicator { width: 30px; height: 30px; }");   // bigger checkbox
    grid->addWidget(m_ampCheck, 1, 0, Qt::AlignVCenter);       // RF Amp, lower (TX row), left
    gainRow(grid, g, 1, 1, "TX", m_txVgaSlider, m_txVgaSpin, 0, 47, 1, 20);  // TX lbl c1, slider c2, spin c3
    m_txVgaSlider->setMinimumWidth(80);
    m_txVgaSlider->setMinimumHeight(34);                        // taller/bigger TX bar
    m_txVgaSpin->setMinimumWidth(66);                           // bigger value box
    m_txVgaSpin->setMaximumWidth(96);
    m_txVgaSpin->setMinimumHeight(34);
    m_txVgaSpin->setStyleSheet("QSpinBox { font-size: 13pt; }");
    m_settingsBtn = new QPushButton(QString::fromUtf8("⚙  Ayarlar"), g);
    connect(m_settingsBtn, &QPushButton::clicked, this, &StudioWindow::openSettingsDialog);
    grid->addWidget(m_settingsBtn, 2, 0, 1, 4);                 // Settings, full width
    grid->setColumnStretch(2, 1);                              // TX slider column stretches = long bar
    grid->setContentsMargins(6, 2, 8, 2);

    // ---- Settings screen (modal): rate + RX gains + IQ record.
    m_settingsDlg = new QDialog(this);
    m_settingsDlg->setWindowTitle(QString::fromUtf8("Ayarlar"));
    QGridLayout* dg = new QGridLayout(m_settingsDlg);
    dg->addWidget(new QLabel(QString::fromUtf8("Örnekleme (RX):"), m_settingsDlg), 0, 0, Qt::AlignRight);
    m_sampleRateCombo->setParent(m_settingsDlg);
    dg->addWidget(m_sampleRateCombo, 0, 1, 1, 2);
    gainRow(dg, m_settingsDlg, 1, 0, "LNA",    m_lnaSlider,   m_lnaSpin,   0, 40, 8, 32);
    gainRow(dg, m_settingsDlg, 2, 0, "VGA",    m_vgaSlider,   m_vgaSpin,   0, 62, 2, 30);
    gainRow(dg, m_settingsDlg, 3, 0, "RX AMP", m_rxAmpSlider, m_rxAmpSpin, 0, 14, 14, 14);
    m_autoModeCheck->setParent(m_settingsDlg);
    m_autoModeCheck->setText(QString::fromUtf8("Banda göre RX/TX sekmesini otomatik seç"));
    dg->addWidget(m_autoModeCheck, 4, 0, 1, 3);
    dg->addWidget(new QLabel("Tema:", m_settingsDlg), 5, 0, Qt::AlignRight);
    m_themeCombo = new QComboBox(m_settingsDlg);
    for (const Theme& t : Themes::all())
        m_themeCombo->addItem(t.name, t.id);
    m_themeCombo->setCurrentIndex(std::max(0, m_themeCombo->findData(Themes::current().id)));
    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i) { applyTheme(m_themeCombo->itemData(i).toString()); });
    dg->addWidget(m_themeCombo, 5, 1, 1, 2);
    QPushButton* closeBtn = new QPushButton(QString::fromUtf8("Kapat"), m_settingsDlg);
    connect(closeBtn, &QPushButton::clicked, m_settingsDlg, &QDialog::accept);
    dg->addWidget(closeBtn, 6, 0, 1, 3);
    m_recButton->setParent(m_settingsDlg);
    m_recButton->hide();   // IQ-to-file capture is a desktop/debug feature
#else
    // ---- Desktop: the full device panel in the strip.
    grid->addWidget(m_startButton, 0, 0, 2, 2);
    grid->addWidget(new QLabel("Rate:", g), 0, 3, Qt::AlignRight);
    grid->addWidget(m_sampleRateCombo, 0, 4, 1, 2);
    grid->addWidget(m_ampCheck, 0, 2, 2, 1, Qt::AlignCenter);
    grid->addWidget(m_recButton, 1, 4, 1, 2);
    gainRow(grid, g, 2, 0, "LNA",    m_lnaSlider,   m_lnaSpin,   0, 40, 8, 32);
    gainRow(grid, g, 2, 3, "VGA",    m_vgaSlider,   m_vgaSpin,   0, 62, 2, 30);
    gainRow(grid, g, 3, 0, "AMP",    m_rxAmpSlider, m_rxAmpSpin, 0, 14, 14, 14);
    gainRow(grid, g, 3, 3, "TX VGA", m_txVgaSlider, m_txVgaSpin, 0, 47, 1, 20);
    // Row 4: auto mode (moved out of the frequency box to make room for the step buttons) + theme
    m_autoModeCheck->setParent(g);
    grid->addWidget(m_autoModeCheck, 4, 0, 1, 3);
    grid->addWidget(new QLabel("Theme:", g), 4, 3, Qt::AlignRight);
    m_themeCombo = new QComboBox(g);
    for (const Theme& t : Themes::all())
        m_themeCombo->addItem(t.name, t.id);
    m_themeCombo->setCurrentIndex(std::max(0, m_themeCombo->findData(Themes::current().id)));
    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i) { applyTheme(m_themeCombo->itemData(i).toString()); });
    grid->addWidget(m_themeCombo, 4, 4, 1, 2);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(4, 1);
#endif

    m_txVgaSlider->setToolTip("Transmit gain (0-47 dB). Start low; add the RF amp only if needed.");
    connect(m_lnaSlider, &QSlider::valueChanged, this, [this](int v) { m_device->setLnaGain(v); });
    connect(m_vgaSlider, &QSlider::valueChanged, this, [this](int v) { m_device->setVgaGain(v); });
    connect(m_rxAmpSlider, &QSlider::valueChanged, this, [this](int v) { m_device->setRxAmpGain(v); });
    connect(m_txVgaSlider, &QSlider::valueChanged, this, [this](int v) { m_device->setTxVgaGain(v); });
    return g;
}

qint64 StudioWindow::fingerStepHz() const
{
    int lo = 0, hi = 0;
    if (m_active && m_active->meterRange(m_frequency, lo, hi)) {
        // Channel width -> a sensible tuning raster for that service
        const int bw = std::max(std::abs(lo), std::abs(hi));
        if (bw >= 100000) return 100000;   // WFM broadcast
        if (bw >= 25000)  return 25000;    // airband / wide NFM
        if (bw >= 12500)  return 12500;    // NFM (PMR / amateur)
        if (bw >= 10000)  return 10000;    // AM
        if (bw >= 5000)   return 5000;
        return 1000;
    }
    const qint64 dial = m_freqCtrl ? m_freqCtrl->getActiveStep() : 0;   // TX: the digit selected on the dial
    return dial > 0 ? dial : 100000;
}

void StudioWindow::refreshStepLabels()
{
    if (!m_stepUpBtn) return;
    const qint64 st = fingerStepHz();
    QString s;
    if (st >= 1000000)   s = QString("%1 M").arg(st / 1e6, 0, 'g', 3);
    else if (st >= 1000) s = QString("%1 k").arg(st / 1e3, 0, 'g', 4);
    else                 s = QString("%1 Hz").arg(st);
    m_stepUpBtn->setText("+\n" + s);
    m_stepDownBtn->setText(QString::fromUtf8("−\n") + s);
}

void StudioWindow::applyTheme(const QString& id)
{
    const Theme& t = Themes::byId(id);
    Themes::apply(t);
    m_plotter->setThemeColors(t.plotBg, t.plotGrid, t.plotText, t.wfBg, t.fftLine, t.fillTop, t.fillBottom);
    if (m_analogMeter) m_analogMeter->setThemeColors(t.plotBg, t.plotText, t.accent);
    m_bandLabel->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; font-size: 11pt; }").arg(t.accent.name()));
    QSettings s("MarenRobotics", "HackTvStudio");
    s.setValue("theme", t.id);
}

void StudioWindow::openSettingsDialog()
{
    if (!m_settingsDlg) return;
    m_settingsDlg->resize(std::min(460, width() - 20), m_settingsDlg->sizeHint().height());
    m_settingsDlg->exec();
}

void StudioWindow::addPanel(ModePanel* panel)
{
    m_panels.push_back(panel);

#ifdef Q_OS_ANDROID
    // Head unit: no scroll area — the layout compresses to fit the screen so every
    // tab is fully visible (overflow controls live in the ⚙ settings dialog).
    m_tabs->addTab(panel, panel->title());
#else
    // Desktop: a vertical-only scroll area: on a small window the panel scrolls
    // instead of pushing the window's minimum height past the screen.
    QScrollArea* scroll = new QScrollArea(m_tabs);
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    m_tabs->addTab(scroll, panel->title());
#endif
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
        // Back to the right side of the controls
        m_specDocked = false;
        m_splitter->insertWidget(1, m_specWidget);
        m_specWidget->show();
        placeSpectrumOnTop(m_splitterStateV);
    }
}

// Horizontal layout: controls on the left, spectrum + waterfall on the right half.
void StudioWindow::placeSpectrumOnTop(const QByteArray& state)
{
    if (!m_splitter || m_specDocked) return;
    m_splitter->insertWidget(0, m_specWidget);   // spectrum + waterfall full width on top
    m_splitter->insertWidget(1, m_tabs);         // mode panel below
    m_plotter->setPercent2DScreen(55);
#ifdef Q_OS_ANDROID
    m_tabs->setMaximumHeight(215);               // head unit: one compact control row
#endif
    if (!state.isEmpty()) {
        m_splitter->restoreState(state);
    } else {
#ifdef Q_OS_ANDROID
        m_splitter->setStretchFactor(0, 1);      // all spare height goes to the spectrum
        m_splitter->setStretchFactor(1, 0);
        m_splitter->setSizes({ 400, 190 });
#else
        m_splitter->setStretchFactor(0, 3);      // desktop: the panels are taller, share the height
        m_splitter->setStretchFactor(1, 2);
        m_splitter->setSizes({ 480, 360 });
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
#ifndef Q_OS_ANDROID
    if (band.mode == Modulation::TV) {
        const TvChannel ch = BandPlan::tvChannel(m_frequency);
        if (ch.valid)
            name += QString("  ·  Channel %1 (video %2)").arg(ch.name, BandPlan::formatFrequency(ch.videoCarrierHz));
    }
#endif
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
#ifndef Q_OS_ANDROID
    case Modulation::TV:
        target = m_tvRxTab;
        break;
#endif
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
        if (m_analogMeter) { m_analogMeter->setLevel(0.0f); m_analogMeter->setClipRatio(0.0f); }
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
    if (m_analogMeter && !bins.isEmpty()) {
        // Needle = SNR: the strongest bin inside the tuned channel above the noise
        // floor (median of the whole span). Independent of LNA/VGA/AMP settings and
        // of how wide the channel is, so a good FM station and a good TV channel
        // both read around S9 instead of pegging the needle at full gain.
        const int n = bins.size();
        int i0 = 0, i1 = n - 1;
        if (lowHz < highHz) {
            const double binHz = static_cast<double>(m_sampleRate) / n;
            i0 = std::max(0, std::min(n - 1, n / 2 + static_cast<int>(std::floor(lowHz / binHz))));
            i1 = std::max(i0, std::min(n - 1, n / 2 + static_cast<int>(std::ceil(highHz / binHz))));
        }
        float peak = -200.0f;
        for (int i = i0; i <= i1; ++i) peak = std::max(peak, bins[i]);
        // Noise floor = 10th percentile of the span: the median is far too high when
        // the band is packed with stations (FM broadcast at 2 MS/s).
        QVector<float> sorted = bins;
        const int k = std::max(0, n / 10);
        std::nth_element(sorted.begin(), sorted.begin() + k, sorted.end());
        const float floor = sorted[k];
        m_analogMeter->setLevel(std::max(0.0f, peak - floor));
        m_analogMeter->setInfo(m_meterInfo->text());
    }
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
    if (m_analogMeter) m_analogMeter->setClipRatio(clip);
    refreshStepLabels();
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
    s.setValue(kSplitterKey, m_splitterStateV);
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

    m_frequency = s.value("frequency", static_cast<qulonglong>(kDefaultFrequency)).toULongLong();
    const int rate = s.value("sampleRate", static_cast<int>(kDefaultSampleRate)).toInt();
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
    m_splitterStateV = s.value(kSplitterKey).toByteArray();
    placeSpectrumOnTop(m_splitterStateV);
    if (s.contains("geometry")) restoreGeometry(s.value("geometry").toByteArray());

    for (ModePanel* p : m_panels)
        p->loadSettings(s);

#ifdef Q_OS_ANDROID
    const int tab = s.value("activeTab", m_radioRxTab).toInt();
#else
    const int tab = s.value("activeTab", m_tvRxTab).toInt();
#endif
    if (tab >= 0 && tab < m_tabs->count())
        m_tabs->setCurrentIndex(tab);
}
