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
    // iPhone 16 Pro: 393 x 852 pt logical resolution
    setMinimumSize(393, 750);
    resize(393, 852);

    setupUi();
    applyDarkStyle();

    // Create gain settings dialog (non-modal, reusable)
    m_gainDialog = new GainSettingsDialog(m_tcpClient, m_fmDemod, m_amDemod, this);

    // Sync RF Amp: settings dialog -> main screen button
    connect(m_gainDialog, &GainSettingsDialog::ampEnableChanged, [this](bool enabled) {
        m_rfAmpBtn->blockSignals(true);
        m_rfAmpBtn->setChecked(enabled);
        m_rfAmpBtn->setText(enabled ? "RF AMP: ON (+14dB)" : "RF AMP: OFF");
        m_rfAmpBtn->blockSignals(false);
    });

    loadSettings();

    connect(m_tcpClient, &TcpClient::connected, this, &RadioWindow::onConnected);
    connect(m_tcpClient, &TcpClient::disconnected, this, &RadioWindow::onDisconnected);
    connect(m_tcpClient, &TcpClient::connectionError, this, &RadioWindow::onConnectionError);
    connect(m_tcpClient, &TcpClient::controlResponseReceived, this, &RadioWindow::onControlResponse);
    connect(m_tcpClient, &TcpClient::iqDataReceived, this, &RadioWindow::onIqDataReceived);
    connect(m_audioCapture, &AudioCapture::audioDataReady, this, &RadioWindow::onAudioCaptured);
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
    QSettings s("MarenRobotics", "HackRfRadio");

    // Connection
    s.setValue("host", m_hostEdit->text());
    s.setValue("dataPort", m_dataPortSpin->value());
    s.setValue("controlPort", m_controlPortSpin->value());
    s.setValue("audioPort", m_audioPortSpin->value());

    // Frequency
    s.setValue("frequency", QVariant::fromValue(m_freqWidget->frequency()));
    s.setValue("bandPreset", m_bandPreset->currentIndex());

    // Modulation
    s.setValue("modulation", m_modulationCombo->currentIndex());

    // Volume & Squelch
    s.setValue("volume", m_volumeSlider->value());
    s.setValue("squelch", m_squelchSlider->value());

    // Gain & TX (from dialog)
    s.setValue("vgaGain", m_gainDialog->vgaGain());
    s.setValue("lnaGain", m_gainDialog->lnaGain());
    s.setValue("txGain", m_gainDialog->txGain());
    s.setValue("amplitude", m_gainDialog->amplitude());
    s.setValue("modIndex", m_gainDialog->modIndex());
    s.setValue("rxGain", m_gainDialog->rxGain());
    s.setValue("rxModIndex", m_gainDialog->rxModIndex());
    s.setValue("deemph", m_gainDialog->deemph());
    s.setValue("ampEnable", m_gainDialog->ampEnabled());

    // Window geometry
    s.setValue("geometry", saveGeometry());

    qDebug() << "Settings saved";
}

