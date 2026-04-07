#include "radiowindow.h"
#include "constants.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QSettings>
#include <QDebug>
#include <QScrollArea>
#include <QScroller>
#include <QFrame>
#include <cmath>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#include <QJniEnvironment>
#endif

RadioWindow::RadioWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tcpClient(new TcpClient(this))
    , m_audioCapture(new AudioCapture(this))
    , m_audioPlayback(new AudioPlayback(this))
    , m_fmDemod(new FMDemodulator(2000000.0, 12500.0, this))
    , m_amDemod(new AMDemodulator(2000000.0, 10000.0, this))
    , m_gainDialog(nullptr)
{
    setWindowTitle("HackRF Radio");
    setMinimumSize(320, 600);
    resize(393, 852);

    // Populate data tables before setupUi
    m_bwEntries = {
        {"2 MHz",    2000000},
        {"4 MHz",    4000000},
        {"8 MHz",    8000000},
        {"10 MHz",  10000000},
        {"12.5 MHz",12500000},
        {"16 MHz",  16000000},
        {"20 MHz",  20000000}
    };

    m_bandEntries = {
        {"2m (145 MHz)",   145000000ULL},
        {"Marine (156.8)", 156800000ULL},
        {"FM (100 MHz)",   100000000ULL},
        {"PMR446",         446006250ULL},
        {"70cm (435 MHz)", 435000000ULL},
        {"FRS (462 MHz)",  462562500ULL},
        {"LPD433",         433075000ULL},
        {"CB 27 MHz",      27005000ULL},
        {"Custom",         0ULL}
    };

    m_modEntries = {
        {"NFM 12.5k", "NFM", "#00FF66"},
        {"WFM 150k",  "WFM", "#FFAA00"},
        {"AM 10k",    "AM",  "#FF6666"}
    };

    setupUi();
    applyDarkStyle();

    // Create gain settings page (embedded widget, not dialog)
    m_gainDialog = new GainSettingsDialog(m_tcpClient, m_fmDemod, m_amDemod);
    m_stackedWidget->addWidget(m_gainDialog);  // index 1

    // Back button returns to main page
    connect(m_gainDialog, &GainSettingsDialog::backClicked, this, &RadioWindow::onSettingsBack);

    // Sync RF Amp: settings page -> main screen button
    connect(m_gainDialog, &GainSettingsDialog::ampEnableChanged, [this](bool enabled) {
        m_rfAmpBtn->blockSignals(true);
        m_rfAmpBtn->setChecked(enabled);
        m_rfAmpBtn->setText(enabled ? "AMP ON" : "AMP OFF");
        m_rfAmpBtn->blockSignals(false);
    });

    // Auto-save when any setting changes
    connect(m_gainDialog, &GainSettingsDialog::settingsChanged, this, &RadioWindow::saveSettings);

    loadSettings();

#ifdef Q_OS_ANDROID
    // Request microphone permission at runtime (required for Android 6+)
    QJniObject activity = QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative", "activity",
        "()Landroid/app/Activity;");
    if (activity.isValid()) {
        QJniObject permission = QJniObject::fromString("android.permission.RECORD_AUDIO");
        QJniObject permissions = QJniEnvironment().findClass("java/lang/String");
        QJniObject permArray = QJniObject::callStaticObjectMethod(
            "java/lang/reflect/Array", "newInstance",
            "(Ljava/lang/Class;I)Ljava/lang/Object;",
            QJniEnvironment().findClass("java/lang/String"), 1);
        QJniEnvironment env;
        jobjectArray arr = env->NewObjectArray(1, env->FindClass("java/lang/String"), nullptr);
        env->SetObjectArrayElement(arr, 0, permission.object<jstring>());
        activity.callMethod<void>("requestPermissions", "([Ljava/lang/String;I)V", arr, 1);
    }
#endif

    connect(m_tcpClient, &TcpClient::connected, this, &RadioWindow::onConnected);
    connect(m_tcpClient, &TcpClient::disconnected, this, &RadioWindow::onDisconnected);
    connect(m_tcpClient, &TcpClient::connectionError, this, &RadioWindow::onConnectionError);
    connect(m_tcpClient, &TcpClient::controlResponseReceived, this, &RadioWindow::onControlResponse);
    connect(m_tcpClient, &TcpClient::iqDataReceived, this, &RadioWindow::onIqDataReceived);
    connect(m_audioCapture, &AudioCapture::audioDataReady, this, &RadioWindow::onAudioCaptured);

    // Stereo indicator
    connect(m_fmDemod, &FMDemodulator::stereoStatusChanged, this, [this](bool stereo) {
        if (m_forceMono) {
            m_stereoLabel->setText("MONO");
            m_stereoLabel->setStyleSheet("font-weight: bold; font-size: 18px; color: #FF9900;");
        } else {
            m_stereoLabel->setText(stereo ? "STEREO" : "");
            m_stereoLabel->setStyleSheet(stereo
                ? "font-weight: bold; font-size: 18px; color: #00FF66;"
                : "font-weight: bold; font-size: 18px; color: #666666;");
        }
    });
}

RadioWindow::~RadioWindow()
{
    m_audioCapture->stop();
    m_audioPlayback->stop();
    m_tcpClient->disconnectFromServer();
}

void RadioWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

// ============================================================
// Settings Save/Load
// ============================================================

