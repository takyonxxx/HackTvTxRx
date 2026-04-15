#include "mainwindow.h"
#include <QApplication>
#include <QFuture>
#include <QLabel>
#include <QtConcurrent/QtConcurrent>
#include <QFrame>
#include <QScrollArea>
#include <QStandardPaths>
#include <QDir>
#include <QSettings>
#include <memory>
#include "constants.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

// ============================================================
// Constructor / Destructor
// ============================================================

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_shuttingDown(false)
    , m_isProcessing(false)
{
    QString homePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    m_sSettingsFile = homePath + "/hacktv_settings.ini";

    if (QFile::exists(m_sSettingsFile)) loadSettings();

    logBrowser = new QTextBrowser(this);
    logBrowser->setVisible(false);
    audioOutput = std::make_unique<AudioOutput>();
    if (audioOutput) audioOutput->setVolume(m_volumeLevel);

    m_threadPool = new QThreadPool(this);
    m_threadPool->setMaxThreadCount(QThread::idealThreadCount() / 2);

    setupUi();
    applyModePresets();

    logTimer = new QTimer(this);
    connect(logTimer, &QTimer::timeout, this, &MainWindow::updateLogDisplay);
    logTimer->start(500);

    m_initDone = true;
}

MainWindow::~MainWindow()
{
    m_shuttingDown.store(true);
    m_isProcessing.store(false);
    stopMicCapture();
    stopFilePlayback();
    if (m_hackTvLib) {
        m_hackTvLib->clearCallbacks();
        m_hackTvLib->stop();
        delete m_hackTvLib;
        m_hackTvLib = nullptr;
    }
}

// ============================================================
// UI Setup
// ============================================================

void MainWindow::setupUi()
{
    labelStyle = "QLabel { background-color: #ad6d0a; color: #fff8ee; border-radius: 3px; "
                 "font-weight: bold; padding: 2px 6px; font-size: 11px; }";

    setWindowTitle("HackTvRxTx");

    // Global dark theme
    setStyleSheet(
        "QMainWindow { background-color: #0d1117; }"
        "QGroupBox { background-color: #161b22; color: #c9d1d9; }"
        "QComboBox { background-color: #21262d; color: #c9d1d9; border: 1px solid #30363d; "
        "border-radius: 3px; padding: 3px 6px; font-size: 11px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #21262d; color: #c9d1d9; "
        "selection-background-color: #30363d; }"
        "QCheckBox { color: #c9d1d9; font-size: 11px; }"
        "QLineEdit { background-color: #21262d; color: #c9d1d9; border: 1px solid #30363d; "
        "border-radius: 3px; padding: 3px 6px; }"
        "QPushButton { background-color: #21262d; color: #c9d1d9; border: 1px solid #30363d; "
        "border-radius: 4px; padding: 6px 12px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #30363d; }"
        "QPushButton:pressed { background-color: #0d1117; }"
        "QSlider::groove:horizontal { border: 1px solid #30363d; height: 6px; "
        "background: #21262d; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #58a6ff; width: 14px; "
        "margin: -4px 0; border-radius: 7px; }"
        "QTextBrowser { background-color: #0d1117; color: #8b949e; border: 1px solid #21262d; }"
    );

    QWidget *centralWidget = new QWidget(this);
    mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(2);
    mainLayout->setContentsMargins(4, 2, 4, 4);

    addDeviceGroup();
    addRxGroup();
    addRadioTxGroup();
    addTvTxGroup();

    // Bottom buttons
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(6);

    startStopButton = new QPushButton("START", this);
    startStopButton->setMinimumHeight(28);
    connect(startStopButton, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);

    hardResetButton = new QPushButton("HARD RESET", this);
    hardResetButton->setMinimumHeight(28);
    hardResetButton->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #7a1a1a, stop:1 #551010); "
        "color: #ffcccc; border: 1px solid #cc3333; border-radius: 4px; padding: 6px 15px; "
        "font-weight: bold; font-size: 11px; }"
        "QPushButton:pressed { background: #551010; }");
    connect(hardResetButton, &QPushButton::clicked, this, &MainWindow::hardReset);

    exitButton = new QPushButton("EXIT", this);
    exitButton->setMinimumHeight(28);
    connect(exitButton, &QPushButton::clicked, this, &MainWindow::exitApp);

    btnLayout->addWidget(startStopButton, 2);
    btnLayout->addWidget(hardResetButton, 1);
    btnLayout->addWidget(exitButton, 1);
    mainLayout->addLayout(btnLayout);

    setCentralWidget(centralWidget);

    fileDialog = new QFileDialog(this);
    fileDialog->setFileMode(QFileDialog::ExistingFile);

    // Set combo to match loaded/default mode
    int modeIdx = operatingModeCombo->findData(m_opMode);
    if (modeIdx >= 0) operatingModeCombo->setCurrentIndex(modeIdx);
    onOperatingModeChanged(modeIdx);
}

void MainWindow::addDeviceGroup()
{
    deviceGroup = new QGroupBox("Device Settings", this);
    QGridLayout *lay = new QGridLayout(deviceGroup);
    lay->setVerticalSpacing(4);
    lay->setHorizontalSpacing(8);
    lay->setContentsMargins(10, 18, 10, 6);

    QString ls = "QLabel { color: #90c8e0; font-size: 11px; font-weight: bold; }";

    QLabel *devLabel = new QLabel("Device:", this);
    devLabel->setStyleSheet(ls);
    outputCombo = new QComboBox(this);
    outputCombo->addItem("HackRF", "hackrf");
    outputCombo->addItem("HackRF TCP", "hackrftcp");
    outputCombo->addItem("RTL-SDR TCP", "rtlsdrtcp");
    outputCombo->addItem("RtlSdr", "rtlsdr");
    connect(outputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this]() {
        QString dev = outputCombo->currentData().toString();
        bool isTcp = (dev == "hackrftcp" || dev == "rtlsdrtcp");
        tcpAddressLabel->setVisible(isTcp);
        tcpAddressEdit->setVisible(isTcp);
        // Set default port hint based on device
        if (dev == "hackrftcp")
            tcpAddressEdit->setPlaceholderText("127.0.0.1 (ports 5000-5002)");
        else if (dev == "rtlsdrtcp")
            tcpAddressEdit->setPlaceholderText("127.0.0.1:1234");
        if (m_initDone) saveSettings();
    });

    // TCP address field (shown only when HackRF TCP selected)
    tcpAddressLabel = new QLabel("Addr:", this);
    tcpAddressLabel->setStyleSheet(ls);
    tcpAddressLabel->setVisible(false);
    tcpAddressEdit = new QLineEdit("127.0.0.1", this);
    tcpAddressEdit->setMaximumWidth(120);
    tcpAddressEdit->setVisible(false);

    QLabel *modeLabel = new QLabel("Mode:", this);
    modeLabel->setStyleSheet(ls);
    operatingModeCombo = new QComboBox(this);
    operatingModeCombo->addItem("WFM Radio",    MODE_WFM);
    operatingModeCombo->addItem("NFM Radio",    MODE_NFM);
    operatingModeCombo->addItem("AM Radio",     MODE_AM);
    operatingModeCombo->addItem("FM File TX",   MODE_FM_FILE);
    operatingModeCombo->addItem("TV File TX",   MODE_TV_FILE);
    operatingModeCombo->addItem("TV Test TX",   MODE_TV_TEST);
    operatingModeCombo->addItem("TV RTSP TX",   MODE_TV_RTSP);
    connect(operatingModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onOperatingModeChanged);

    QLabel *bwLabel = new QLabel("BW:", this);
    bwLabel->setStyleSheet(ls);
    sampleRateCombo = new QComboBox(this);
    for (auto& [rate, txt] : std::map<int,QString>{
             {2000000,"2"},{4000000,"4"},{8000000,"8"},{10000000,"10"},
             {12500000,"12.5"},{16000000,"16"},{20000000,"20"}}) {
        sampleRateCombo->addItem(txt + " MHz", rate);
    }
    connect(sampleRateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSampleRateChanged);

    ampEnabled = new QCheckBox("RF Amp (TX)", this);
    ampEnabled->setMinimumWidth(90);
    connect(ampEnabled, &QCheckBox::stateChanged, [this]() {
        if (m_isProcessing && m_hackTvLib)
            m_hackTvLib->setAmpEnable(ampEnabled->isChecked());
        if (m_initDone) saveSettings();
    });

    stereoEnabled = new QCheckBox("Stereo", this);
    stereoEnabled->setChecked(true);
    stereoEnabled->setMinimumWidth(65);
    connect(stereoEnabled, &QCheckBox::stateChanged, [this]() {
        m_forceMono = !stereoEnabled->isChecked();
        if (fmDemodulator) fmDemodulator->setForceMono(m_forceMono);
        if (m_forceMono) {
            m_stereoLabel->setText("MONO");
            m_stereoLabel->setStyleSheet("QLabel { font-weight: bold; font-size: 18px; color: #FF9900; }");
        } else {
            bool st = fmDemodulator ? fmDemodulator->isStereo() : false;
            m_stereoLabel->setText(st ? "STEREO" : "");
            m_stereoLabel->setStyleSheet(st
                ? "QLabel { font-weight: bold; font-size: 18px; color: #00FF66; }"
                : "QLabel { font-weight: bold; font-size: 18px; color: #666666; }");
        }
        if (m_initDone) saveSettings();
    });

    lay->addWidget(devLabel, 0, 0);
    lay->addWidget(outputCombo, 0, 1);
    lay->addWidget(modeLabel, 0, 2);
    lay->addWidget(operatingModeCombo, 0, 3, 1, 2);
    lay->addWidget(bwLabel, 1, 0);
    lay->addWidget(sampleRateCombo, 1, 1);
    lay->addWidget(ampEnabled, 1, 2);
    lay->addWidget(stereoEnabled, 1, 3);
    lay->addWidget(tcpAddressLabel, 1, 4);
    lay->addWidget(tcpAddressEdit, 1, 5);

    lay->setColumnStretch(1, 2);
    lay->setColumnStretch(3, 3);
    mainLayout->addWidget(deviceGroup);
}