void RadioWindow::loadSettings()
{
    QSettings s("MarenRobotics", "HackRfRadio");

    if (!s.contains("host")) {
        qDebug() << "No saved settings, using defaults";
        return;
    }

    // Connection
    m_hostEdit->setText(s.value("host", "127.0.0.1").toString());
    m_dataPortSpin->setValue(s.value("dataPort", 5000).toInt());
    m_controlPortSpin->setValue(s.value("controlPort", 5001).toInt());
    m_audioPortSpin->setValue(s.value("audioPort", 5002).toInt());

    // Frequency
    uint64_t freq = s.value("frequency", 145000000ULL).toULongLong();
    m_freqWidget->setFrequency(freq);
    m_bandPreset->setCurrentIndex(s.value("bandPreset", 0).toInt());

    // Modulation
    m_modulationCombo->setCurrentIndex(s.value("modulation", 0).toInt());

    // Volume & Squelch
    m_volumeSlider->setValue(s.value("volume", 50).toInt());
    m_squelchSlider->setValue(s.value("squelch", 10).toInt());

    // Gain & TX (to dialog)
    m_gainDialog->setVgaGain(s.value("vgaGain", 20).toInt());
    m_gainDialog->setLnaGain(s.value("lnaGain", 40).toInt());
    m_gainDialog->setTxGain(s.value("txGain", 47).toInt());
    m_gainDialog->setAmplitude(s.value("amplitude", 50).toInt());
    m_gainDialog->setModIndex(s.value("modIndex", 40).toInt());
    m_gainDialog->setRxGain(s.value("rxGain", 20).toInt());
    m_gainDialog->setRxModIndex(s.value("rxModIndex", 10).toInt());
    m_gainDialog->setDeemph(s.value("deemph", 0).toInt());
    // NOTE: IF bandwidth is NOT restored from settings.
    // The modulation combo (set above) already triggered onModulationChanged
    // which sets the correct default IF BW for the selected mode.
    m_gainDialog->setAmpEnabled(s.value("ampEnable", false).toBool());
    m_rfAmpBtn->setChecked(s.value("ampEnable", false).toBool());

    // Window geometry
    if (s.contains("geometry"))
        restoreGeometry(s.value("geometry").toByteArray());

    // Apply local demodulator parameters directly
    // onModulationChanged (triggered by setCurrentIndex above) already set IF BW default.
    // RX gain must be applied AFTER bandwidth since setBandwidth->rebuildChain resets gain.
    float rxGain = m_gainDialog->rxGain() / 10.0f;
    m_fmDemod->setOutputGain(rxGain);
    m_fmDemod->setRxModIndex(m_gainDialog->rxModIndex() / 10.0f);
    m_fmDemod->setDeemphTau(static_cast<float>(m_gainDialog->deemph()));

    qDebug() << "Settings applied - freq:" << freq << "rxGain:" << rxGain
             << "rxModIdx:" << m_gainDialog->rxModIndex() / 10.0f
             << "deemph:" << m_gainDialog->deemph();
}

// ============================================================
// UI Setup - Touch-friendly iPhone 16 Pro layout
// ============================================================