void RadioWindow::saveSettings()
{
    if (!m_gainDialog) return;
    QSettings s("MarenRobotics", "HackRfRadio");

    // Connection
    s.setValue("host", m_gainDialog->host());
    s.setValue("dataPort", m_gainDialog->dataPort());
    s.setValue("controlPort", m_gainDialog->controlPort());
    s.setValue("audioPort", m_gainDialog->audioPort());

    // Frequency
    s.setValue("frequency", QVariant::fromValue(m_freqWidget->frequency()));
    s.setValue("bandPreset", m_bandPresetIndex);

    // Modulation
    s.setValue("modulation", m_modulationIndex);
    s.setValue("bwIndex", m_bwIndex);

    // Volume & Squelch
    s.setValue("volume", m_volumeSlider->value());
    s.setValue("squelch", m_squelchSlider->value());

    // Gain & TX (from settings page)
    s.setValue("vgaGain", m_gainDialog->vgaGain());
    s.setValue("lnaGain", m_gainDialog->lnaGain());
    s.setValue("txGain", m_gainDialog->txGain());
    s.setValue("amplitude", m_gainDialog->amplitude());
    s.setValue("modIndex", m_gainDialog->modIndex());
    s.setValue("rxGain", m_gainDialog->rxGain());
    s.setValue("rxModIndex", m_gainDialog->rxModIndex());
    s.setValue("deemph", m_gainDialog->deemph());
    s.setValue("audioLpf", m_gainDialog->audioLpf());
    s.setValue("fmnr", m_gainDialog->fmnrEnabled());
    s.setValue("ampEnable", m_gainDialog->ampEnabled());

    // Window geometry
    s.setValue("geometry", saveGeometry());

    // Settings saved silently
}

void RadioWindow::loadSettings()
{
    QSettings s("MarenRobotics", "HackRfRadio");

    if (!s.contains("host")) {
        qDebug() << "No saved settings, using defaults";
        // Apply defaults
        onModulationChanged(0);
        return;
    }

    // Connection
    if (m_gainDialog) {
        m_gainDialog->setHost(s.value("host", "192.168.1.8").toString());
        m_gainDialog->setDataPort(s.value("dataPort", 5000).toInt());
        m_gainDialog->setControlPort(s.value("controlPort", 5001).toInt());
        m_gainDialog->setAudioPort(s.value("audioPort", 5002).toInt());
    }

    // Frequency
    uint64_t freq = s.value("frequency", 145000000ULL).toULongLong();
    m_freqWidget->setFrequency(freq);

    // Band preset
    m_bandPresetIndex = s.value("bandPreset", 0).toInt();
    if (m_bandPresetIndex >= 0 && m_bandPresetIndex < m_bandEntries.size())
        m_bandPresetBtn->setText(m_bandEntries[m_bandPresetIndex].label);

    // Bandwidth
    m_bwIndex = s.value("bwIndex", 0).toInt();
    if (m_bwIndex >= 0 && m_bwIndex < m_bwEntries.size()) {
        m_bwLabel->setText(m_bwEntries[m_bwIndex].label);
        m_sampleRate = m_bwEntries[m_bwIndex].rate;
    }

    // Volume & Squelch
    m_volumeSlider->setValue(s.value("volume", 50).toInt());
    m_squelchSlider->setValue(s.value("squelch", 10).toInt());

    // Gain & TX (to settings page)
    if (m_gainDialog) {
        m_gainDialog->blockSignals(true);
        m_gainDialog->setVgaGain(s.value("vgaGain", 30).toInt());
        m_gainDialog->setLnaGain(s.value("lnaGain", 40).toInt());
        m_gainDialog->setTxGain(s.value("txGain", 47).toInt());
        m_gainDialog->setAmplitude(s.value("amplitude", 50).toInt());
        m_gainDialog->setModIndex(s.value("modIndex", 40).toInt());
        m_gainDialog->setRxGain(s.value("rxGain", 10).toInt());
        m_gainDialog->setRxModIndex(s.value("rxModIndex", 30).toInt());
        m_gainDialog->setDeemph(s.value("deemph", 0).toInt());
        m_gainDialog->setAudioLpf(s.value("audioLpf", 50).toInt());
        m_gainDialog->setFmnrEnabled(s.value("fmnr", true).toBool());
        m_gainDialog->setAmpEnabled(s.value("ampEnable", false).toBool());
        m_gainDialog->blockSignals(false);
    }
    m_rfAmpBtn->setChecked(s.value("ampEnable", false).toBool());

    // Window geometry
    if (s.contains("geometry"))
        restoreGeometry(s.value("geometry").toByteArray());

    // Modulation (applies BW, demod params, mode label)
    m_modulationIndex = s.value("modulation", 0).toInt();
    onModulationChanged(m_modulationIndex);

    m_cPlotter->setSampleRate(m_sampleRate);
    m_cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));
    m_cPlotter->setCenterFreq(static_cast<quint64>(freq));

    // Apply volume and squelch
    m_audioPlayback->setVolume(m_volumeSlider->value() / 100.0f);
    m_volumeLabel->setText(QString("%1%").arg(m_volumeSlider->value()));
    m_squelchLevel = m_squelchSlider->value() / 100.0f;
    m_squelchLabel->setText(QString("%1%").arg(m_squelchSlider->value()));

    qDebug() << "Settings applied - freq:" << freq
             << "bw:" << m_sampleRate
             << "mod:" << m_modulationIndex;
}

// ============================================================
// UI Setup - Touch-friendly layout, NO QComboBox (crashes on Android)
// ============================================================