void MainWindow::addRxGroup()
{
    freqCtrl = new CFreqCtrl();
    freqCtrl->setup(0, 0, 6000e6, 1, FCTL_UNIT_MHZ);
    freqCtrl->setDigitColor(QColor("#FFC300"));
    freqCtrl->setFrequency(m_frequency);
    connect(freqCtrl, &CFreqCtrl::newFrequency, this, &MainWindow::onFreqCtrl_setFrequency);
    freqCtrl->setMinimumHeight(68);
    freqCtrl->setMaximumHeight(90);

    cPlotter = new CPlotter(this);
    cPlotter->setSampleRate(m_sampleRate);
    cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));
    cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));
    cPlotter->setFftRange(-110.0f, 0.0f);
    cPlotter->setPandapterRange(-110.f, 0.f);
    cPlotter->setFreqUnits(1000);
    cPlotter->setPercent2DScreen(50);
    cPlotter->setFilterBoxEnabled(true);
    cPlotter->setCenterLineEnabled(true);
    cPlotter->setClickResolution(1);
    cPlotter->setFftPlotColor(QColor("#CEECF5"));
    cPlotter->setFftFill(true);
    cPlotter->setMinimumHeight(150);
    cPlotter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    cPlotter->setDemodRanges(-200000, -_KHZ(5), _KHZ(5), 200000, true);
    cPlotter->setHiLowCutFrequencies(m_LowCutFreq, m_HiCutFreq);

    connect(cPlotter, &CPlotter::newDemodFreq, this, &MainWindow::on_plotter_newDemodFreq);
    connect(cPlotter, &CPlotter::newFilterFreq, this, &MainWindow::on_plotter_newFilterFreq);
    connect(cPlotter, &CPlotter::wheelFreqChange, this, [this](int dir) {
        qint64 step = freqCtrl->getActiveStep();
        m_frequency += dir * step;
        freqCtrl->setFrequency(m_frequency);
        cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));
        if (m_isProcessing && m_hackTvLib) m_hackTvLib->setFrequency(m_frequency);
        if (m_tcpConnected) sendTcpCommand(QString("SET_FREQ:%1").arg(m_frequency));
        saveSettings();
    });

    cMeter = new CMeter(this);
    cMeter->setMinimumHeight(58);
    cMeter->setMaximumHeight(75);

    // Vertical gain slider
    QSlider *plotterGainSlider = new QSlider(Qt::Vertical, this);
    plotterGainSlider->setRange(-130, 0);
    plotterGainSlider->setValue(-110);
    plotterGainSlider->setFixedWidth(22);
    plotterGainSlider->setStyleSheet(
        "QSlider::groove:vertical { border: 1px solid #334455; width: 6px; background: #1a2a3a; border-radius: 3px; }"
        "QSlider::handle:vertical { background: #FFC300; height: 14px; margin: 0 -4px; border-radius: 7px; }");
    connect(plotterGainSlider, &QSlider::valueChanged, [this](int v) {
        cPlotter->setPandapterRange(static_cast<float>(v), 0.f);
        cPlotter->setFftRange(static_cast<float>(v), 0.f);
    });

    rxGroup = new QGroupBox("Receiver", this);
    rxGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid #0096c8; border-radius: 5px; margin-top: 0.5ex; } "
                           "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; padding: 0 8px; color: #00ffcc; }");

    QVBoxLayout *rxLayout = new QVBoxLayout(rxGroup);
    rxLayout->setSpacing(2);
    rxLayout->setContentsMargins(4, 14, 4, 4);

    // Top: Meter + Stereo + FreqCtrl
    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->addWidget(cMeter, 3);

    m_stereoLabel = new QLabel("", rxGroup);
    m_stereoLabel->setAlignment(Qt::AlignCenter);
    m_stereoLabel->setMinimumWidth(70);
    m_stereoLabel->setCursor(Qt::PointingHandCursor);
    m_stereoLabel->setStyleSheet("QLabel { font-weight: bold; font-size: 16px; color: #666666; }");
    m_stereoLabel->installEventFilter(this);
    topLayout->addWidget(m_stereoLabel, 0);
    topLayout->addWidget(freqCtrl, 7);
    rxLayout->addLayout(topLayout);

    // Plotter + gain slider
    QHBoxLayout *plotterLayout = new QHBoxLayout();
    plotterLayout->setSpacing(2);
    plotterLayout->addWidget(cPlotter, 1);
    plotterLayout->addWidget(plotterGainSlider, 0);
    rxLayout->addLayout(plotterLayout, 1);

    // Controls grid
    QGridLayout *cg = new QGridLayout();
    cg->setSpacing(4);
    cg->setContentsMargins(0, 2, 0, 0);
    QString rxls = "QLabel { color: #c8f0ff; font-size: 11px; font-weight: bold; }";

    // Row 0: Vol LNA VGA Amp
    auto addSliderRow = [&](int row, int col, const QString& name, QSlider*& slider,
                            QLabel*& levelLabel, int minV, int maxV, int val) {
        QLabel *lbl = new QLabel(name, rxGroup);
        lbl->setStyleSheet(rxls);
        slider = new QSlider(Qt::Horizontal, rxGroup);
        slider->setRange(minV, maxV);
        slider->setValue(val);
        levelLabel = new QLabel(QString::number(val), rxGroup);
        levelLabel->setAlignment(Qt::AlignCenter);
        levelLabel->setFixedWidth(32);
        levelLabel->setStyleSheet(labelStyle);
        cg->addWidget(lbl, row, col);
        cg->addWidget(slider, row, col+1);
        cg->addWidget(levelLabel, row, col+2);
    };

    addSliderRow(0, 0, "Vol:", volumeSlider, volumeLevelLabel, 0, 100, m_volumeLevel);
    addSliderRow(0, 3, "LNA:", lnaSlider, lnaLevelLabel, 0, 40, m_lnaGain);
    addSliderRow(0, 6, "VGA:", vgaSlider, vgaLevelLabel, 0, 62, m_vgaGain);
    addSliderRow(0, 9, "Amp:", rxAmpSlider, rxAmpLevelLabel, 0, 11, m_rxAmpGain);

    connect(volumeSlider, &QSlider::valueChanged, [this](int v) {
        volumeLevelLabel->setText(QString::number(v));
        m_volumeLevel = v;
        if (audioOutput) audioOutput->setVolume(v);
        saveSettings();
    });
    connect(lnaSlider, &QSlider::valueChanged, [this](int v) {
        lnaLevelLabel->setText(QString::number(v));
        m_lnaGain = v;
        if (m_isProcessing && m_hackTvLib) m_hackTvLib->setLnaGain(v);
        if (m_tcpConnected) sendTcpCommand(QString("SET_LNA_GAIN:%1").arg(v));
        saveSettings();
    });
    connect(vgaSlider, &QSlider::valueChanged, [this](int v) {
        vgaLevelLabel->setText(QString::number(v));
        m_vgaGain = v;
        if (m_isProcessing && m_hackTvLib) m_hackTvLib->setVgaGain(v);
        if (m_tcpConnected) sendTcpCommand(QString("SET_VGA_GAIN:%1").arg(v));
        saveSettings();
    });
    connect(rxAmpSlider, &QSlider::valueChanged, [this](int v) {
        rxAmpLevelLabel->setText(QString::number(v));
        m_rxAmpGain = v;
        if (m_isProcessing && m_hackTvLib) m_hackTvLib->setRxAmpGain(v);
        if (m_tcpConnected) sendTcpCommand(QString("SET_RX_AMP_GAIN:%1").arg(v));
        saveSettings();
    });

    // Row 1: RX Gain, ModIdx, DeEmph
    {
        QLabel *lbl;
        lbl = new QLabel("Gain:", rxGroup); lbl->setStyleSheet(rxls);
        rxGainSlider = new QSlider(Qt::Horizontal, rxGroup);
        rxGainSlider->setRange(0, 100);
        rxGainSlider->setValue(static_cast<int>(rxGain * 10));
        rxGainLevelLabel = new QLabel(QString::number(rxGain, 'f', 1), rxGroup);
        rxGainLevelLabel->setAlignment(Qt::AlignCenter);
        rxGainLevelLabel->setFixedWidth(32);
        rxGainLevelLabel->setStyleSheet(labelStyle);
        cg->addWidget(lbl, 1, 0);
        cg->addWidget(rxGainSlider, 1, 1);
        cg->addWidget(rxGainLevelLabel, 1, 2);

        rxModIndexLabel = new QLabel("ModIdx:", rxGroup); rxModIndexLabel->setStyleSheet(rxls);
        rxModIndexSlider = new QSlider(Qt::Horizontal, rxGroup);
        rxModIndexSlider->setRange(1, 100);
        rxModIndexSlider->setValue(static_cast<int>(rxModIndex * 10));
        rxModIndexLevelLabel = new QLabel(QString::number(rxModIndex, 'f', 1), rxGroup);
        rxModIndexLevelLabel->setAlignment(Qt::AlignCenter);
        rxModIndexLevelLabel->setFixedWidth(32);
        rxModIndexLevelLabel->setStyleSheet(labelStyle);
        cg->addWidget(rxModIndexLabel, 1, 3);
        cg->addWidget(rxModIndexSlider, 1, 4);
        cg->addWidget(rxModIndexLevelLabel, 1, 5);

        rxDeemphLabel = new QLabel("DeEm:", rxGroup); rxDeemphLabel->setStyleSheet(rxls);
        rxDeemphSlider = new QSlider(Qt::Horizontal, rxGroup);
        rxDeemphSlider->setRange(0, 1000);
        rxDeemphSlider->setValue(rxDeemph);
        rxDeemphLevelLabel = new QLabel(rxDeemph == 0 ? "OFF" : QString("%1us").arg(rxDeemph), rxGroup);
        rxDeemphLevelLabel->setAlignment(Qt::AlignCenter);
        rxDeemphLevelLabel->setFixedWidth(32);
        rxDeemphLevelLabel->setStyleSheet(labelStyle);
        cg->addWidget(rxDeemphLabel, 1, 6);
        cg->addWidget(rxDeemphSlider, 1, 7);
        cg->addWidget(rxDeemphLevelLabel, 1, 8);
    }

    connect(rxGainSlider, &QSlider::valueChanged, [this](int v) {
        rxGain = v / 10.0f;
        rxGainLevelLabel->setText(QString::number(rxGain, 'f', 1));
        if (fmDemodulator) fmDemodulator->setOutputGain(rxGain);

        saveSettings();
    });
    connect(rxModIndexSlider, &QSlider::valueChanged, [this](int v) {
        rxModIndex = v / 10.0f;
        rxModIndexLevelLabel->setText(QString::number(rxModIndex, 'f', 1));
        if (fmDemodulator) fmDemodulator->setRxModIndex(rxModIndex);
        saveSettings();
    });
    connect(rxDeemphSlider, &QSlider::valueChanged, [this](int v) {
        rxDeemph = v;
        rxDeemphLevelLabel->setText(v == 0 ? "OFF" : QString("%1us").arg(v));
        if (fmDemodulator) fmDemodulator->setDeemphTau(static_cast<float>(v));
        saveSettings();
    });

    for (int c : {1,4,7,10}) cg->setColumnStretch(c, 1);
    rxLayout->addLayout(cg);
    mainLayout->addWidget(rxGroup, 1);
}