void RadioWindow::setupUi()
{
    QWidget* central = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(10, 4, 10, 8);

    // ──────────────────────────────────────────────
    // TOP BAR: Connection status + Settings button
    // ──────────────────────────────────────────────
    QHBoxLayout* topBar = new QHBoxLayout();
    topBar->setSpacing(8);

    m_connectBtn = new QPushButton("Connect");
    m_connectBtn->setObjectName("connectBtn");
    m_connectBtn->setMinimumHeight(38);
    connect(m_connectBtn, &QPushButton::clicked, this, &RadioWindow::onConnectClicked);

    m_connectionStatus = new QLabel("Disconnected");
    m_connectionStatus->setObjectName("connStatus");
    m_connectionStatus->setAlignment(Qt::AlignCenter);
    m_connectionStatus->setStyleSheet("color: #FF4444; font-weight: bold; font-size: 13px;");

    m_settingsBtn = new QPushButton("Settings");
    m_settingsBtn->setObjectName("settingsBtn");
    m_settingsBtn->setMinimumHeight(38);
    connect(m_settingsBtn, &QPushButton::clicked, this, &RadioWindow::onSettingsClicked);

    topBar->addWidget(m_connectBtn, 1);
    topBar->addWidget(m_connectionStatus, 1);
    topBar->addWidget(m_settingsBtn, 1);
    mainLayout->addLayout(topBar);

    // Hidden connection fields (shown in settings or auto-used)
    // Keep them in the widget tree but not displayed
    m_hostEdit = new QLineEdit("127.0.0.1");
    m_hostEdit->setVisible(false);
    m_dataPortSpin = new QSpinBox(); m_dataPortSpin->setRange(1, 65535); m_dataPortSpin->setValue(5000);
    m_dataPortSpin->setVisible(false);
    m_controlPortSpin = new QSpinBox(); m_controlPortSpin->setRange(1, 65535); m_controlPortSpin->setValue(5001);
    m_controlPortSpin->setVisible(false);
    m_audioPortSpin = new QSpinBox(); m_audioPortSpin->setRange(1, 65535); m_audioPortSpin->setValue(5002);
    m_audioPortSpin->setVisible(false);

    // ──────────────────────────────────────────────
    // FREQUENCY DISPLAY - Large, prominent
    // ──────────────────────────────────────────────
    m_freqWidget = new FrequencyWidget();
    m_freqWidget->setMinimumHeight(130);
    connect(m_freqWidget, &FrequencyWidget::frequencyChanged, this, &RadioWindow::onFrequencyChanged);
    mainLayout->addWidget(m_freqWidget);

    // Band preset selector - touch-friendly
    m_bandPreset = new QComboBox();
    m_bandPreset->setMinimumHeight(44);
    m_bandPreset->addItem("VHF - 2m Amateur (144-148 MHz)", QVariant::fromValue(145000000ULL));
    m_bandPreset->addItem("VHF - Marine Ch16 (156.800 MHz)", QVariant::fromValue(156800000ULL));
    m_bandPreset->addItem("VHF - FM Broadcast (88-108 MHz)", QVariant::fromValue(100000000ULL));
    m_bandPreset->addItem("VHF - PMR446 (446 MHz)", QVariant::fromValue(446006250ULL));
    m_bandPreset->addItem("UHF - 70cm Amateur (430-440 MHz)", QVariant::fromValue(435000000ULL));
    m_bandPreset->addItem("UHF - FRS/GMRS (462 MHz)", QVariant::fromValue(462562500ULL));
    m_bandPreset->addItem("UHF - LPD433 (433 MHz)", QVariant::fromValue(433075000ULL));
    m_bandPreset->addItem("HF - CB 27 MHz", QVariant::fromValue(27005000ULL));
    m_bandPreset->addItem("Custom", QVariant::fromValue(0ULL));
    connect(m_bandPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioWindow::onBandPresetChanged);
    mainLayout->addWidget(m_bandPreset);

    // ──────────────────────────────────────────────
    // MODE selector row
    // ──────────────────────────────────────────────
    QHBoxLayout* modeRow = new QHBoxLayout();
    modeRow->setSpacing(10);
    m_modulationCombo = new QComboBox();
    m_modulationCombo->setMinimumHeight(44);
    m_modulationCombo->addItem("NFM (Narrow FM - 12.5 kHz)");
    m_modulationCombo->addItem("WFM (Wide FM - 150 kHz)");
    m_modulationCombo->addItem("AM (Amplitude Mod)");
    connect(m_modulationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioWindow::onModulationChanged);
    m_modeLabel = new QLabel("NFM");
    m_modeLabel->setObjectName("modeLabel");
    m_modeLabel->setAlignment(Qt::AlignCenter);
    m_modeLabel->setMinimumWidth(60);
    m_modeLabel->setStyleSheet("font-weight: bold; font-size: 18px; color: #00FF66;");
    modeRow->addWidget(m_modulationCombo, 1);
    modeRow->addWidget(m_modeLabel);

    // RF Amp toggle button
    m_rfAmpBtn = new QPushButton("RF AMP: OFF");
    m_rfAmpBtn->setObjectName("rfAmpBtn");
    m_rfAmpBtn->setCheckable(true);
    m_rfAmpBtn->setChecked(false);
    m_rfAmpBtn->setMinimumHeight(38);
    connect(m_rfAmpBtn, &QPushButton::toggled, [this](bool checked) {
        m_rfAmpBtn->setText(checked ? "RF AMP: ON (+14dB)" : "RF AMP: OFF");
        m_gainDialog->setAmpEnabled(checked);
        if (m_tcpClient->isConnected()) m_tcpClient->setAmpEnable(checked);
    });
    modeRow->addWidget(m_rfAmpBtn);

    mainLayout->addLayout(modeRow);

    // ──────────────────────────────────────────────
    // SIGNAL METER (CMeter bar) + SPECTRUM (CPlotter, no waterfall)
    // ──────────────────────────────────────────────
    m_cMeter = new CMeter(this);
    m_cMeter->setMinimumHeight(35);
    m_cMeter->setMaximumHeight(45);
    mainLayout->addWidget(m_cMeter);

    m_cPlotter = new CPlotter(this);
    m_cPlotter->setSampleRate(m_sampleRate);
    m_cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));
    m_cPlotter->setCenterFreq(100000000ULL);
    m_cPlotter->setFftRange(-110.0f, 0.0f);
    m_cPlotter->setPandapterRange(-110.f, 0.f);
    m_cPlotter->setPercent2DScreen(100); // spectrum only, no waterfall
    m_cPlotter->setFftFill(true);
    m_cPlotter->setFftPlotColor(QColor("#CEECF5"));
    m_cPlotter->setFreqUnits(1000);
    m_cPlotter->setFilterBoxEnabled(false);
    m_cPlotter->setCenterLineEnabled(false);
    m_cPlotter->setClickResolution(0);
    m_cPlotter->setFocusPolicy(Qt::NoFocus);
    m_cPlotter->setMouseTracking(false);
    m_cPlotter->setMinimumHeight(180);
    m_cPlotter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(m_cPlotter, 1);

    // ──────────────────────────────────────────────
    // Spacer above sliders
    // ──────────────────────────────────────────────
    mainLayout->addStretch(1);

    // ──────────────────────────────────────────────
    // VOLUME & SQUELCH - near PTT for quick access
    // ──────────────────────────────────────────────
    QGridLayout* sliderGrid = new QGridLayout();
    sliderGrid->setVerticalSpacing(4);
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

    // Squelch
    QLabel* sqIcon = new QLabel("SQ");
    sqIcon->setObjectName("sliderIcon");
    m_squelchSlider = new QSlider(Qt::Horizontal);
    m_squelchSlider->setRange(0, 100);
    m_squelchSlider->setValue(10);
    m_squelchSlider->setMinimumHeight(36);
    m_squelchLabel = new QLabel("10%");
    m_squelchLabel->setMinimumWidth(42);
    m_squelchLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_squelchLevel = 0.10f;
    connect(m_squelchSlider, &QSlider::valueChanged, this, &RadioWindow::onSquelchChanged);
    sliderGrid->addWidget(sqIcon, 1, 0);
    sliderGrid->addWidget(m_squelchSlider, 1, 1);
    sliderGrid->addWidget(m_squelchLabel, 1, 2);

    // IF Bandwidth (main screen)
    QLabel* bwIcon = new QLabel("BW");
    bwIcon->setObjectName("sliderIcon");
    m_mainIfBwSlider = new QSlider(Qt::Horizontal);
    m_mainIfBwSlider->setRange(1, 200);
    m_mainIfBwSlider->setValue(25);  // 12.5 kHz default
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
        m_fmDemod->setBandwidth(bw);
        m_amDemod->setBandwidth(bw);
        // Sync settings dialog slider
        m_gainDialog->setIfBandwidth(v);
    });
    sliderGrid->addWidget(bwIcon, 2, 0);
    sliderGrid->addWidget(m_mainIfBwSlider, 2, 1);
    sliderGrid->addWidget(m_mainIfBwLabel, 2, 2);

    sliderGrid->setColumnStretch(1, 1);
    mainLayout->addLayout(sliderGrid);

    // ──────────────────────────────────────────────
    // Spacer below sliders
    // ──────────────────────────────────────────────
    mainLayout->addStretch(1);

    // ──────────────────────────────────────────────
    // TX/RX STATUS INDICATOR
    // ──────────────────────────────────────────────
    m_txRxIndicator = new QLabel("RX - Listening");
    m_txRxIndicator->setObjectName("txRxIndicator");
    m_txRxIndicator->setAlignment(Qt::AlignCenter);
    m_txRxIndicator->setMinimumHeight(44);
    m_txRxIndicator->setStyleSheet(
        "font-size: 18px; font-weight: bold; color: #00FF66; "
        "background-color: #1A3A1A; border: 2px solid #00FF66; border-radius: 10px; padding: 8px;");
    mainLayout->addWidget(m_txRxIndicator);

    // ──────────────────────────────────────────────
    // PTT BUTTON - Large, dominant, bottom of screen
    // ──────────────────────────────────────────────
    m_pttButton = new QPushButton("PTT\nHold to Talk");
    m_pttButton->setObjectName("pttButton");
    m_pttButton->setMinimumHeight(110);
    m_pttButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_pttButton, &QPushButton::pressed, this, &RadioWindow::onPttPressed);
    connect(m_pttButton, &QPushButton::released, this, &RadioWindow::onPttReleased);
    mainLayout->addWidget(m_pttButton);

    setCentralWidget(central);
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

        QComboBox {
            background-color: #1A1A2E; color: #EEEEFF;
            border: 1px solid #334455; border-radius: 8px;
            padding: 8px 12px; font-size: 13px;
        }
        QComboBox::drop-down {
            border: none; width: 30px;
        }
        QComboBox QAbstractItemView {
            background-color: #1A1A2E; color: #EEEEFF;
            selection-background-color: #334466;
            border: 1px solid #445566;
        }

        QLineEdit, QSpinBox {
            background-color: #1A1A2E; color: #EEEEFF;
            border: 1px solid #334455; border-radius: 6px;
            padding: 6px 8px; font-size: 13px;
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

void RadioWindow::onSettingsClicked()
{
    m_gainDialog->resize(380, 500);
    m_gainDialog->show();
    m_gainDialog->raise();
    m_gainDialog->activateWindow();
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
    logMessage(QString("Connecting to %1...").arg(m_hostEdit->text()));
    m_tcpClient->connectToServer(m_hostEdit->text().trimmed(),
                                  m_dataPortSpin->value(), m_controlPortSpin->value(), m_audioPortSpin->value());
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
    m_tcpClient->setVgaGain(m_gainDialog->vgaGain());
    m_tcpClient->setLnaGain(m_gainDialog->lnaGain());
    m_tcpClient->setTxAmpGain(m_gainDialog->txGain());
    m_tcpClient->setAmplitude(m_gainDialog->amplitude() / 100.0f);
    m_tcpClient->setModulationIndex(m_gainDialog->modIndex() / 100.0f);
    m_tcpClient->setAmpEnable(m_gainDialog->ampEnabled());
    m_tcpClient->switchToRx();

    // Apply IF bandwidth from slider
    int bwVal = m_gainDialog->ifBandwidth();
    float bw;
    if (bwVal <= 25) bw = bwVal * 500.0f;
    else bw = 12500.0f + (bwVal - 25) * 1000.0f;
    m_fmDemod->setBandwidth(bw);
    m_amDemod->setBandwidth(bw);

    // Apply RX gain AFTER bandwidth (setBandwidth->rebuildChain may reset gain)
    float rxGain = m_gainDialog->rxGain() / 10.0f;
    m_fmDemod->setOutputGain(rxGain);
    m_fmDemod->setRxModIndex(m_gainDialog->rxModIndex() / 10.0f);
    m_fmDemod->setDeemphTau(static_cast<float>(m_gainDialog->deemph()));
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

    // ── FFT + spectrum display + signal meter ──
    int fftSize = 2048;
    if (static_cast<int>(n) >= fftSize) {
        std::vector<float> fft_output(fftSize);
        float signal_level_dbfs;
        getFft(samples, fft_output, signal_level_dbfs, fftSize);

        // Update CMeter with signal level
        m_cMeter->setLevel(signal_level_dbfs);

        // Update CPlotter with FFT data (frame-drop guard)
        if (m_fftUpdatePending.testAndSetAcquire(0, 1)) {
            float* fft_data = new float[fftSize];
            std::memcpy(fft_data, fft_output.data(), fftSize * sizeof(float));
            QMetaObject::invokeMethod(this, "updatePlotter",
                                      Qt::QueuedConnection,
                                      Q_ARG(float*, fft_data),
                                      Q_ARG(int, fftSize));
        }

        // Squelch level from signal power
        float minDbm = -100.0f;
        float maxDbm = 0.0f;
        float level = (signal_level_dbfs - minDbm) / (maxDbm - minDbm);
        m_lastSignalLevel = std::clamp(level, 0.0f, 1.0f);
    }

    std::vector<float> audio;
    switch (m_currentModulation) {
    case FM_NB: case FM_WB: audio = m_fmDemod->demodulate(samples); break;
    case AM: audio = m_amDemod->demodulate(samples); break;
    }

    if (!audio.empty()) {
        // Squelch based on IQ signal level (0.0-1.0 from FFT)
        // SQ slider 0-100% maps to signal level threshold
        // SQ=0: squelch OFF, all audio passes
        // SQ=50: only signals above 50% of meter scale pass
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

    m_tcpClient->switchToTx();

    // Send current TX params from dialog
    m_gainDialog->sendTxParams();

    // Calculate estimated TX power in dBm and show on meter
    // HackRF TX chain:
    //   Base output (0 gain, full amplitude): ~-40 dBm
    //   IF amplifier adds 0-47 dB
    //   Baseband amplitude scales signal: 20*log10(amplitude)
    //   Formula: txPower = -40 + IF_gain + 20*log10(amplitude)
    float ifGain = static_cast<float>(m_gainDialog->txGain());          // 0-47
    float amplitude = m_gainDialog->amplitude() / 100.0f;               // 0.01-1.0
    float ampDb = 20.0f * std::log10(std::max(amplitude, 0.01f));       // -40 to 0 dB
    float rfAmpDb = m_gainDialog->ampEnabled() ? 14.0f : 0.0f;         // RF amp: +14 dB
    float estimatedDbm = -40.0f + ifGain + ampDb + rfAmpDb;
    // Clamp to realistic HackRF range
    estimatedDbm = std::clamp(estimatedDbm, -60.0f, 15.0f);

    qDebug() << "TX estimated power:" << estimatedDbm << "dBm (IF=" << ifGain << "amp=" << amplitude << ")";

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

    // Restore RX params from dialog
    m_gainDialog->sendRxParams();

    m_txRxIndicator->setText("RX - Listening");
    m_txRxIndicator->setStyleSheet(
        "font-size: 18px; font-weight: bold; color: #00FF66; "
        "background-color: #1A3A1A; border: 2px solid #00FF66; border-radius: 10px; padding: 8px;");
    logMessage("PTT OFF");
}

void RadioWindow::onAudioCaptured(const std::vector<float>& samples)
{
    if (!m_isTx || !m_tcpClient->isConnected()) return;
    m_tcpClient->sendAudioData(samples.data(), samples.size());
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
// Frequency & Band
// ============================================================

void RadioWindow::onFrequencyChanged(uint64_t freq)
{
    if (m_tcpClient->isConnected()) m_tcpClient->setFrequency(freq);
    m_cPlotter->setCenterFreq(static_cast<quint64>(freq));
    logMessage(QString("Freq: %1 MHz").arg(freq / 1000000.0, 0, 'f', 3));
}

void RadioWindow::onBandPresetChanged(int index)
{
    uint64_t freq = m_bandPreset->itemData(index).toULongLong();
    if (freq > 0) {
        m_freqWidget->setFrequency(freq);
        QString name = m_bandPreset->currentText();
        if (name.contains("FM Broadcast")) m_modulationCombo->setCurrentIndex(1);
        else if (name.contains("CB")) m_modulationCombo->setCurrentIndex(2);
        else m_modulationCombo->setCurrentIndex(0);
    }
}

// ============================================================
// Modulation
// ============================================================

void RadioWindow::onModulationChanged(int index)
{
    switch (index) {
    case 0:
        m_currentModulation = FM_NB;
        m_fmDemod->setBandwidth(12500.0);
        m_gainDialog->setIfBandwidth(25);
        m_mainIfBwSlider->blockSignals(true);
        m_mainIfBwSlider->setValue(25);
        m_mainIfBwLabel->setText("12.5 kHz");
        m_mainIfBwSlider->blockSignals(false);
        m_modeLabel->setText("NFM");
        m_modeLabel->setStyleSheet("font-weight: bold; font-size: 18px; color: #00FF66;");
        break;
    case 1:
        m_currentModulation = FM_WB;
        m_fmDemod->setBandwidth(150000.0);
        m_gainDialog->setIfBandwidth(163);
        m_mainIfBwSlider->blockSignals(true);
        m_mainIfBwSlider->setValue(163);
        m_mainIfBwLabel->setText("150.0 kHz");
        m_mainIfBwSlider->blockSignals(false);
        m_modeLabel->setText("WFM");
        m_modeLabel->setStyleSheet("font-weight: bold; font-size: 18px; color: #FFAA00;");
        break;
    case 2:
        m_currentModulation = AM;
        m_amDemod->setBandwidth(10000.0);
        m_gainDialog->setIfBandwidth(20);
        m_mainIfBwSlider->blockSignals(true);
        m_mainIfBwSlider->setValue(20);
        m_mainIfBwLabel->setText("10.0 kHz");
        m_mainIfBwSlider->blockSignals(false);
        m_modeLabel->setText("AM");
        m_modeLabel->setStyleSheet("font-weight: bold; font-size: 18px; color: #FF6666;");
        break;
    }
    m_sampleRate = 2000000;
    if (m_tcpClient->isConnected()) m_tcpClient->setSampleRate(m_sampleRate);
    m_fmDemod->setSampleRate(m_sampleRate);
    m_amDemod->setSampleRate(m_sampleRate);

    // Re-apply RX gain after rebuildChain (setBandwidth/setSampleRate resets it)
    float rxGain = m_gainDialog->rxGain() / 10.0f;
    m_fmDemod->setOutputGain(rxGain);
    m_fmDemod->setRxModIndex(m_gainDialog->rxModIndex() / 10.0f);
    m_fmDemod->setDeemphTau(static_cast<float>(m_gainDialog->deemph()));
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