void RadioWindow::setupUi()
{
    // QStackedWidget as central widget - page 0 = radio, page 1 = settings
    m_stackedWidget = new QStackedWidget(this);
    setCentralWidget(m_stackedWidget);

    // === PAGE 0: Main radio UI (fixed, no scroll) ===
    QWidget* mainPage = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(mainPage);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(8, 4, 8, 4);

    // ──────────────────────────────────────────────
    // TOP BAR: Connection status + Settings button
    // ──────────────────────────────────────────────
    QHBoxLayout* topBar = new QHBoxLayout();
    topBar->setSpacing(8);

    m_connectBtn = new QPushButton("Connect");
    m_connectBtn->setObjectName("connectBtn");
    m_connectBtn->setMinimumHeight(48);
    connect(m_connectBtn, &QPushButton::clicked, this, &RadioWindow::onConnectClicked);

    m_connectionStatus = new QLabel("Disconnected");
    m_connectionStatus->setObjectName("connStatus");
    m_connectionStatus->setAlignment(Qt::AlignCenter);
    m_connectionStatus->setStyleSheet("color: #FF4444; font-weight: bold; font-size: 14px;");

    m_settingsBtn = new QPushButton("Settings");
    m_settingsBtn->setObjectName("settingsBtn");
    m_settingsBtn->setMinimumHeight(48);
    connect(m_settingsBtn, &QPushButton::clicked, this, &RadioWindow::onSettingsClicked);

    topBar->addWidget(m_connectBtn, 1);
    topBar->addWidget(m_connectionStatus, 1);
    topBar->addWidget(m_settingsBtn, 1);
    mainLayout->addLayout(topBar);

    // ──────────────────────────────────────────────
    // FREQUENCY DISPLAY - Large, prominent
    // ──────────────────────────────────────────────
    m_freqWidget = new FrequencyWidget();
    m_freqWidget->setMinimumHeight(130);
    m_freqWidget->setMaximumHeight(150);
    connect(m_freqWidget, &FrequencyWidget::frequencyChanged, this, &RadioWindow::onFrequencyChanged);
    mainLayout->addWidget(m_freqWidget);

    // Band preset + BW selector - cycling buttons (no QComboBox)
    QHBoxLayout* bandBwRow = new QHBoxLayout();
    bandBwRow->setSpacing(6);

    m_bwMinusBtn = new QPushButton("-");
    m_bwMinusBtn->setObjectName("bwPmBtn");
    m_bwMinusBtn->setMinimumHeight(48);
    m_bwMinusBtn->setFixedWidth(48);
    connect(m_bwMinusBtn, &QPushButton::clicked, [this]() {
        if (m_bwIndex > 0) {
            m_bwIndex--;
            m_bwLabel->setText(m_bwEntries[m_bwIndex].label);
            onBwChanged(m_bwIndex);
        }
    });

    m_bwLabel = new QLabel(m_bwEntries[0].label);
    m_bwLabel->setAlignment(Qt::AlignCenter);
    m_bwLabel->setMinimumHeight(48);
    m_bwLabel->setStyleSheet("color: #EEEEFF; font-size: 14px; font-weight: bold; "
        "background-color: #1A1A2E; border: 1px solid #334455; border-radius: 8px; padding: 4px 8px;");

    m_bwPlusBtn = new QPushButton("+");
    m_bwPlusBtn->setObjectName("bwPmBtn");
    m_bwPlusBtn->setMinimumHeight(48);
    m_bwPlusBtn->setFixedWidth(48);
    connect(m_bwPlusBtn, &QPushButton::clicked, [this]() {
        if (m_bwIndex < m_bwEntries.size() - 1) {
            m_bwIndex++;
            m_bwLabel->setText(m_bwEntries[m_bwIndex].label);
            onBwChanged(m_bwIndex);
        }
    });

    bandBwRow->addWidget(m_bwMinusBtn, 0);
    bandBwRow->addWidget(m_bwLabel, 0);
    bandBwRow->addWidget(m_bwPlusBtn, 0);

    m_bandPresetBtn = new QPushButton(m_bandEntries[0].label);
    m_bandPresetBtn->setObjectName("cycleBtn");
    m_bandPresetBtn->setMinimumHeight(48);
    connect(m_bandPresetBtn, &QPushButton::clicked, [this]() {
        m_bandPresetIndex = (m_bandPresetIndex + 1) % m_bandEntries.size();
        m_bandPresetBtn->setText(m_bandEntries[m_bandPresetIndex].label);
        // Apply band frequency
        uint64_t freq = m_bandEntries[m_bandPresetIndex].freq;
        if (freq > 0) {
            m_freqWidget->setFrequency(freq);
            // Auto-select modulation based on band
            QString name = m_bandEntries[m_bandPresetIndex].label;
            if (name.contains("FM")) {
                m_modulationIndex = 1;
                onModulationChanged(1);
            } else if (name.contains("CB")) {
                m_modulationIndex = 2;
                onModulationChanged(2);
            } else {
                m_modulationIndex = 0;
                onModulationChanged(0);
            }
        }
    });

    bandBwRow->addWidget(m_bandPresetBtn, 1);
    mainLayout->addLayout(bandBwRow);

    // ──────────────────────────────────────────────
    // MODE selector row - cycling button (no QComboBox)
    // ──────────────────────────────────────────────
    QHBoxLayout* modeRow = new QHBoxLayout();
    modeRow->setSpacing(10);

    m_modulationBtn = new QPushButton(m_modEntries[0].label);
    m_modulationBtn->setObjectName("cycleBtn");
    m_modulationBtn->setMinimumHeight(48);
    connect(m_modulationBtn, &QPushButton::clicked, [this]() {
        m_modulationIndex = (m_modulationIndex + 1) % m_modEntries.size();
        onModulationChanged(m_modulationIndex);
    });
    modeRow->addWidget(m_modulationBtn, 1);

    m_modeLabel = new QLabel("NFM");
    m_modeLabel->setObjectName("modeLabel");
    m_modeLabel->setAlignment(Qt::AlignCenter);
    m_modeLabel->setMinimumWidth(60);
    m_modeLabel->setStyleSheet("font-weight: bold; font-size: 18px; color: #00FF66;");
    modeRow->addWidget(m_modeLabel);

    // Stereo indicator - clickable to toggle mono/stereo
    m_stereoLabel = new QLabel("");
    m_stereoLabel->setAlignment(Qt::AlignCenter);
    m_stereoLabel->setMinimumWidth(80);
    m_stereoLabel->setCursor(Qt::PointingHandCursor);
    m_stereoLabel->setStyleSheet("font-weight: bold; font-size: 18px; color: #666666;");
    m_stereoLabel->installEventFilter(this);
    modeRow->addWidget(m_stereoLabel);

    // RF Amp toggle button
    m_rfAmpBtn = new QPushButton("AMP OFF");
    m_rfAmpBtn->setObjectName("rfAmpBtn");
    m_rfAmpBtn->setCheckable(true);
    m_rfAmpBtn->setChecked(false);
    m_rfAmpBtn->setMinimumHeight(48);
    connect(m_rfAmpBtn, &QPushButton::toggled, [this](bool checked) {
        m_rfAmpBtn->setText(checked ? "AMP ON" : "AMP OFF");
        if (m_gainDialog) m_gainDialog->setAmpEnabled(checked);
        if (m_tcpClient->isConnected()) m_tcpClient->setAmpEnable(checked);
    });
    modeRow->addWidget(m_rfAmpBtn);

    mainLayout->addLayout(modeRow);
    mainLayout->addSpacing(6);

    // ──────────────────────────────────────────────
    // SIGNAL METER (CMeter bar) + SPECTRUM (CPlotter)
    // ──────────────────────────────────────────────
    m_cMeter = new CMeter(this);
    m_cMeter->setMinimumHeight(60);
    m_cMeter->setMaximumHeight(70);
    mainLayout->addWidget(m_cMeter);
    mainLayout->addSpacing(4);

    m_cPlotter = new CPlotter(this);
    m_cPlotter->setContentsMargins(0, 8, 8, 0);
    m_cPlotter->setSampleRate(m_sampleRate);
    m_cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));
    m_cPlotter->setCenterFreq(100000000ULL);
    m_cPlotter->setFftRange(-110.0f, 0.0f);
    m_cPlotter->setPandapterRange(-110.f, 0.f);
    m_cPlotter->setPercent2DScreen(100);
    m_cPlotter->setFftFill(true);
    m_cPlotter->setFftPlotColor(QColor("#CEECF5"));
    m_cPlotter->setFreqUnits(1000);
    m_cPlotter->setFilterBoxEnabled(false);
    m_cPlotter->setCenterLineEnabled(false);
    m_cPlotter->setClickResolution(0);
    m_cPlotter->setFocusPolicy(Qt::NoFocus);
    m_cPlotter->setMouseTracking(false);
    m_cPlotter->setMinimumHeight(120);
    m_cPlotter->setMaximumHeight(300);
    m_cPlotter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(m_cPlotter, 1);
    mainLayout->addSpacing(8);

    // ──────────────────────────────────────────────
    // VOLUME & IF BW sliders
    // ──────────────────────────────────────────────
    QGridLayout* sliderGrid = new QGridLayout();
    sliderGrid->setVerticalSpacing(6);
    sliderGrid->setHorizontalSpacing(8);

    // Volume
    QLabel* volIcon = new QLabel("VOL");
    volIcon->setObjectName("sliderIcon");
    m_volumeSlider = new QSlider(Qt::Horizontal);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(50);
    m_volumeSlider->setMinimumHeight(36);
    m_volumeLabel = new QLabel("50%");
    m_volumeLabel->setMinimumWidth(42);
    m_volumeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &RadioWindow::onVolumeChanged);
    sliderGrid->addWidget(volIcon, 0, 0);
    sliderGrid->addWidget(m_volumeSlider, 0, 1);
    sliderGrid->addWidget(m_volumeLabel, 0, 2);

    // Squelch - hidden, kept for compatibility
    m_squelchSlider = new QSlider(Qt::Horizontal);
    m_squelchSlider->setRange(0, 100);
    m_squelchSlider->setValue(0);
    m_squelchSlider->setVisible(false);
    m_squelchLabel = new QLabel("");
    m_squelchLabel->setVisible(false);
    m_squelchLevel = 0.0f;

    // IF Bandwidth
    QLabel* bwIcon = new QLabel("BW");
    bwIcon->setObjectName("sliderIcon");
    m_mainIfBwSlider = new QSlider(Qt::Horizontal);
    m_mainIfBwSlider->setRange(1, 200);
    m_mainIfBwSlider->setValue(25);
    m_mainIfBwSlider->setMinimumHeight(36);
    m_mainIfBwLabel = new QLabel("12.5 kHz");
    m_mainIfBwLabel->setMinimumWidth(60);
    m_mainIfBwLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_mainIfBwSlider, &QSlider::valueChanged, [this](int v) {
        float bw;
        if (v <= 25) bw = v * 500.0f;
        else bw = 12500.0f + (v - 25) * 1000.0f;
        if (bw >= 1000.0f)
            m_mainIfBwLabel->setText(QString("%1 kHz").arg(bw / 1000.0f, 0, 'f', 1));
        else
            m_mainIfBwLabel->setText(QString("%1 Hz").arg(static_cast<int>(bw)));
        if (m_gainDialog) m_gainDialog->setIfBandwidth(v);
    });
    connect(m_mainIfBwSlider, &QSlider::sliderReleased, [this]() {
        int v = m_mainIfBwSlider->value();
        float bw;
        if (v <= 25) bw = v * 500.0f;
        else bw = 12500.0f + (v - 25) * 1000.0f;
        m_fmDemod->setBandwidth(bw);
        m_amDemod->setBandwidth(bw);
    });
    sliderGrid->addWidget(bwIcon, 1, 0);
    sliderGrid->addWidget(m_mainIfBwSlider, 1, 1);
    sliderGrid->addWidget(m_mainIfBwLabel, 1, 2);

    sliderGrid->setColumnStretch(1, 1);
    mainLayout->addLayout(sliderGrid);
    mainLayout->addSpacing(6);

    // ──────────────────────────────────────────────
    // TX/RX STATUS INDICATOR
    // ──────────────────────────────────────────────
    m_txRxIndicator = new QLabel("RX - Listening");
    m_txRxIndicator->setObjectName("txRxIndicator");
    m_txRxIndicator->setAlignment(Qt::AlignCenter);
    m_txRxIndicator->setMinimumHeight(36);
    m_txRxIndicator->setMaximumHeight(40);
    m_txRxIndicator->setStyleSheet(
        "font-size: 18px; font-weight: bold; color: #00FF66; "
        "background-color: #1A3A1A; border: 2px solid #00FF66; border-radius: 10px; padding: 8px;");
    mainLayout->addWidget(m_txRxIndicator);

    // ──────────────────────────────────────────────
    // PTT BUTTON
    // ──────────────────────────────────────────────
    m_pttButton = new QPushButton("PTT\nHold to Talk");
    m_pttButton->setObjectName("pttButton");
    m_pttButton->setMinimumHeight(80);
    m_pttButton->setMaximumHeight(100);
    m_pttButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_pttButton, &QPushButton::pressed, this, &RadioWindow::onPttPressed);
    connect(m_pttButton, &QPushButton::released, this, &RadioWindow::onPttReleased);
    mainLayout->addWidget(m_pttButton);

    // Add main page as index 0
    m_stackedWidget->addWidget(mainPage);
}