void MainWindow::addRadioTxGroup()
{
    radioTxGroup = new QGroupBox("Radio TX", this);
    radioTxGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid #cc5522; border-radius: 5px; margin-top: 0.5ex; } "
                                "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; padding: 0 8px; color: #ff8844; }");

    QVBoxLayout *lay = new QVBoxLayout(radioTxGroup);
    lay->setSpacing(4);
    lay->setContentsMargins(8, 14, 8, 6);

    // TX/RX indicator
    txRxIndicator = new QLabel("RX - Listening", radioTxGroup);
    txRxIndicator->setAlignment(Qt::AlignCenter);
    txRxIndicator->setMinimumHeight(32);
    txRxIndicator->setStyleSheet(
        "font-size: 16px; font-weight: bold; color: #00FF66; "
        "background-color: #1A3A1A; border: 2px solid #00FF66; border-radius: 8px; padding: 6px;");
    lay->addWidget(txRxIndicator);

    // TX params row
    QGridLayout *txGrid = new QGridLayout();
    txGrid->setSpacing(4);
    QString txls = "QLabel { color: #ffcc88; font-size: 11px; font-weight: bold; }";

    QLabel *l1 = new QLabel("TX Amp:", radioTxGroup); l1->setStyleSheet(txls);
    txAmplitudeSlider = new QSlider(Qt::Horizontal, radioTxGroup);
    txAmplitudeSlider->setRange(1, 100);
    txAmplitudeSlider->setValue(static_cast<int>(tx_amplitude * 100));
    txAmplitudeLevelLabel = new QLabel(QString::number(tx_amplitude, 'f', 2), radioTxGroup);
    txAmplitudeLevelLabel->setFixedWidth(40);
    txAmplitudeLevelLabel->setStyleSheet(labelStyle);
    txGrid->addWidget(l1, 0, 0); txGrid->addWidget(txAmplitudeSlider, 0, 1); txGrid->addWidget(txAmplitudeLevelLabel, 0, 2);

    QLabel *l2 = new QLabel("Mod Idx:", radioTxGroup); l2->setStyleSheet(txls);
    txModIndexSlider = new QSlider(Qt::Horizontal, radioTxGroup);
    txModIndexSlider->setRange(1, 500);
    txModIndexSlider->setValue(static_cast<int>(tx_modulation_index * 100));
    txModIndexLevelLabel = new QLabel(QString::number(tx_modulation_index, 'f', 2), radioTxGroup);
    txModIndexLevelLabel->setFixedWidth(40);
    txModIndexLevelLabel->setStyleSheet(labelStyle);
    txGrid->addWidget(l2, 0, 3); txGrid->addWidget(txModIndexSlider, 0, 4); txGrid->addWidget(txModIndexLevelLabel, 0, 5);

    QLabel *l3 = new QLabel("Power:", radioTxGroup); l3->setStyleSheet(txls);
    txPowerSlider = new QSlider(Qt::Horizontal, radioTxGroup);
    txPowerSlider->setRange(0, 47);
    txPowerSlider->setValue(m_txAmpGain);
    txPowerLevelLabel = new QLabel(QString::number(m_txAmpGain), radioTxGroup);
    txPowerLevelLabel->setFixedWidth(40);
    txPowerLevelLabel->setStyleSheet(labelStyle);
    txGrid->addWidget(l3, 0, 6); txGrid->addWidget(txPowerSlider, 0, 7); txGrid->addWidget(txPowerLevelLabel, 0, 8);

    txGrid->setColumnStretch(1, 1);
    txGrid->setColumnStretch(4, 1);
    txGrid->setColumnStretch(7, 1);
    lay->addLayout(txGrid);

    connect(txAmplitudeSlider, &QSlider::valueChanged, [this](int v) {
        tx_amplitude = v / 100.0f;
        txAmplitudeLevelLabel->setText(QString::number(tx_amplitude, 'f', 2));
        if (m_isProcessing && m_hackTvLib) m_hackTvLib->setAmplitude(tx_amplitude);
        saveSettings();
    });
    connect(txModIndexSlider, &QSlider::valueChanged, [this](int v) {
        tx_modulation_index = v / 100.0f;
        txModIndexLevelLabel->setText(QString::number(tx_modulation_index, 'f', 2));
        if (m_isProcessing && m_hackTvLib) m_hackTvLib->setModulation_index(tx_modulation_index);
        saveSettings();
    });
    connect(txPowerSlider, &QSlider::valueChanged, [this](int v) {
        m_txAmpGain = v;
        txPowerLevelLabel->setText(QString::number(v));
        if (m_isProcessing && m_hackTvLib) m_hackTvLib->setTxAmpGain(v);
        saveSettings();
    });

    // PTT button
    pttButton = new QPushButton("PTT\nHold to Talk", radioTxGroup);
    pttButton->setMinimumHeight(60);
    pttButton->setStyleSheet(
        "QPushButton { background-color: #1A3A1A; color: #FFFFFF; font-size: 18px; font-weight: bold; "
        "border: 3px solid #2A6A2A; border-radius: 12px; }"
        "QPushButton:pressed { background-color: #882222; border-color: #CC3333; color: #FFCCCC; }");
    connect(pttButton, &QPushButton::pressed, this, &MainWindow::onPttPressed);
    connect(pttButton, &QPushButton::released, this, &MainWindow::onPttReleased);
    lay->addWidget(pttButton);

    mainLayout->addWidget(radioTxGroup);
}

void MainWindow::addTvTxGroup()
{
    tvTxGroup = new QGroupBox("TV / File TX", this);
    tvTxGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid #5566aa; border-radius: 5px; margin-top: 0.5ex; } "
                             "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; padding: 0 8px; color: #8899cc; }");

    QVBoxLayout *lay = new QVBoxLayout(tvTxGroup);
    lay->setSpacing(4);
    lay->setContentsMargins(8, 14, 8, 6);

    // TV Mode
    QHBoxLayout *row1 = new QHBoxLayout();
    tvModeCombo = new QComboBox(tvTxGroup);
    for (auto& [name, val] : std::vector<std::pair<QString,QString>>{
             {"PAL-I","i"}, {"PAL-B/G","g"}, {"PAL-D/K","pal-d"}, {"PAL-FM","pal-fm"},
             {"PAL-N","pal-n"}, {"PAL-M","pal-m"}, {"SECAM-L","l"}, {"SECAM-D/K","d"},
             {"NTSC-M","m"}, {"NTSC-A","ntsc-a"}, {"CCIR-A","a"}}) {
        tvModeCombo->addItem(name, val);
    }
    tvModeCombo->setCurrentIndex(1); // PAL-B/G

    channelCombo = new QComboBox(tvTxGroup);
    populateChannelCombo();

    colorDisabled = new QCheckBox("No Color", tvTxGroup);
    connect(colorDisabled, &QCheckBox::stateChanged, [this]() { if (m_initDone) saveSettings(); });

    row1->addWidget(tvModeCombo, 2);
    row1->addWidget(channelCombo, 2);
    row1->addWidget(colorDisabled, 1);
    lay->addLayout(row1);

    // File input
    QHBoxLayout *row2 = new QHBoxLayout();
    inputFileEdit = new QLineEdit(tvTxGroup);
    inputFileEdit->setPlaceholderText("Select file...");
    chooseFileButton = new QPushButton("Browse", tvTxGroup);
    connect(chooseFileButton, &QPushButton::clicked, this, &MainWindow::chooseFile);
    ffmpegOptionsEdit = new QLineEdit(tvTxGroup);
    ffmpegOptionsEdit->setText("rtsp://192.168.2.249:554/stream1");
    ffmpegOptionsEdit->setVisible(false);

    row2->addWidget(inputFileEdit, 1);
    row2->addWidget(chooseFileButton);
    row2->addWidget(ffmpegOptionsEdit, 1);
    lay->addLayout(row2);

    mainLayout->addWidget(tvTxGroup);
}

// ============================================================
// Operating Mode Change — presets for NFM/WFM/AM/TV
// ============================================================

void MainWindow::onOperatingModeChanged(int index)
{
    m_opMode = operatingModeCombo->currentData().toInt();
    m_isRadioMode = (m_opMode <= MODE_FM_FILE);

    bool isRadio = (m_opMode >= MODE_NFM && m_opMode <= MODE_AM);
    bool isFmFile = (m_opMode == MODE_FM_FILE);
    bool isTvMode = (m_opMode >= MODE_TV_FILE);

    // Show/hide groups — TV modes hide RX (no spectrum needed)
    rxGroup->setVisible(!isTvMode);
    radioTxGroup->setVisible(isRadio || isFmFile);
    tvTxGroup->setVisible(isTvMode || isFmFile);

    // PTT only for radio modes
    pttButton->setVisible(isRadio);

    // TV/File specific visibility
    if (isTvMode) {
        tvModeCombo->setVisible(true);
        channelCombo->setVisible(true);
        colorDisabled->setVisible(true);
        inputFileEdit->setVisible(m_opMode == MODE_TV_FILE || m_opMode == MODE_FM_FILE);
        chooseFileButton->setVisible(m_opMode == MODE_TV_FILE || m_opMode == MODE_FM_FILE);
        ffmpegOptionsEdit->setVisible(m_opMode == MODE_TV_RTSP);
    } else if (isFmFile) {
        tvModeCombo->setVisible(false);
        channelCombo->setVisible(false);
        colorDisabled->setVisible(false);
        inputFileEdit->setVisible(true);
        chooseFileButton->setVisible(true);
        ffmpegOptionsEdit->setVisible(false);
    }

    // FM-specific RX controls: ModIdx and DeEmph only for NFM/WFM
    bool showFmControls = (m_opMode == MODE_NFM || m_opMode == MODE_WFM);
    rxModIndexLabel->setVisible(showFmControls);
    rxModIndexSlider->setVisible(showFmControls);
    rxModIndexLevelLabel->setVisible(showFmControls);
    rxDeemphLabel->setVisible(showFmControls);
    rxDeemphSlider->setVisible(showFmControls);
    rxDeemphLevelLabel->setVisible(showFmControls);

    // Stereo checkbox only relevant for WFM
    stereoEnabled->setVisible(m_opMode == MODE_WFM);

    // Stereo label
    if (m_opMode == MODE_AM) {
        m_stereoLabel->setText("AM");
        m_stereoLabel->setStyleSheet("QLabel { font-weight: bold; font-size: 18px; color: #FF9900; }");
    } else if (isTvMode) {
        m_stereoLabel->setText("");
    } else {
        m_stereoLabel->setText("");
        m_stereoLabel->setStyleSheet("QLabel { font-weight: bold; font-size: 18px; color: #666666; }");
    }

    applyModePresets();
    applyModeTheme();

    // Auto-restart if already running (mode change requires new demodulator)
    if (m_initDone && m_isProcessing.load()) {
        stopAll();
        QTimer::singleShot(200, this, [this]() { startRx(); });
    }

    if (m_initDone) saveSettings();
}

void MainWindow::applyModePresets()
{
    switch (m_opMode) {
    case MODE_NFM:
        m_rxBandwidth = 12500;
        lnaSlider->setValue(20); vgaSlider->setValue(20);
        rxGainSlider->setValue(30);       // 3.0
        rxModIndexSlider->setValue(15);   // 1.5
        rxDeemphSlider->setValue(50);     // 50µs pre-emphasis match
        txAmplitudeSlider->setValue(50);  // 0.50
        txModIndexSlider->setValue(40);   // 0.40
        sampleRateCombo->setCurrentIndex(0); // 2 MHz
        break;
    case MODE_WFM:
        m_rxBandwidth = 150000;
        lnaSlider->setValue(40); vgaSlider->setValue(20);
        rxGainSlider->setValue(30);       // 3.0
        rxModIndexSlider->setValue(15);   // 1.5
        rxDeemphSlider->setValue(50);     // 50µs pre-emphasis match
        txAmplitudeSlider->setValue(50);
        txModIndexSlider->setValue(40);
        sampleRateCombo->setCurrentIndex(0);
        break;
    case MODE_AM:
        m_rxBandwidth = 10500;
        lnaSlider->setValue(40); vgaSlider->setValue(20);
        rxGainSlider->setValue(30);       // 3.0
        rxModIndexSlider->setValue(10);
        rxDeemphSlider->setValue(0);
        txAmplitudeSlider->setValue(25);  // 0.25 for AM
        txModIndexSlider->setValue(85);   // 0.85
        sampleRateCombo->setCurrentIndex(0);
        break;
    case MODE_FM_FILE:
        m_rxBandwidth = 150000;
        txAmplitudeSlider->setValue(50);
        txModIndexSlider->setValue(40);
        sampleRateCombo->setCurrentIndex(0);
        break;
    default: // TV modes
        sampleRateCombo->setCurrentIndex(5); // 16 MHz
        break;
    }

    m_CutFreq = m_rxBandwidth;
    m_HiCutFreq = m_rxBandwidth;
    m_LowCutFreq = -m_rxBandwidth;
    cPlotter->setDemodRanges(-m_rxBandwidth * 2, -1000, 1000, m_rxBandwidth * 2, true);
    cPlotter->setHiLowCutFrequencies(m_LowCutFreq, m_HiCutFreq);

    // Update active demodulators if running
    if (m_isProcessing) {
        if (fmDemodulator) fmDemodulator->setBandwidth(m_rxBandwidth);
        if (amDemodulator) amDemodulator->setBandwidth(m_rxBandwidth);
    }
}

// ============================================================
// Mode-aware Theme
// ============================================================

void MainWindow::applyModeTheme()
{
    // Mode accent colors: NFM=green, WFM=blue, AM=orange, FM File=teal, TV=purple
    switch (m_opMode) {
    case MODE_NFM:
        m_modeAccentColor = "#00cc66";
        m_modeAccentDark  = "#0a3a1a";
        break;
    case MODE_WFM:
        m_modeAccentColor = "#3399ff";
        m_modeAccentDark  = "#0a1a3a";
        break;
    case MODE_AM:
        m_modeAccentColor = "#ff9933";
        m_modeAccentDark  = "#3a2a0a";
        break;
    case MODE_FM_FILE:
        m_modeAccentColor = "#00ccaa";
        m_modeAccentDark  = "#0a3a2a";
        break;
    default: // TV modes
        m_modeAccentColor = "#aa66ff";
        m_modeAccentDark  = "#2a0a3a";
        break;
    }

    // Device group
    deviceGroup->setStyleSheet(QString(
        "QGroupBox { font-weight: bold; border: 1px solid %1; border-radius: 5px; margin-top: 0.5ex; "
        "background-color: %2; } "
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; "
        "padding: 0 8px; color: %1; }")
        .arg(m_modeAccentColor, m_modeAccentDark));

    // RX group
    rxGroup->setStyleSheet(QString(
        "QGroupBox { font-weight: bold; border: 1px solid %1; border-radius: 5px; margin-top: 0.5ex; } "
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; "
        "padding: 0 8px; color: %1; }")
        .arg(m_modeAccentColor));

    // Radio TX group
    radioTxGroup->setStyleSheet(QString(
        "QGroupBox { font-weight: bold; border: 1px solid #cc5522; border-radius: 5px; margin-top: 0.5ex; } "
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; "
        "padding: 0 8px; color: #ff8844; }"));

    // TV TX group
    tvTxGroup->setStyleSheet(QString(
        "QGroupBox { font-weight: bold; border: 1px solid %1; border-radius: 5px; margin-top: 0.5ex; } "
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; "
        "padding: 0 8px; color: %1; }")
        .arg(m_modeAccentColor));

    // Start button color
    startStopButton->setStyleSheet(QString(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 %1, stop:1 %2); "
        "color: #ffffff; border: 1px solid %1; border-radius: 4px; padding: 6px 15px; "
        "font-weight: bold; font-size: 12px; }"
        "QPushButton:pressed { background: %2; }")
        .arg(m_modeAccentColor, m_modeAccentDark));

    // FreqCtrl digit color matches mode
    if (m_opMode == MODE_AM)
        freqCtrl->setDigitColor(QColor("#FFB833"));
    else if (m_opMode == MODE_NFM)
        freqCtrl->setDigitColor(QColor("#66FF99"));
    else if (m_opMode == MODE_WFM)
        freqCtrl->setDigitColor(QColor("#66CCFF"));
    else
        freqCtrl->setDigitColor(QColor("#FFC300"));

    // Plotter FFT fill color matches mode
    if (m_opMode == MODE_NFM)
        cPlotter->setFftPlotColor(QColor("#88FFAA"));
    else if (m_opMode == MODE_WFM)
        cPlotter->setFftPlotColor(QColor("#88CCFF"));
    else if (m_opMode == MODE_AM)
        cPlotter->setFftPlotColor(QColor("#FFCC88"));
    else
        cPlotter->setFftPlotColor(QColor("#CEECF5"));

    // Update slider handle colors to match mode
    QString sliderStyle = QString(
        "QSlider::groove:horizontal { border: 1px solid #30363d; height: 6px; "
        "background: #21262d; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: %1; width: 14px; "
        "margin: -4px 0; border-radius: 7px; }").arg(m_modeAccentColor);

    for (QSlider* s : {volumeSlider, lnaSlider, vgaSlider, rxAmpSlider,
                       rxGainSlider, rxModIndexSlider, rxDeemphSlider})
        s->setStyleSheet(sliderStyle);

    // TX sliders keep warm orange
    QString txSliderStyle =
        "QSlider::groove:horizontal { border: 1px solid #30363d; height: 6px; "
        "background: #21262d; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #ff8844; width: 14px; "
        "margin: -4px 0; border-radius: 7px; }";
    for (QSlider* s : {txAmplitudeSlider, txModIndexSlider, txPowerSlider})
        s->setStyleSheet(txSliderStyle);
}

// ============================================================
// START / STOP
// ============================================================

void MainWindow::onStartStopClicked()
{
    if (startStopButton->text() == "START") {
        startRx();
    } else {
        stopAll();
    }
}