void RadioWindow::applyDarkStyle()
{
    setStyleSheet(R"(
        QMainWindow { background-color: #0D0D1A; }

        QLabel { color: #CCDDEE; font-size: 13px; }

        QLabel#sliderIcon {
            color: #7799BB; font-weight: bold; font-size: 13px;
            min-width: 32px;
        }

        QLabel#txRxIndicator {
            font-size: 20px; font-weight: bold;
            border-radius: 10px; padding: 10px;
        }

        QSlider::groove:horizontal {
            border: none; height: 8px;
            background: #1A1A2E; border-radius: 4px;
        }
        QSlider::handle:horizontal {
            background: qradialgradient(cx:0.5, cy:0.5, radius:0.5,
                fx:0.4, fy:0.4, stop:0 #88BBFF, stop:1 #4488DD);
            width: 32px; height: 32px;
            margin: -12px 0; border-radius: 16px;
            border: 2px solid #335588;
        }
        QSlider::sub-page:horizontal {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #224466, stop:1 #3366AA);
            border-radius: 4px;
        }

        QPushButton#connectBtn {
            background-color: #1A3322; color: #44DD66;
            border: 1px solid #2A5533; border-radius: 8px;
            font-weight: bold; font-size: 13px; padding: 8px;
        }
        QPushButton#connectBtn:pressed { background-color: #2A5533; }

        QPushButton#settingsBtn {
            background-color: #1A1A33; color: #8899CC;
            border: 1px solid #333355; border-radius: 8px;
            font-weight: bold; font-size: 13px; padding: 8px;
        }
        QPushButton#settingsBtn:pressed { background-color: #2A2A44; }

        QPushButton#cycleBtn {
            background-color: #1A1A2E; color: #EEEEFF;
            border: 1px solid #334455; border-radius: 8px;
            padding: 8px 12px; font-size: 13px;
        }
        QPushButton#cycleBtn:pressed { background-color: #334466; }

        QPushButton#bwPmBtn {
            background-color: #1A2A44; color: #55BBFF;
            border: 1px solid #334466; border-radius: 8px;
            font-size: 20px; font-weight: bold;
        }
        QPushButton#bwPmBtn:pressed { background-color: #334466; }

        QPushButton#rfAmpBtn {
            background-color: #1A1A2A; color: #667788;
            border: 1px solid #333355; border-radius: 8px;
            font-weight: bold; font-size: 11px; padding: 6px 10px;
        }
        QPushButton#rfAmpBtn:checked {
            background-color: #3A1A0A; color: #FF8844;
            border: 1px solid #CC5522;
        }
        QPushButton#rfAmpBtn:pressed { background-color: #2A2A44; }

        QPushButton#pttButton {
            background-color: #1A3A1A;
            color: #FFFFFF;
            font-size: 22px; font-weight: bold;
            border: 3px solid #2A6A2A;
            border-radius: 16px;
        }
        QPushButton#pttButton:pressed {
            background-color: #882222;
            border-color: #CC3333;
            color: #FFCCCC;
        }
    )");
}