void MainWindow::startRx()
{
    // TCP mode — connect to emulator directly, skip HackTvLib
    if (isTcpMode()) {
        startTcpRx();
        return;
    }

    // USB mode — use HackTvLib
    // Create HackTvLib
    if (m_hackTvLib) {
        m_hackTvLib->clearCallbacks();
        m_hackTvLib->stop();
        delete m_hackTvLib;
        m_hackTvLib = nullptr;
        QThread::msleep(500);
    }

    initializeHackTvLib();
    if (!m_hackTvLib) return;
    QThread::msleep(200);

    bool isTvTx = (m_opMode >= MODE_TV_FILE);
    bool isFmFileTx = (m_opMode == MODE_FM_FILE);

    QStringList args;
    if (isTvTx) {
        args = buildTvTxCommand();
    } else if (isFmFileTx) {
        // FM File TX: start in TX mode with fmtransmitter
        args = buildRxCommand();  // base args (freq, sample rate, device)
        // Override to TX mode
        for (int i = 0; i < args.size(); i++) {
            if (args[i] == "--rx-tx-mode" && i + 1 < args.size()) {
                args[i + 1] = "tx";
                break;
            }
        }
        if (!args.contains("fmtransmitter")) {
            args.append("fmtransmitter");
        }
    } else {
        args = buildRxCommand();
    }

    std::vector<std::string> stdArgs;
    for (const QString& a : args) stdArgs.push_back(a.toStdString());
    m_hackTvLib->setArguments(stdArgs);

    // Callbacks
    m_hackTvLib->setLogCallback([this](const std::string& msg) {
        if (!m_shuttingDown.load())
            QMetaObject::invokeMethod(this, [this, msg]() {
                pendingLogs.append(QString::fromStdString(msg));
            }, Qt::QueuedConnection);
    });

    if (!isTvTx && !isFmFileTx) {
        m_hackTvLib->setReceivedDataCallback([this](const int8_t* data, size_t len) {
            if (!m_isProcessing.load() || !data || len != 262144 || m_shuttingDown.load() || m_isTx) return;
            const int n = len / 2;
            auto sp = std::make_shared<std::vector<std::complex<float>>>(n);
            for (int i = 0; i < n; i++)
                (*sp)[i] = std::complex<float>(static_cast<int8_t>(data[i*2]) / 128.0f,
                                               static_cast<int8_t>(data[i*2+1]) / 128.0f);
            QtConcurrent::run(m_threadPool, [this, sp]() { processDemod(*sp); });
            QtConcurrent::run(m_threadPool, [this, sp]() { processFft(*sp); });
        });
    }

    if (!m_hackTvLib->start()) {
        qDebug() << "Failed to start HackTvLib";
        return;
    }

    // Apply params
    m_hackTvLib->setLnaGain(m_lnaGain);
    m_hackTvLib->setVgaGain(m_vgaGain);
    m_hackTvLib->setRxAmpGain(m_rxAmpGain);
    m_hackTvLib->setAmplitude(tx_amplitude);
    m_hackTvLib->setModulation_index(tx_modulation_index);
    m_hackTvLib->setTxAmpGain(m_txAmpGain);
    m_hackTvLib->setAmpEnable(ampEnabled->isChecked());

    // Set TX modulation type
    int txModType = (m_opMode == MODE_AM) ? 2 : (m_opMode == MODE_WFM) ? 1 : 0;
    m_hackTvLib->setTxModulationType(txModType);

    // Create demodulators for RX (not for TX modes)
    if (!isTvTx && !isFmFileTx) {
        amDemodulator.reset();
        fmDemodulator.reset();

        if (m_opMode == MODE_AM) {
            amDemodulator = std::make_unique<AMDemodulator>(
                static_cast<double>(m_sampleRate), static_cast<double>(m_rxBandwidth));

        } else {
            double fmBw = (m_opMode == MODE_WFM) ? 150000.0 : 12500.0;
            fmDemodulator = std::make_unique<FMDemodulator>(
                static_cast<double>(m_sampleRate), fmBw);
            fmDemodulator->setOutputGain(rxGain);
            fmDemodulator->setRxModIndex(rxModIndex);
            fmDemodulator->setDeemphTau(static_cast<float>(rxDeemph));
            fmDemodulator->setForceMono(m_forceMono);
            connect(fmDemodulator.get(), &FMDemodulator::stereoStatusChanged, this, [this](bool stereo) {
                if (m_forceMono) {
                    m_stereoLabel->setText("MONO");
                    m_stereoLabel->setStyleSheet("QLabel { font-weight: bold; font-size: 18px; color: #FF9900; }");
                } else {
                    m_stereoLabel->setText(stereo ? "STEREO" : "");
                    m_stereoLabel->setStyleSheet(stereo
                        ? "QLabel { font-weight: bold; font-size: 18px; color: #00FF66; }"
                        : "QLabel { font-weight: bold; font-size: 18px; color: #666666; }");
                }
            });
        }
    }

    // FM File TX: start file playback
    if (isFmFileTx) {
        QTimer::singleShot(1000, this, [this]() {
            if (!m_hackTvLib || !m_hackTvLib->isDeviceReady()) return;
            m_hackTvLib->enableExternalAudioRing();
            m_hackTvLib->setTxModulationType(0); // FM
            if (!inputFileEdit->text().isEmpty())
                startFilePlayback(inputFileEdit->text());
        });
    }

    cPlotter->setSampleRate(m_sampleRate);
    cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));
    cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));

    startStopButton->setText("STOP");
    m_isProcessing.store(true);
    m_isTx = false;

    txRxIndicator->setText("RX - Listening");
    txRxIndicator->setStyleSheet(
        "font-size: 16px; font-weight: bold; color: #00FF66; "
        "background-color: #1A3A1A; border: 2px solid #00FF66; border-radius: 8px; padding: 6px;");

    // Start mic capture for radio modes (mic stays ready)
    if (m_opMode >= MODE_NFM && m_opMode <= MODE_AM) {
        startMicCapture();
    }
}

void MainWindow::stopAll()
{
    m_isProcessing.store(false);
    m_isTx = false;
    stopMicCapture();
    stopFilePlayback();
    stopTcpRx();

    if (m_hackTvLib) {
        m_hackTvLib->clearCallbacks();
        m_hackTvLib->stop();
        delete m_hackTvLib;
        m_hackTvLib = nullptr;
    }

    fmDemodulator.reset();
    amDemodulator.reset();

    startStopButton->setText("START");
    txRxIndicator->setText("RX - Listening");
    txRxIndicator->setStyleSheet(
        "font-size: 16px; font-weight: bold; color: #00FF66; "
        "background-color: #1A3A1A; border: 2px solid #00FF66; border-radius: 8px; padding: 6px;");
}

// ============================================================
// PTT — Switch between RX and TX (radio modes only)
// ============================================================

void MainWindow::onPttPressed()
{
    if (!m_isProcessing || !m_hackTvLib || m_isTx) return;
    if (m_opMode < MODE_NFM || m_opMode > MODE_AM) return;

    qDebug() << "=== PTT PRESSED ===";
    m_isTx = true;

    // Stop current RX, reinitialize as TX
    m_hackTvLib->clearCallbacks();
    m_hackTvLib->stop();
    delete m_hackTvLib;
    m_hackTvLib = nullptr;
    QThread::msleep(150);

    initializeHackTvLib();
    if (!m_hackTvLib) { m_isTx = false; return; }

    // Build TX command
    std::string srStr = std::to_string(m_sampleRate);
    std::string freqStr = std::to_string(m_frequency);
    std::string devStr = outputCombo->currentData().toString().toStdString();
    std::vector<std::string> args = {
        "-o", devStr,
        "--rx-tx-mode", "tx",
        "-s", srStr,
        "-f", freqStr,
        "-a", "fmtransmitter"
    };
    if (devStr == "hackrftcp") {
        args.insert(args.end(), {"-D", tcpAddressEdit->text().trimmed().toStdString()});
    }

    m_hackTvLib->setArguments(args);
    m_hackTvLib->setLogCallback([this](const std::string& msg) {
        if (!m_shuttingDown.load())
            QMetaObject::invokeMethod(this, [this, msg]() {
                pendingLogs.append(QString::fromStdString(msg));
            }, Qt::QueuedConnection);
    });

    if (!m_hackTvLib->start()) {
        qDebug() << "Failed to start TX";
        m_isTx = false;
        return;
    }

    QThread::msleep(200);
    m_hackTvLib->enableExternalAudioRing();

    // Set TX params
    int txModType = (m_opMode == MODE_AM) ? 2 : (m_opMode == MODE_WFM) ? 1 : 0;
    m_hackTvLib->setTxModulationType(txModType);
    m_hackTvLib->setAmplitude(tx_amplitude);
    m_hackTvLib->setModulation_index(tx_modulation_index);
    m_hackTvLib->setTxAmpGain(m_txAmpGain);
    m_hackTvLib->setAmpEnable(ampEnabled->isChecked());

    // Pre-fill silence
    std::vector<float> silence(2205, 0.0f);
    m_hackTvLib->writeExternalAudio(silence.data(), silence.size());

    // Start mic feed to TX
    if (!m_micStarted) startMicCapture();

    // Start mic → TX ring buffer feed
    if (m_micFlushTimer) {
        m_micFlushTimer->disconnect();
        connect(m_micFlushTimer, &QTimer::timeout, this, [this]() {
            if (!m_micDevice || !m_hackTvLib || !m_isTx) return;
            QByteArray data = m_micDevice->readAll();
            if (data.isEmpty()) return;
            const float* p = reinterpret_cast<const float*>(data.constData());
            size_t n = data.size() / sizeof(float);
            if (n > 0) m_hackTvLib->writeExternalAudio(p, n);
        });
    }

    txRxIndicator->setText("TX - Transmitting");
    txRxIndicator->setStyleSheet(
        "font-size: 16px; font-weight: bold; color: #FF4444; "
        "background-color: #3A1A1A; border: 2px solid #FF4444; border-radius: 8px; padding: 6px;");

    qDebug() << "TX mode active - modType:" << txModType;
}

void MainWindow::onPttReleased()
{
    if (!m_isTx) return;

    qDebug() << "=== PTT RELEASED ===";
    m_isTx = false;

    // Stop TX, go back to RX
    if (m_hackTvLib) {
        m_hackTvLib->clearCallbacks();
        m_hackTvLib->stop();
        delete m_hackTvLib;
        m_hackTvLib = nullptr;
    }
    QThread::msleep(150);

    // Restart RX
    startRx();
}

// ============================================================
// Keyboard PTT
// ============================================================

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && !m_pttHeld) {
        m_pttHeld = true;
        onPttPressed();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && m_pttHeld) {
        m_pttHeld = false;
        onPttReleased();
        event->accept();
        return;
    }
    QMainWindow::keyReleaseEvent(event);
}

// ============================================================
// Build commands
// ============================================================

QStringList MainWindow::buildRxCommand()
{
    QStringList args;
    QString device = outputCombo->currentData().toString();
    args << "-o" << device;

    // For TCP devices, pass the address
    if (device == "hackrftcp") {
        args << "-D" << tcpAddressEdit->text().trimmed();
    }

    args << "--rx-tx-mode" << "rx";
    args << "-s" << QString::number(m_sampleRate);
    args << "-f" << QString::number(m_frequency);
    return args;
}