// ============================================================
// Settings page navigation
// ============================================================

void RadioWindow::onSettingsClicked()
{
    m_stackedWidget->setCurrentIndex(1);
}

void RadioWindow::onSettingsBack()
{
    m_stackedWidget->setCurrentIndex(0);
}

// ============================================================
// Connection
// ============================================================

void RadioWindow::onConnectClicked()
{
    if (m_tcpClient->isConnected()) {
        m_tcpClient->disconnectFromServer();
        return;
    }
    QString host = m_gainDialog ? m_gainDialog->host().trimmed() : "192.168.1.8";
    int dp = m_gainDialog ? m_gainDialog->dataPort() : 5000;
    int cp = m_gainDialog ? m_gainDialog->controlPort() : 5001;
    int ap = m_gainDialog ? m_gainDialog->audioPort() : 5002;
    logMessage(QString("Connecting to %1...").arg(host));
    m_tcpClient->connectToServer(host, dp, cp, ap);
}

void RadioWindow::onConnected()
{
    m_connectionStatus->setText("Connected");
    m_connectionStatus->setStyleSheet("color: #44FF44; font-weight: bold; font-size: 13px;");
    m_connectBtn->setText("Disconnect");
    logMessage("Connected to server");

    // Start audio devices
    m_audioPlayback->start();
    m_audioCapture->start();
    m_micStarted = true;

    // Send all current settings to server
    m_tcpClient->setFrequency(m_freqWidget->frequency());
    m_tcpClient->setSampleRate(m_sampleRate);
    if (m_gainDialog) {
        m_tcpClient->setVgaGain(m_gainDialog->vgaGain());
        m_tcpClient->setLnaGain(m_gainDialog->lnaGain());
        m_tcpClient->setTxAmpGain(m_gainDialog->txGain());
        m_tcpClient->setAmplitude(m_gainDialog->amplitude() / 100.0f);
        m_tcpClient->setModulationIndex(m_gainDialog->modIndex() / 100.0f);
        m_tcpClient->setAmpEnable(m_gainDialog->ampEnabled());
    }
    m_tcpClient->setModulationType(static_cast<int>(m_currentModulation));
    m_tcpClient->switchToRx();

    // Apply IF bandwidth from slider
    if (m_gainDialog) {
        int bwVal = m_gainDialog->ifBandwidth();
        float bw;
        if (bwVal <= 25) bw = bwVal * 500.0f;
        else bw = 12500.0f + (bwVal - 25) * 1000.0f;
        m_fmDemod->setBandwidth(bw);
        m_amDemod->setBandwidth(bw);

        float rxGain = m_gainDialog->rxGain() / 10.0f;
        m_fmDemod->setOutputGain(rxGain);
        m_fmDemod->setRxModIndex(m_gainDialog->rxModIndex() / 10.0f);
        m_fmDemod->setDeemphTau(static_cast<float>(m_gainDialog->deemph()));
        m_fmDemod->setAudioLPF(m_gainDialog->audioLpf() / 10.0f * 1000.0f);
    }
}

void RadioWindow::onDisconnected()
{
    m_connectionStatus->setText("Disconnected");
    m_connectionStatus->setStyleSheet("color: #FF4444; font-weight: bold; font-size: 13px;");
    m_connectBtn->setText("Connect");
    m_audioPlayback->stop();
    m_audioCapture->stop();
    m_micStarted = false;
    m_isTx = false;
    logMessage("Disconnected");
}

void RadioWindow::onConnectionError(const QString& error) { logMessage("Error: " + error); }
void RadioWindow::onControlResponse(const QString& response) { logMessage("Server: " + response); }

// ============================================================
// IQ Data (RX)
// ============================================================

void RadioWindow::onIqDataReceived(const QByteArray& data)
{
    if (m_isTx) return;
    m_iqAccumulator.append(data);
    while (m_iqAccumulator.size() >= IQ_PROCESS_THRESHOLD) processIqBuffer();
}

void RadioWindow::processIqBuffer()
{
    if (m_iqAccumulator.size() < IQ_PROCESS_THRESHOLD) return;

    QByteArray chunk = m_iqAccumulator.left(IQ_PROCESS_THRESHOLD);
    m_iqAccumulator.remove(0, IQ_PROCESS_THRESHOLD);

    const int8_t* iq = reinterpret_cast<const int8_t*>(chunk.constData());
    size_t n = chunk.size() / 2;
    std::vector<std::complex<float>> samples(n);

    for (size_t i = 0; i < n; i++) {
        samples[i] = std::complex<float>(iq[i*2] / 128.0f, iq[i*2+1] / 128.0f);
    }

    int fftSize = 2048;
    if (static_cast<int>(n) >= fftSize) {
        std::vector<float> fft_output(fftSize);
        float signal_level_dbfs;
        getFft(samples, fft_output, signal_level_dbfs, fftSize);

        m_cMeter->setLevel(signal_level_dbfs);

        if (m_fftUpdatePending.testAndSetAcquire(0, 1)) {
            float* fft_data = new float[fftSize];
            std::memcpy(fft_data, fft_output.data(), fftSize * sizeof(float));
            QMetaObject::invokeMethod(this, "updatePlotter",
                                      Qt::QueuedConnection,
                                      Q_ARG(float*, fft_data),
                                      Q_ARG(int, fftSize));
        }

        float minDbm = -100.0f;
        float maxDbm = 0.0f;
        float level = (signal_level_dbfs - minDbm) / (maxDbm - minDbm);
        m_lastSignalLevel = std::clamp(level, 0.0f, 1.0f);
    }

    std::vector<float> audio;
    switch (m_currentModulation) {
    case FM_NB: case FM_WB:
        audio = m_fmDemod->demodulate(samples);
        break;
    case AM: {
        auto mono = m_amDemod->demodulate(samples);
        float amGain = m_gainDialog ? (m_gainDialog->rxGain() / 10.0f) : 1.0f;
        audio.resize(mono.size() * 2);
        for (size_t i = 0; i < mono.size(); i++) {
            float s = mono[i] * amGain;
            if (s > 0.9f) s = 0.9f + 0.1f * std::tanh((s - 0.9f) * 8.0f);
            else if (s < -0.9f) s = -0.9f + 0.1f * std::tanh((s + 0.9f) * 8.0f);
            audio[i * 2]     = s;
            audio[i * 2 + 1] = s;
        }
        break;
    }
    }

    if (!audio.empty()) {
        if (m_squelchLevel <= 0.001f || m_lastSignalLevel >= m_squelchLevel) {
            m_audioPlayback->enqueueAudio(audio);
        }
    }
}

// ============================================================
// PTT
// ============================================================

void RadioWindow::onPttPressed()
{
    if (!m_tcpClient->isConnected() || m_isTx) return;

    qDebug() << "=== PTT PRESSED ===";
    m_isTx = true;
    m_iqAccumulator.clear();

    if (!m_micStarted) {
        m_audioCapture->start();
        m_micStarted = true;
    }

    m_tcpClient->setModulationType(static_cast<int>(m_currentModulation));
    m_tcpClient->switchToTx();

    if (m_gainDialog) m_gainDialog->sendTxParams();

    float ifGain = m_gainDialog ? static_cast<float>(m_gainDialog->txGain()) : 0.0f;
    float amplitude = m_gainDialog ? m_gainDialog->amplitude() / 100.0f : 0.5f;
    float ampDb = 20.0f * std::log10(std::max(amplitude, 0.01f));
    float rfAmpDb = (m_gainDialog && m_gainDialog->ampEnabled()) ? 14.0f : 0.0f;
    float estimatedDbm = -40.0f + ifGain + ampDb + rfAmpDb;
    estimatedDbm = std::clamp(estimatedDbm, -60.0f, 15.0f);

    qDebug() << "TX estimated power:" << estimatedDbm << "dBm";

    m_txRxIndicator->setText("TX - Transmitting");
    m_txRxIndicator->setStyleSheet(
        "font-size: 18px; font-weight: bold; color: #FF4444; "
        "background-color: #3A1A1A; border: 2px solid #FF4444; border-radius: 10px; padding: 8px;");
    logMessage("PTT ON");
}