QStringList MainWindow::buildTvTxCommand()
{
    QStringList args;
    QString device = outputCombo->currentData().toString();
    args << "-o" << device;
    args << "--rx-tx-mode" << "tx";

    if (device == "hackrftcp") {
        args << "-D" << tcpAddressEdit->text().trimmed();
    }

    if (ampEnabled->isChecked()) args << "-a";
    if (colorDisabled->isChecked()) args << "--nocolour";
    args << "--repeat" << "--a2stereo" << "--filter" << "--acp";

    switch (m_opMode) {
    case MODE_FM_FILE:
        args << "fmtransmitter";
        break;
    case MODE_TV_FILE:
        if (!inputFileEdit->text().isEmpty())
            args << inputFileEdit->text();
        else
            args << "test";
        break;
    case MODE_TV_TEST:
        args << "test";
        break;
    case MODE_TV_RTSP:
        args << ("ffmpeg:" + ffmpegOptionsEdit->text());
        break;
    default:
        args << "test";
        break;
    }

    args << "-f" << QString::number(m_frequency);
    args << "-s" << QString::number(m_sampleRate);
    args << "-m" << tvModeCombo->currentData().toString();
    return args;
}

// ============================================================
// IQ Processing
// ============================================================

void MainWindow::processDemod(const std::vector<std::complex<float>>& samples)
{
    if (!audioOutput || m_isTx) return;
    try {
        std::vector<float> audio;
        if (m_opMode == MODE_AM && amDemodulator) {
            auto mono = amDemodulator->demodulate(samples);
            float amGain = rxGain;  // rxGain from slider

            // Debug
            static int dbgCnt = 0;
            if (++dbgCnt >= 100) {
                dbgCnt = 0;
                float maxRaw = 0.0f, maxGained = 0.0f;
                for (size_t i = 0; i < mono.size(); i++) {
                    float a = std::fabs(mono[i]);
                    if (a > maxRaw) maxRaw = a;
                    float g = std::fabs(mono[i] * amGain);
                    if (g > maxGained) maxGained = g;
                }                
            }

            // AM demod returns mono, duplicate to stereo with gain + soft clip
            audio.resize(mono.size() * 2);
            for (size_t i = 0; i < mono.size(); i++) {
                float s = mono[i] * amGain;
                // Soft clipper: linear below ±0.6, smooth tanh compression above
                if (s > 0.6f) s = 0.6f + 0.4f * std::tanh((s - 0.6f) * 2.0f);
                else if (s < -0.6f) s = -0.6f + 0.4f * std::tanh((s + 0.6f) * 2.0f);
                audio[i*2] = s;
                audio[i*2+1] = s;
            }
        } else if (fmDemodulator) {
            audio = fmDemodulator->demodulate(samples);
        }
        if (!audio.empty()) {
            // AM already has its own gain + soft clip, skip audioGain for AM
            if (m_opMode == MODE_AM) {
                audioOutput->enqueueAudio(std::move(audio));
            } else {
                for (auto& s : audio) {
                    s *= audioGain;
                    if (s > 0.6f) s = 0.6f + 0.4f * std::tanh((s - 0.6f) * 2.0f);
                    else if (s < -0.6f) s = -0.6f + 0.4f * std::tanh((s + 0.6f) * 2.0f);
                }
                audioOutput->enqueueAudio(std::move(audio));
            }
        }
    } catch (const std::exception& e) {
        qCritical() << "Demod error:" << e.what();
    }
}

void MainWindow::processFft(const std::vector<std::complex<float>>& samples)
{
    static QMutex fftMutex;
    QMutexLocker locker(&fftMutex);

    int fft_size = 2048;
    std::vector<float> fft_output(fft_size);
    float signal_level_dbfs;
    getFft(samples, fft_output, signal_level_dbfs, fft_size);

    QMetaObject::invokeMethod(cMeter, "setLevel", Qt::QueuedConnection, Q_ARG(float, signal_level_dbfs));

    if (m_fftUpdatePending.testAndSetAcquire(0, 1)) {
        float* fft_data = new float[fft_size];
        std::memcpy(fft_data, fft_output.data(), fft_size * sizeof(float));
        QMetaObject::invokeMethod(this, "updatePlotter", Qt::QueuedConnection,
                                  Q_ARG(float*, fft_data), Q_ARG(int, fft_size));
    }
}

void MainWindow::updatePlotter(float* fft_data, int size)
{
    m_fftUpdatePending.storeRelease(0);
    cPlotter->setNewFttData(fft_data, fft_data, size);
    delete[] fft_data;
}

// ============================================================
// Frequency / Filter / Sample Rate
// ============================================================

void MainWindow::onFreqCtrl_setFrequency(qint64 freq)
{
    m_frequency = freq;
    cPlotter->setCenterFreq(static_cast<quint64>(freq));
    if (m_isProcessing && m_hackTvLib) m_hackTvLib->setFrequency(freq);
    if (m_tcpConnected) sendTcpCommand(QString("SET_FREQ:%1").arg(freq));
    saveSettings();
}

void MainWindow::on_plotter_newDemodFreq(qint64 freq, qint64 delta)
{
    (void)delta;
    m_frequency = freq;
    cPlotter->setCenterFreq(static_cast<quint64>(freq));
    freqCtrl->setFrequency(freq);
    if (m_isProcessing && m_hackTvLib) m_hackTvLib->setFrequency(freq);
    if (m_tcpConnected) sendTcpCommand(QString("SET_FREQ:%1").arg(freq));
    saveSettings();
}

void MainWindow::on_plotter_newFilterFreq(int low, int high)
{
    m_LowCutFreq = low;
    m_HiCutFreq = high;
    m_CutFreq = std::abs(high);
    m_rxBandwidth = m_CutFreq;
    if (m_isProcessing && fmDemodulator) fmDemodulator->setBandwidth(m_CutFreq);
    if (m_isProcessing && amDemodulator) amDemodulator->setBandwidth(m_CutFreq);
    saveSettings();
}

void MainWindow::onSampleRateChanged(int index)
{
    (void)index;
    m_sampleRate = sampleRateCombo->currentData().toInt();
    cPlotter->setSampleRate(m_sampleRate);
    cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));
    if (m_isProcessing && m_hackTvLib) m_hackTvLib->setSampleRate(m_sampleRate);
    if (m_tcpConnected) sendTcpCommand(QString("SET_SAMPLE_RATE:%1").arg(m_sampleRate));
    if (fmDemodulator) fmDemodulator->setSampleRate(static_cast<double>(m_sampleRate));
    if (amDemodulator) amDemodulator->setSampleRate(static_cast<double>(m_sampleRate));
    saveSettings();
}

void MainWindow::onChannelChanged(int index)
{
    long long freq = channelCombo->itemData(index).toLongLong();
    m_frequency = freq;
    freqCtrl->setFrequency(freq);
    cPlotter->setCenterFreq(static_cast<quint64>(freq));
    if (m_isProcessing && m_hackTvLib) m_hackTvLib->setFrequency(freq);
    saveSettings();
}

void MainWindow::setCurrentSampleRate(int sr)
{
    int idx = sampleRateCombo->findData(sr);
    if (idx >= 0) sampleRateCombo->setCurrentIndex(idx);
}

// ============================================================
// HackTvLib Init
// ============================================================

void MainWindow::initializeHackTvLib()
{
    try {
        m_hackTvLib = new HackTvLib(this);
    } catch (...) {
        m_hackTvLib = nullptr;
    }
}

// ============================================================
// Mic Capture
// ============================================================

void MainWindow::startMicCapture()
{
    stopMicCapture();

    QAudioFormat fmt;
    fmt.setSampleRate(44100);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Float);

    QAudioDevice dev = QMediaDevices::defaultAudioInput();
    if (dev.isNull()) return;

    if (!dev.isFormatSupported(fmt)) {
        fmt = dev.preferredFormat();
        fmt.setChannelCount(1);
        fmt.setSampleFormat(QAudioFormat::Float);
    }

    m_micSource = new QAudioSource(dev, fmt, this);
    m_micSource->setBufferSize(4096);
    m_micDevice = m_micSource->start();
    if (!m_micDevice) { delete m_micSource; m_micSource = nullptr; return; }

    m_micFlushTimer = new QTimer(this);
    // Default: just drain mic buffer (actual TX feed is connected in onPttPressed)
    connect(m_micFlushTimer, &QTimer::timeout, this, [this]() {
        if (!m_micDevice) return;
        if (!m_isTx) { m_micDevice->readAll(); return; } // drain if not TX
        if (!m_hackTvLib) return;
        QByteArray data = m_micDevice->readAll();
        if (data.isEmpty()) return;
        const float* p = reinterpret_cast<const float*>(data.constData());
        size_t n = data.size() / sizeof(float);
        if (n > 0) m_hackTvLib->writeExternalAudio(p, n);
    });
    m_micFlushTimer->start(5);
    m_micStarted = true;
}

void MainWindow::stopMicCapture()
{
    if (m_micFlushTimer) { m_micFlushTimer->stop(); delete m_micFlushTimer; m_micFlushTimer = nullptr; }
    if (m_micSource) { m_micSource->stop(); delete m_micSource; m_micSource = nullptr; m_micDevice = nullptr; }
    m_micStarted = false;
}

// ============================================================
// File Playback (FM File TX)
// ============================================================