void RadioWindow::onPttReleased()
{
    if (!m_isTx) return;

    qDebug() << "=== PTT RELEASED ===";
    m_isTx = false;

    m_tcpClient->switchToRx();

    if (m_gainDialog) m_gainDialog->sendRxParams();

    m_txRxIndicator->setText("RX - Listening");
    m_txRxIndicator->setStyleSheet(
        "font-size: 18px; font-weight: bold; color: #00FF66; "
        "background-color: #1A3A1A; border: 2px solid #00FF66; border-radius: 10px; padding: 8px;");
    logMessage("PTT OFF");
}

void RadioWindow::onAudioCaptured(const std::vector<float>& samples)
{
    if (!m_isTx || !m_tcpClient->isConnected()) return;

    // Always send audio immediately - never block TX
    m_tcpClient->sendAudioData(samples.data(), samples.size());

    // Accumulate mic samples for FFT display
    static std::vector<float> micFftBuf;
    static constexpr int MIC_FFT_SIZE = 1024;

    micFftBuf.insert(micFftBuf.end(), samples.begin(), samples.end());

    // Only do FFT when we have enough samples AND plotter is ready
    if (static_cast<int>(micFftBuf.size()) < MIC_FFT_SIZE) return;
    if (!m_fftUpdatePending.testAndSetAcquire(0, 1)) {
        // Plotter busy - keep only last MIC_FFT_SIZE samples to stay current
        if (static_cast<int>(micFftBuf.size()) > MIC_FFT_SIZE * 2)
            micFftBuf.erase(micFftBuf.begin(), micFftBuf.end() - MIC_FFT_SIZE);
        return;
    }

    // Convert real audio to complex
    std::vector<std::complex<float>> cplx(MIC_FFT_SIZE);
    for (int i = 0; i < MIC_FFT_SIZE; i++) {
        cplx[i] = std::complex<float>(micFftBuf[i], 0.0f);
    }
    micFftBuf.erase(micFftBuf.begin(), micFftBuf.begin() + MIC_FFT_SIZE);

    std::vector<float> fft_output(MIC_FFT_SIZE);
    float signal_level_dbfs;
    getFft(cplx, fft_output, signal_level_dbfs, MIC_FFT_SIZE);

    m_cMeter->setLevel(signal_level_dbfs);

    float* fft_data = new float[MIC_FFT_SIZE];
    std::memcpy(fft_data, fft_output.data(), MIC_FFT_SIZE * sizeof(float));
    QMetaObject::invokeMethod(this, "updatePlotter",
                              Qt::QueuedConnection,
                              Q_ARG(float*, fft_data),
                              Q_ARG(int, MIC_FFT_SIZE));
}

// ============================================================
// Keyboard PTT
// ============================================================

void RadioWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && !m_pttHeld) {
        m_pttHeld = true; onPttPressed(); event->accept(); return;
    }
    QMainWindow::keyPressEvent(event);
}

void RadioWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && m_pttHeld) {
        m_pttHeld = false; onPttReleased(); event->accept(); return;
    }
    QMainWindow::keyReleaseEvent(event);
}

// ============================================================
// Frequency
// ============================================================

void RadioWindow::onFrequencyChanged(uint64_t freq)
{
    if (m_tcpClient->isConnected()) m_tcpClient->setFrequency(freq);
    m_cPlotter->setCenterFreq(static_cast<quint64>(freq));
    logMessage(QString("Freq: %1 MHz").arg(freq / 1000000.0, 0, 'f', 3));
}

// ============================================================
// Modulation
// ============================================================