void MainWindow::startFilePlayback(const QString& filePath)
{
    stopFilePlayback();
    m_fileAudioData.clear();
    m_filePlayPos = 0;

    QString ext = filePath.section('.', -1).toLower();
    if (ext == "wav") {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) return;
        QByteArray header = file.read(44);
        if (header.size() < 44) { file.close(); return; }
        QByteArray pcm = file.readAll();
        file.close();

        int channels = *reinterpret_cast<const int16_t*>(header.constData() + 22);
        int sampleRate = *reinterpret_cast<const int32_t*>(header.constData() + 24);
        int bps = *reinterpret_cast<const int16_t*>(header.constData() + 34);

        if (bps == 16) {
            const int16_t* s = reinterpret_cast<const int16_t*>(pcm.constData());
            size_t total = pcm.size() / sizeof(int16_t);
            if (channels == 2) {
                m_fileAudioData.resize(total / 2);
                for (size_t i = 0; i < total / 2; i++)
                    m_fileAudioData[i] = (s[i*2] + s[i*2+1]) / (2.0f * 32768.0f);
            } else {
                m_fileAudioData.resize(total);
                for (size_t i = 0; i < total; i++)
                    m_fileAudioData[i] = s[i] / 32768.0f;
            }
            if (sampleRate != 44100 && sampleRate > 0) {
                size_t newLen = static_cast<size_t>(m_fileAudioData.size() * 44100.0 / sampleRate);
                std::vector<float> rs(newLen);
                double ratio = static_cast<double>(m_fileAudioData.size()) / newLen;
                for (size_t i = 0; i < newLen; i++) {
                    double pos = i * ratio;
                    size_t idx = static_cast<size_t>(pos);
                    double frac = pos - idx;
                    rs[i] = (idx + 1 < m_fileAudioData.size())
                                ? static_cast<float>(m_fileAudioData[idx] * (1.0 - frac) + m_fileAudioData[idx+1] * frac)
                                : m_fileAudioData[std::min(idx, m_fileAudioData.size()-1)];
                }
                m_fileAudioData = std::move(rs);
            }
        }
        startFilePlaybackTimer();
    } else {
        m_audioDecoder = new QAudioDecoder(this);
        QAudioFormat df;
        df.setSampleRate(44100);
        df.setChannelCount(1);
        df.setSampleFormat(QAudioFormat::Int16);
        m_audioDecoder->setAudioFormat(df);
        m_audioDecoder->setSource(QUrl::fromLocalFile(filePath));

        connect(m_audioDecoder, &QAudioDecoder::bufferReady, this, [this]() {
            QAudioBuffer buf = m_audioDecoder->read();
            if (!buf.isValid()) return;
            const int16_t* d = buf.constData<int16_t>();
            int n = buf.sampleCount();
            size_t old = m_fileAudioData.size();
            m_fileAudioData.resize(old + n);
            for (int i = 0; i < n; i++) m_fileAudioData[old+i] = d[i] / 32768.0f;
        });
        connect(m_audioDecoder, &QAudioDecoder::finished, this, [this]() {
            startFilePlaybackTimer();
            m_audioDecoder->deleteLater();
            m_audioDecoder = nullptr;
        });
        m_audioDecoder->start();
    }
}

void MainWindow::startFilePlaybackTimer()
{
    if (m_fileAudioData.empty()) return;
    m_filePlayPos = 0;
    m_filePlayTimer = new QTimer(this);
    connect(m_filePlayTimer, &QTimer::timeout, this, [this]() {
        if (!m_hackTvLib || m_fileAudioData.empty()) return;
        const size_t chunk = 882;
        size_t rem = m_fileAudioData.size() - m_filePlayPos;
        if (rem == 0) { m_filePlayPos = 0; rem = m_fileAudioData.size(); }
        size_t toSend = std::min(chunk, rem);
        m_hackTvLib->writeExternalAudio(m_fileAudioData.data() + m_filePlayPos, toSend);
        m_filePlayPos += toSend;
    });
    m_filePlayTimer->start(20);
}

void MainWindow::stopFilePlayback()
{
    if (m_filePlayTimer) { m_filePlayTimer->stop(); delete m_filePlayTimer; m_filePlayTimer = nullptr; }
    if (m_audioDecoder) { m_audioDecoder->stop(); m_audioDecoder->deleteLater(); m_audioDecoder = nullptr; }
    m_filePlayPos = 0;
}

// ============================================================
// Misc
// ============================================================

void MainWindow::chooseFile()
{
    bool isFm = (m_opMode == MODE_FM_FILE);
    if (isFm) {
        fileDialog->setNameFilter("Audio Files (*.wav *.mp3 *.flac *.ogg);;All Files (*)");
        fileDialog->setDirectory(QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    } else {
        fileDialog->setNameFilter("Video Files (*.flv *.mp4 *.mkv *.avi);;All Files (*)");
        fileDialog->setDirectory(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    }
    if (fileDialog->exec()) {
        QStringList sel = fileDialog->selectedFiles();
        if (!sel.isEmpty()) inputFileEdit->setText(sel.first());
    }
}

void MainWindow::populateChannelCombo()
{
    struct Ch { QString name; long long freq; };
    QVector<Ch> chs = {
        {"E2",48250000},{"E3",55250000},{"E4",62250000},
        {"E5",175250000},{"E6",182250000},{"E7",189250000},{"E8",196250000},
        {"E21",471250000},{"E22",479250000},{"E23",487250000},{"E24",495250000},
        {"E25",503250000},{"E30",543250000},{"E35",583250000},{"E39",615250000},
        {"E40",623250000},{"E45",663250000},{"E50",703250000},{"E55",743250000},
        {"E60",783250000},{"E65",823250000},{"E69",855250000}
    };
    for (auto& c : chs) channelCombo->addItem(c.name, c.freq);
    connect(channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onChannelChanged);
}

void MainWindow::hardReset()
{
    m_isProcessing.store(false);
    stopMicCapture();
    stopFilePlayback();
    if (m_hackTvLib) {
        m_hackTvLib->clearCallbacks();
        m_hackTvLib->hardReset();
        delete m_hackTvLib;
        m_hackTvLib = nullptr;
    } else {
        HackTvLib* tmp = new HackTvLib(this);
        tmp->hardReset();
        delete tmp;
    }
    startStopButton->setEnabled(false);
    QTimer::singleShot(3000, this, [this]() { startStopButton->setEnabled(true); });
    fmDemodulator.reset();
    amDemodulator.reset();
    startStopButton->setText("START");
    m_fftUpdatePending.storeRelease(0);
}

void MainWindow::updateLogDisplay()
{
    if (!pendingLogs.isEmpty()) {
        for (const QString& l : pendingLogs) logBrowser->append(l);
        pendingLogs.clear();
    }
}

void MainWindow::saveSettings()
{
    if (!m_initDone) return;
    QSettings s(m_sSettingsFile, QSettings::IniFormat);
    s.beginGroup("Rf");
    s.setValue("frequency", m_frequency);
    s.setValue("samplerate", m_sampleRate);
    s.setValue("opMode", m_opMode);
    s.setValue("tx_amplitude_i", static_cast<int>(tx_amplitude * 1000));
    s.setValue("tx_modulation_index_i", static_cast<int>(tx_modulation_index * 1000));
    s.setValue("m_volumeLevel", m_volumeLevel);
    s.setValue("m_txAmpGain", m_txAmpGain);
    s.setValue("m_rxAmpGain", m_rxAmpGain);
    s.setValue("m_lnaGain", m_lnaGain);
    s.setValue("m_vgaGain", m_vgaGain);
    s.setValue("audioGain_i", static_cast<int>(audioGain * 1000));
    s.setValue("rxGain_i", static_cast<int>(rxGain * 1000));
    s.setValue("rxModIndex_i", static_cast<int>(rxModIndex * 1000));
    s.setValue("rxDeemph", rxDeemph);
    s.setValue("rxBandwidth", m_rxBandwidth);
    s.setValue("ampEnabled", ampEnabled->isChecked());
    s.endGroup();
}

void MainWindow::loadSettings()
{
    QSettings s(m_sSettingsFile, QSettings::IniFormat);
    s.beginGroup("Rf");
    m_frequency = s.value("frequency", 145000000).toLongLong();
    m_sampleRate = s.value("samplerate", 2000000).toInt();
    m_opMode = s.value("opMode", 1).toInt();
    if (s.contains("tx_amplitude_i")) tx_amplitude = s.value("tx_amplitude_i").toInt() / 1000.0f;
    if (s.contains("tx_modulation_index_i")) tx_modulation_index = s.value("tx_modulation_index_i").toInt() / 1000.0f;
    m_volumeLevel = s.value("m_volumeLevel", 10).toInt();
    m_txAmpGain = s.value("m_txAmpGain", 47).toInt();
    m_rxAmpGain = s.value("m_rxAmpGain", 0).toInt();
    m_lnaGain = s.value("m_lnaGain", 20).toInt();
    m_vgaGain = s.value("m_vgaGain", 20).toInt();
    if (s.contains("audioGain_i")) audioGain = s.value("audioGain_i").toInt() / 1000.0f;
    if (s.contains("rxGain_i")) rxGain = s.value("rxGain_i").toInt() / 1000.0f;
    if (s.contains("rxModIndex_i")) rxModIndex = s.value("rxModIndex_i").toInt() / 1000.0f;
    rxDeemph = s.value("rxDeemph", 0).toInt();
    m_rxBandwidth = s.value("rxBandwidth", 12500).toInt();
    s.endGroup();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
    exitApp();
}

// ============================================================
// TCP Client (for HackRF TCP emulator)
// ============================================================

void MainWindow::sendTcpCommand(const QString& cmd)
{
    if (isRtlTcpMode()) {
        // RTL-SDR TCP: binary 5-byte commands on data socket
        if (!m_tcpDataSocket || m_tcpDataSocket->state() != QAbstractSocket::ConnectedState) return;
        QByteArray pkt(5, 0);
        QStringList parts = cmd.split(':');
        QString c = parts[0].toUpper();
        quint32 val = parts.size() > 1 ? parts[1].toUInt() : 0;
        quint8 cmdByte = 0;
        if (c == "SET_FREQ")         cmdByte = 0x01;
        else if (c == "SET_SAMPLE_RATE") cmdByte = 0x02;
        else if (c == "SET_LNA_GAIN" || c == "SET_VGA_GAIN") { cmdByte = 0x04; /* gain in tenths */ val *= 10; }
        else if (c == "SET_RX_AMP_GAIN") { cmdByte = 0x04; val *= 10; }
        else if (c == "SET_AMP_ENABLE")  cmdByte = 0x0F; // bias tee
        else return; // unsupported
        pkt[0] = static_cast<char>(cmdByte);
        pkt[1] = static_cast<char>((val >> 24) & 0xFF);
        pkt[2] = static_cast<char>((val >> 16) & 0xFF);
        pkt[3] = static_cast<char>((val >> 8) & 0xFF);
        pkt[4] = static_cast<char>(val & 0xFF);
        m_tcpDataSocket->write(pkt);
        m_tcpDataSocket->flush();
        return;
    }
    // HackRF TCP: text protocol on control socket
    if (!m_tcpCtrlSocket || m_tcpCtrlSocket->state() != QAbstractSocket::ConnectedState) return;
    m_tcpCtrlSocket->write((cmd + "\n").toUtf8());
    m_tcpCtrlSocket->flush();
}

void MainWindow::startTcpRx()
{
    stopTcpRx();

    QString addr = tcpAddressEdit->text().trimmed();
    if (addr.isEmpty()) addr = "127.0.0.1";
    bool rtlMode = isRtlTcpMode();

    if (rtlMode) {
        // ── RTL-SDR TCP: single port, binary protocol, uint8 IQ ──
        int port = 1234;
        if (addr.contains(':')) {
            QStringList parts = addr.split(':');
            addr = parts[0];
            port = parts[1].toInt();
        }

        qDebug() << "RTL-SDR TCP connecting to" << addr << "port" << port;

        m_tcpDataSocket = new QTcpSocket(this);
        m_tcpDataSocket->setReadBufferSize(1024 * 1024);
        m_tcpDataSocket->connectToHost(addr, port);
        if (!m_tcpDataSocket->waitForConnected(3000)) {
            qDebug() << "RTL-SDR TCP connect failed:" << m_tcpDataSocket->errorString();
            delete m_tcpDataSocket; m_tcpDataSocket = nullptr;
            return;
        }

        // Read and discard 12-byte dongle info header
        if (m_tcpDataSocket->waitForReadyRead(2000)) {
            QByteArray hdr = m_tcpDataSocket->read(12);
            qDebug() << "RTL-SDR header:" << hdr.left(4) << "size=" << hdr.size();
        }

        // IQ data handler — uint8 centered at 127
        connect(m_tcpDataSocket, &QTcpSocket::readyRead, this, [this]() {
            if (m_shuttingDown.load() || m_isTx) return;
            m_tcpBuffer.append(m_tcpDataSocket->readAll());

            const int chunkSize = 262144;
            while (m_tcpBuffer.size() >= chunkSize) {
                QByteArray chunk = m_tcpBuffer.left(chunkSize);
                m_tcpBuffer.remove(0, chunkSize);

                const uint8_t* data = reinterpret_cast<const uint8_t*>(chunk.constData());
                const int n = chunkSize / 2;
                auto sp = std::make_shared<std::vector<std::complex<float>>>(n);
                for (int i = 0; i < n; i++)
                    (*sp)[i] = std::complex<float>((data[i*2] - 127.5f) / 128.0f,
                                                    (data[i*2+1] - 127.5f) / 128.0f);

                QtConcurrent::run(m_threadPool, [this, sp]() { processDemod(*sp); });
                QtConcurrent::run(m_threadPool, [this, sp]() { processFft(*sp); });
            }
        });

        connect(m_tcpDataSocket, &QTcpSocket::disconnected, this, [this]() {
            qDebug() << "RTL-SDR TCP disconnected";
            if (m_isProcessing.load()) stopAll();
        });

        qDebug() << "RTL-SDR TCP connected - streaming IQ";

        // Send initial config via binary commands
        // (sendTcpCommand will use binary format since isRtlTcpMode)
        // We need m_tcpDataSocket set first, which it is now
        sendTcpCommand(QString("SET_FREQ:%1").arg(m_frequency));
        sendTcpCommand(QString("SET_SAMPLE_RATE:%1").arg(m_sampleRate));
        sendTcpCommand(QString("SET_LNA_GAIN:%1").arg(m_lnaGain));

    } else {
        // ── HackRF TCP: 3 ports, text protocol, int8 IQ ──
        int dataPort = 5000, ctrlPort = 5001, audioPort = 5002;
        if (addr.contains(':')) {
            QStringList parts = addr.split(':');
            addr = parts[0];
            dataPort = parts[1].toInt();
            ctrlPort = dataPort + 1;
            audioPort = dataPort + 2;
        }

        qDebug() << "HackRF TCP connecting to" << addr << "ports" << dataPort << ctrlPort << audioPort;

        // Control socket
        m_tcpCtrlSocket = new QTcpSocket(this);
        m_tcpCtrlSocket->connectToHost(addr, ctrlPort);
        if (!m_tcpCtrlSocket->waitForConnected(3000)) {
            qDebug() << "TCP control connect failed:" << m_tcpCtrlSocket->errorString();
            delete m_tcpCtrlSocket; m_tcpCtrlSocket = nullptr;
            return;
        }

        sendTcpCommand(QString("SET_FREQ:%1").arg(m_frequency));
        sendTcpCommand(QString("SET_SAMPLE_RATE:%1").arg(m_sampleRate));
        sendTcpCommand(QString("SET_LNA_GAIN:%1").arg(m_lnaGain));
        sendTcpCommand(QString("SET_VGA_GAIN:%1").arg(m_vgaGain));
        sendTcpCommand(QString("SET_RX_AMP_GAIN:%1").arg(m_rxAmpGain));
        sendTcpCommand(QString("SET_AMP_ENABLE:%1").arg(ampEnabled->isChecked() ? 1 : 0));
        sendTcpCommand("SWITCH_RX");

        // Audio socket
        m_tcpAudioSocket = new QTcpSocket(this);
        m_tcpAudioSocket->connectToHost(addr, audioPort);
        if (!m_tcpAudioSocket->waitForConnected(2000)) {
            delete m_tcpAudioSocket; m_tcpAudioSocket = nullptr;
        }

        // Data socket — int8 IQ
        m_tcpDataSocket = new QTcpSocket(this);
        m_tcpDataSocket->setReadBufferSize(1024 * 1024);
        connect(m_tcpDataSocket, &QTcpSocket::readyRead, this, [this]() {
            if (m_shuttingDown.load() || m_isTx) return;
            m_tcpBuffer.append(m_tcpDataSocket->readAll());

            const int chunkSize = 262144;
            while (m_tcpBuffer.size() >= chunkSize) {
                QByteArray chunk = m_tcpBuffer.left(chunkSize);
                m_tcpBuffer.remove(0, chunkSize);

                const int8_t* data = reinterpret_cast<const int8_t*>(chunk.constData());
                const int n = chunkSize / 2;
                auto sp = std::make_shared<std::vector<std::complex<float>>>(n);
                for (int i = 0; i < n; i++)
                    (*sp)[i] = std::complex<float>(data[i*2] / 128.0f, data[i*2+1] / 128.0f);

                QtConcurrent::run(m_threadPool, [this, sp]() { processDemod(*sp); });
                QtConcurrent::run(m_threadPool, [this, sp]() { processFft(*sp); });
            }
        });

        connect(m_tcpDataSocket, &QTcpSocket::disconnected, this, [this]() {
            if (m_isProcessing.load()) stopAll();
        });

        m_tcpDataSocket->connectToHost(addr, dataPort);
        if (!m_tcpDataSocket->waitForConnected(3000)) {
            qDebug() << "TCP data connect failed";
            stopTcpRx();
            return;
        }
        qDebug() << "HackRF TCP connected - streaming IQ";
    }

    // Create demodulators (common for both protocols)
    amDemodulator.reset();
    fmDemodulator.reset();
    if (m_opMode == MODE_AM) {
        amDemodulator = std::make_unique<AMDemodulator>(
            static_cast<double>(m_sampleRate), static_cast<double>(m_rxBandwidth));
    } else {
        double fmBw = (m_opMode == MODE_WFM) ? 150000.0 : 12500.0;
        fmDemodulator = std::make_unique<FMDemodulator>(
            static_cast<double>(m_sampleRate), fmBw);
        fmDemodulator->setOutputGain(rxGain);
        fmDemodulator->setRxModIndex(rxModIndex);
        fmDemodulator->setDeemphTau(static_cast<float>(rxDeemph));
        fmDemodulator->setForceMono(m_forceMono);
        connect(fmDemodulator.get(), &FMDemodulator::stereoStatusChanged, this, [this](bool stereo) {
            if (m_forceMono) {
                m_stereoLabel->setText("MONO");
                m_stereoLabel->setStyleSheet("QLabel { font-weight: bold; font-size: 18px; color: #FF9900; }");
            } else {
                m_stereoLabel->setText(stereo ? "STEREO" : "");
                m_stereoLabel->setStyleSheet(stereo
                    ? "QLabel { font-weight: bold; font-size: 18px; color: #00FF66; }"
                    : "QLabel { font-weight: bold; font-size: 18px; color: #666666; }");
            }
        });
    }

    cPlotter->setSampleRate(m_sampleRate);
    cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));
    cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));

    m_tcpConnected = true;
    m_isProcessing.store(true);
    m_isTx = false;
    startStopButton->setText("STOP");

    txRxIndicator->setText(rtlMode ? "RX - RTL TCP" : "RX - HackRF TCP");
    txRxIndicator->setStyleSheet(
        "font-size: 16px; font-weight: bold; color: #00FF66; "
        "background-color: #1A3A1A; border: 2px solid #00FF66; border-radius: 8px; padding: 6px;");

    if (m_opMode >= MODE_NFM && m_opMode <= MODE_AM) {
        startMicCapture();
    }
}

void MainWindow::stopTcpRx()
{
    m_tcpConnected = false;
    m_tcpBuffer.clear();

    if (m_tcpDataSocket) {
        m_tcpDataSocket->disconnect();
        m_tcpDataSocket->abort();
        delete m_tcpDataSocket;
        m_tcpDataSocket = nullptr;
    }
    if (m_tcpCtrlSocket) {
        m_tcpCtrlSocket->abort();
        delete m_tcpCtrlSocket;
        m_tcpCtrlSocket = nullptr;
    }
    if (m_tcpAudioSocket) {
        m_tcpAudioSocket->abort();
        delete m_tcpAudioSocket;
        m_tcpAudioSocket = nullptr;
    }
}

void MainWindow::exitApp()
{
    m_shuttingDown.store(true);
    m_isProcessing.store(false);
    stopMicCapture();
    stopFilePlayback();
    stopTcpRx();
    if (m_hackTvLib) {
        m_hackTvLib->clearCallbacks();
        m_hackTvLib->stop();
        delete m_hackTvLib;
        m_hackTvLib = nullptr;
        QThread::msleep(300);
    }
#ifdef Q_OS_WIN
    TerminateProcess(GetCurrentProcess(), 0);
#else
    QApplication::quit();
#endif
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_stereoLabel && event->type() == QEvent::MouseButtonPress) {
        if (m_opMode == MODE_AM) return true;
        // Toggle via checkbox (which triggers the stateChanged handler)
        stereoEnabled->setChecked(!stereoEnabled->isChecked());
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}