void RadioWindow::onModulationChanged(int index)
{
    if (index < 0 || index >= m_modEntries.size()) return;

    m_modulationIndex = index;
    m_modulationBtn->setText(m_modEntries[index].label);
    m_modeLabel->setText(m_modEntries[index].shortLabel);
    m_modeLabel->setStyleSheet(QString("font-weight: bold; font-size: 18px; color: %1;").arg(m_modEntries[index].color));

    switch (index) {
    case 0:
        m_currentModulation = FM_NB;
        m_fmDemod->setBandwidth(12500.0);
        if (m_gainDialog) {
            m_gainDialog->setIfBandwidth(25);
            // TX presets (NOT blocked — must reach server)
            m_gainDialog->setAmplitude(50);
            m_gainDialog->setModIndex(40);
            // RX presets (blocked — only local)
            m_gainDialog->blockSignals(true);
            m_gainDialog->setVgaGain(15);
            m_gainDialog->setLnaGain(20);
            m_gainDialog->setRxGain(30);       // 3.0
            m_gainDialog->setRxModIndex(10);   // 1.0 (deviation multiplier)
            m_gainDialog->setDeemph(0);        // OFF
            m_gainDialog->setAudioLpf(50);     // 5.0 kHz
            m_gainDialog->blockSignals(false);
            m_fmDemod->setOutputGain(3.0f);
            m_fmDemod->setRxModIndex(1.0f);
            m_fmDemod->setDeemphTau(0.0f);
            m_fmDemod->setAudioLPF(5000.0f);
        }
        m_mainIfBwSlider->blockSignals(true);
        m_mainIfBwSlider->setValue(25);
        m_mainIfBwLabel->setText("12.5 kHz");
        m_mainIfBwSlider->blockSignals(false);
        break;
    case 1:
        m_currentModulation = FM_WB;
        m_fmDemod->setBandwidth(150000.0);
        if (m_gainDialog) {
            m_gainDialog->setIfBandwidth(163);
            // TX presets (NOT blocked — must reach server)
            m_gainDialog->setAmplitude(50);
            m_gainDialog->setModIndex(40);
            // RX presets (blocked — only local)
            m_gainDialog->blockSignals(true);
            m_gainDialog->setVgaGain(20);
            m_gainDialog->setLnaGain(40);
            m_gainDialog->setRxGain(20);       // 2.0
            m_gainDialog->setRxModIndex(10);   // 1.0 (deviation multiplier)
            m_gainDialog->setDeemph(0);        // OFF
            m_gainDialog->setAudioLpf(50);     // 5.0 kHz
            m_gainDialog->blockSignals(false);
            m_fmDemod->setOutputGain(2.0f);
            m_fmDemod->setRxModIndex(1.0f);
            m_fmDemod->setDeemphTau(0.0f);
            m_fmDemod->setAudioLPF(5000.0f);
        }
        m_mainIfBwSlider->blockSignals(true);
        m_mainIfBwSlider->setValue(163);
        m_mainIfBwLabel->setText("150.0 kHz");
        m_mainIfBwSlider->blockSignals(false);
        break;
    case 2:
        m_currentModulation = AM;
        m_amDemod->setBandwidth(10000.0);
        if (m_gainDialog) {
            m_gainDialog->setIfBandwidth(20);
            // TX presets (NOT blocked — must reach server)
            m_gainDialog->setAmplitude(50);    // 0.50 carrier level
            m_gainDialog->setModIndex(85);     // 0.85 = %85 modulation depth
            // RX presets - AM
            m_gainDialog->blockSignals(true);
            m_gainDialog->setVgaGain(20);
            m_gainDialog->setLnaGain(30);
            m_gainDialog->setRxGain(10);       // 1.0
            m_gainDialog->setRxModIndex(10);   // 1.0
            m_gainDialog->setDeemph(0);        // OFF
            m_gainDialog->setAudioLpf(50);     // 5.0 kHz
            m_gainDialog->blockSignals(false);
        }
        m_mainIfBwSlider->blockSignals(true);
        m_mainIfBwSlider->setValue(20);
        m_mainIfBwLabel->setText("10.0 kHz");
        m_mainIfBwSlider->blockSignals(false);
        break;
    }

    m_sampleRate = m_bwEntries[m_bwIndex].rate;
    if (m_tcpClient->isConnected()) {
        m_tcpClient->setSampleRate(m_sampleRate);
        m_tcpClient->setModulationType(static_cast<int>(m_currentModulation));
    }
    m_fmDemod->setSampleRate(m_sampleRate);
    m_amDemod->setSampleRate(m_sampleRate);
    m_cPlotter->setSampleRate(m_sampleRate);
    m_cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));

    if (m_gainDialog) {
        float rxGain = m_gainDialog->rxGain() / 10.0f;
        m_fmDemod->setOutputGain(rxGain);
        m_fmDemod->setRxModIndex(m_gainDialog->rxModIndex() / 10.0f);
        m_fmDemod->setDeemphTau(static_cast<float>(m_gainDialog->deemph()));
        m_fmDemod->setAudioLPF(m_gainDialog->audioLpf() / 10.0f * 1000.0f);
    }

    saveSettings();
}

// ============================================================
// Volume & Squelch
// ============================================================

void RadioWindow::onVolumeChanged(int value)
{
    m_volumeLabel->setText(QString("%1%").arg(value));
    m_audioPlayback->setVolume(value / 100.0f);
}

void RadioWindow::onSquelchChanged(int value)
{
    m_squelchLabel->setText(QString("%1%").arg(value));
    m_squelchLevel = value / 100.0f;
}

// ============================================================
// Bandwidth
// ============================================================

void RadioWindow::onBwChanged(int index)
{
    Q_UNUSED(index);
    m_sampleRate = m_bwEntries[m_bwIndex].rate;

    if (m_tcpClient->isConnected())
        m_tcpClient->setSampleRate(m_sampleRate);

    m_fmDemod->setSampleRate(m_sampleRate);
    m_amDemod->setSampleRate(m_sampleRate);
    m_cPlotter->setSampleRate(m_sampleRate);
    m_cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));

    if (m_gainDialog) {
        float rxGain = m_gainDialog->rxGain() / 10.0f;
        m_fmDemod->setOutputGain(rxGain);
        m_fmDemod->setRxModIndex(m_gainDialog->rxModIndex() / 10.0f);
        m_fmDemod->setDeemphTau(static_cast<float>(m_gainDialog->deemph()));
        m_fmDemod->setAudioLPF(m_gainDialog->audioLpf() / 10.0f * 1000.0f);
    }

    saveSettings();
    qDebug() << "BW changed to" << m_sampleRate / 1e6 << "MHz";
}

// ============================================================
// Log
// ============================================================

void RadioWindow::logMessage(const QString& msg)
{
    qDebug() << "[Radio]" << msg;
}

void RadioWindow::updatePlotter(float* fft_data, int size)
{
    m_fftUpdatePending.storeRelease(0);
    m_cPlotter->setNewFttData(fft_data, size);
    delete[] fft_data;
}

bool RadioWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_stereoLabel && event->type() == QEvent::MouseButtonPress) {
        m_forceMono = !m_forceMono;
        m_fmDemod->setForceMono(m_forceMono);
        if (m_forceMono) {
            m_stereoLabel->setText("MONO");
            m_stereoLabel->setStyleSheet("font-weight: bold; font-size: 18px; color: #FF9900;");
        } else {
            bool stereo = m_fmDemod->isStereo();
            m_stereoLabel->setText(stereo ? "STEREO" : "");
            m_stereoLabel->setStyleSheet(stereo
                ? "font-weight: bold; font-size: 18px; color: #00FF66;"
                : "font-weight: bold; font-size: 18px; color: #666666;");
        }
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}
