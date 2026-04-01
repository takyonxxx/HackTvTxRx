#include "radiowindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QSettings>
#include <QDebug>
#include <cmath>

RadioWindow::RadioWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tcpClient(new TcpClient(this))
    , m_audioCapture(new AudioCapture(this))
    , m_audioPlayback(new AudioPlayback(this))
    , m_fmDemod(new FMDemodulator(2000000.0, 12500.0, this))
    , m_amDemod(new AMDemodulator(2000000.0, 10000.0, this))
{
    setWindowTitle("HackRF Radio - Walkie Talkie");
    setMinimumSize(520, 850);
    resize(540, 900);

    setupUi();
    applyDarkStyle();
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

    // Gain & TX
    s.setValue("volume", m_volumeSlider->value());
    s.setValue("squelch", m_squelchSlider->value());
    s.setValue("vgaGain", m_vgaGainSlider->value());
    s.setValue("lnaGain", m_lnaGainSlider->value());
    s.setValue("txGain", m_txGainSlider->value());
    s.setValue("amplitude", m_amplitudeSlider->value());
    s.setValue("modIndex", m_modIndexSlider->value());
    s.setValue("rxGain", m_rxGainSlider->value());
    s.setValue("deemph", m_deemphSlider->value());

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

    // Gain & TX
    m_volumeSlider->setValue(s.value("volume", 50).toInt());
    m_squelchSlider->setValue(s.value("squelch", 10).toInt());
    m_vgaGainSlider->setValue(s.value("vgaGain", 40).toInt());
    m_lnaGainSlider->setValue(s.value("lnaGain", 40).toInt());
    m_txGainSlider->setValue(s.value("txGain", 47).toInt());
    m_amplitudeSlider->setValue(s.value("amplitude", 10).toInt());
    m_modIndexSlider->setValue(s.value("modIndex", 40).toInt());
    m_rxGainSlider->setValue(s.value("rxGain", 45).toInt());
    m_deemphSlider->setValue(s.value("deemph", 750).toInt());

    // Window geometry
    if (s.contains("geometry"))
        restoreGeometry(s.value("geometry").toByteArray());

    qDebug() << "Settings loaded - freq:" << freq << "modIdx:" << m_modIndexSlider->value() / 100.0f;
}

// ============================================================
// UI Setup
// ============================================================

void RadioWindow::setupUi()
{
    QWidget* central = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    // === Connection ===
    QGroupBox* connGroup = new QGroupBox("Server Connection");
    QGridLayout* connGrid = new QGridLayout(connGroup);

    m_hostEdit = new QLineEdit("127.0.0.1");
    m_dataPortSpin = new QSpinBox(); m_dataPortSpin->setRange(1, 65535); m_dataPortSpin->setValue(5000);
    m_controlPortSpin = new QSpinBox(); m_controlPortSpin->setRange(1, 65535); m_controlPortSpin->setValue(5001);
    m_audioPortSpin = new QSpinBox(); m_audioPortSpin->setRange(1, 65535); m_audioPortSpin->setValue(5002);
    m_connectBtn = new QPushButton("Connect");
    connect(m_connectBtn, &QPushButton::clicked, this, &RadioWindow::onConnectClicked);
    m_connectionStatus = new QLabel("Disconnected");
    m_connectionStatus->setStyleSheet("color: #FF4444; font-weight: bold;");

    connGrid->addWidget(new QLabel("Host:"), 0, 0); connGrid->addWidget(m_hostEdit, 0, 1);
    connGrid->addWidget(new QLabel("Data:"), 0, 2); connGrid->addWidget(m_dataPortSpin, 0, 3);
    connGrid->addWidget(new QLabel("Ctrl:"), 1, 0); connGrid->addWidget(m_controlPortSpin, 1, 1);
    connGrid->addWidget(new QLabel("Audio:"), 1, 2); connGrid->addWidget(m_audioPortSpin, 1, 3);
    connGrid->addWidget(m_connectBtn, 2, 0, 1, 2); connGrid->addWidget(m_connectionStatus, 2, 2, 1, 2);
    mainLayout->addWidget(connGroup);

    // === Frequency ===
    QGroupBox* freqGroup = new QGroupBox("Frequency");
    QVBoxLayout* freqLayout = new QVBoxLayout(freqGroup);
    m_freqWidget = new FrequencyWidget();
    connect(m_freqWidget, &FrequencyWidget::frequencyChanged, this, &RadioWindow::onFrequencyChanged);
    m_bandPreset = new QComboBox();
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
    freqLayout->addWidget(m_freqWidget);
    QHBoxLayout* bandRow = new QHBoxLayout();
    bandRow->addWidget(new QLabel("Band:")); bandRow->addWidget(m_bandPreset, 1);
    freqLayout->addLayout(bandRow);
    mainLayout->addWidget(freqGroup);

    // === Modulation ===
    QGroupBox* modeGroup = new QGroupBox("Modulation");
    QHBoxLayout* modeLayout = new QHBoxLayout(modeGroup);
    m_modulationCombo = new QComboBox();
    m_modulationCombo->addItem("NFM (Narrow FM - 12.5 kHz)");
    m_modulationCombo->addItem("WFM (Wide FM - 150 kHz)");
    m_modulationCombo->addItem("AM (Amplitude Mod)");
    connect(m_modulationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioWindow::onModulationChanged);
    m_modeLabel = new QLabel("NFM");
    m_modeLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #00FF66;");
    modeLayout->addWidget(new QLabel("Mode:")); modeLayout->addWidget(m_modulationCombo, 1); modeLayout->addWidget(m_modeLabel);
    mainLayout->addWidget(modeGroup);

    // === Signal ===
    QGroupBox* sigGroup = new QGroupBox("Signal");
    QVBoxLayout* sigLayout = new QVBoxLayout(sigGroup);
    m_signalMeter = new SignalMeter();
    sigLayout->addWidget(m_signalMeter);
    mainLayout->addWidget(sigGroup);

    // === Gain & TX Params ===
    QGroupBox* gainGroup = new QGroupBox("Gain & TX Parameters");
    QGridLayout* g = new QGridLayout(gainGroup);
    int row = 0;

    m_volumeSlider = new QSlider(Qt::Horizontal); m_volumeSlider->setRange(0, 100); m_volumeSlider->setValue(50);
    m_volumeLabel = new QLabel("50%");
    connect(m_volumeSlider, &QSlider::valueChanged, this, &RadioWindow::onVolumeChanged);
    g->addWidget(new QLabel("Volume:"), row, 0); g->addWidget(m_volumeSlider, row, 1); g->addWidget(m_volumeLabel, row, 2); row++;

    m_squelchSlider = new QSlider(Qt::Horizontal); m_squelchSlider->setRange(0, 100); m_squelchSlider->setValue(10);
    m_squelchLabel = new QLabel("10%"); m_squelchLevel = 0.10f;
    connect(m_squelchSlider, &QSlider::valueChanged, this, &RadioWindow::onSquelchChanged);
    g->addWidget(new QLabel("Squelch:"), row, 0); g->addWidget(m_squelchSlider, row, 1); g->addWidget(m_squelchLabel, row, 2); row++;

    m_vgaGainSlider = new QSlider(Qt::Horizontal); m_vgaGainSlider->setRange(0, 62); m_vgaGainSlider->setValue(40);
    m_vgaGainLabel = new QLabel("40");
    connect(m_vgaGainSlider, &QSlider::valueChanged, [this](int v) {
        m_vgaGainLabel->setText(QString::number(v));
        if (m_tcpClient->isConnected()) m_tcpClient->setVgaGain(v);
    });
    g->addWidget(new QLabel("VGA (RX):"), row, 0); g->addWidget(m_vgaGainSlider, row, 1); g->addWidget(m_vgaGainLabel, row, 2); row++;

    m_lnaGainSlider = new QSlider(Qt::Horizontal); m_lnaGainSlider->setRange(0, 40); m_lnaGainSlider->setValue(40);
    m_lnaGainLabel = new QLabel("40");
    connect(m_lnaGainSlider, &QSlider::valueChanged, [this](int v) {
        m_lnaGainLabel->setText(QString::number(v));
        if (m_tcpClient->isConnected()) m_tcpClient->setLnaGain(v);
    });
    g->addWidget(new QLabel("LNA (RX):"), row, 0); g->addWidget(m_lnaGainSlider, row, 1); g->addWidget(m_lnaGainLabel, row, 2); row++;

    m_txGainSlider = new QSlider(Qt::Horizontal); m_txGainSlider->setRange(0, 47); m_txGainSlider->setValue(47);
    m_txGainLabel = new QLabel("47");
    connect(m_txGainSlider, &QSlider::valueChanged, [this](int v) {
        m_txGainLabel->setText(QString::number(v));
        if (m_tcpClient->isConnected()) m_tcpClient->setTxAmpGain(v);
    });
    g->addWidget(new QLabel("TX Power:"), row, 0); g->addWidget(m_txGainSlider, row, 1); g->addWidget(m_txGainLabel, row, 2); row++;

    m_amplitudeSlider = new QSlider(Qt::Horizontal); m_amplitudeSlider->setRange(1, 100); m_amplitudeSlider->setValue(10);
    m_amplitudeLabel = new QLabel("0.10");
    connect(m_amplitudeSlider, &QSlider::valueChanged, [this](int v) {
        float amp = v / 100.0f;
        m_amplitudeLabel->setText(QString::number(amp, 'f', 2));
        if (m_tcpClient->isConnected()) m_tcpClient->setAmplitude(amp);
    });
    g->addWidget(new QLabel("TX Amp:"), row, 0); g->addWidget(m_amplitudeSlider, row, 1); g->addWidget(m_amplitudeLabel, row, 2); row++;

    m_modIndexSlider = new QSlider(Qt::Horizontal); m_modIndexSlider->setRange(1, 500); m_modIndexSlider->setValue(40);
    m_modIndexLabel = new QLabel("0.40");
    connect(m_modIndexSlider, &QSlider::valueChanged, [this](int v) {
        float idx = v / 100.0f;
        m_modIndexLabel->setText(QString::number(idx, 'f', 2));
        if (m_tcpClient->isConnected()) m_tcpClient->setModulationIndex(idx);
    });
    g->addWidget(new QLabel("Mod Idx:"), row, 0); g->addWidget(m_modIndexSlider, row, 1); g->addWidget(m_modIndexLabel, row, 2); row++;

    // RX audio gain (0.1 - 10.0, slider 1-100, display /10)
    m_rxGainSlider = new QSlider(Qt::Horizontal); m_rxGainSlider->setRange(1, 100); m_rxGainSlider->setValue(45);
    m_rxGainLabel = new QLabel("4.5");
    connect(m_rxGainSlider, &QSlider::valueChanged, [this](int v) {
        float gain = v / 10.0f;
        m_rxGainLabel->setText(QString::number(gain, 'f', 1));
        m_fmDemod->setOutputGain(gain);
    });
    g->addWidget(new QLabel("RX Gain:"), row, 0); g->addWidget(m_rxGainSlider, row, 1); g->addWidget(m_rxGainLabel, row, 2); row++;

    // De-emphasis tau (0 - 1000 us, slider 0-1000, 0=off)
    m_deemphSlider = new QSlider(Qt::Horizontal); m_deemphSlider->setRange(0, 1000); m_deemphSlider->setValue(750);
    m_deemphLabel = new QLabel("750us");
    connect(m_deemphSlider, &QSlider::valueChanged, [this](int v) {
        if (v == 0) m_deemphLabel->setText("OFF");
        else m_deemphLabel->setText(QString("%1us").arg(v));
        m_fmDemod->setDeemphTau(static_cast<float>(v));
    });
    g->addWidget(new QLabel("DeEmph:"), row, 0); g->addWidget(m_deemphSlider, row, 1); g->addWidget(m_deemphLabel, row, 2); row++;

    mainLayout->addWidget(gainGroup);

    // === PTT ===
    QGroupBox* pttGroup = new QGroupBox("Push To Talk");
    QVBoxLayout* pttLayout = new QVBoxLayout(pttGroup);
    m_txRxIndicator = new QLabel("RX - Listening");
    m_txRxIndicator->setAlignment(Qt::AlignCenter);
    m_txRxIndicator->setStyleSheet(
        "font-size: 18px; font-weight: bold; color: #00FF66; "
        "background-color: #1A3A1A; border: 2px solid #00FF66; border-radius: 5px; padding: 8px;");
    m_pttButton = new QPushButton("PTT\n(Hold Space or Click)");
    m_pttButton->setMinimumHeight(80);
    m_pttButton->setStyleSheet(
        "QPushButton { background-color: #2A5A2A; color: white; font-size: 16px; "
        "font-weight: bold; border: 3px solid #3A7A3A; border-radius: 10px; }"
        "QPushButton:pressed { background-color: #CC3333; border-color: #FF4444; }");
    connect(m_pttButton, &QPushButton::pressed, this, &RadioWindow::onPttPressed);
    connect(m_pttButton, &QPushButton::released, this, &RadioWindow::onPttReleased);
    pttLayout->addWidget(m_txRxIndicator);
    pttLayout->addWidget(m_pttButton);
    mainLayout->addWidget(pttGroup);

    setCentralWidget(central);
}

void RadioWindow::applyDarkStyle()
{
    setStyleSheet(R"(
        QMainWindow { background-color: #1A1A2E; }
        QGroupBox { color: #AABBCC; font-weight: bold; border: 1px solid #334455;
            border-radius: 5px; margin-top: 8px; padding-top: 14px; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }
        QLabel { color: #CCDDEE; }
        QLineEdit, QSpinBox, QComboBox { background-color: #2A2A3E; color: #EEEEFF;
            border: 1px solid #445566; border-radius: 3px; padding: 3px 5px; }
        QSlider::groove:horizontal { border: 1px solid #445566; height: 6px;
            background: #2A2A3E; border-radius: 3px; }
        QSlider::handle:horizontal { background: #5599FF; width: 16px; margin: -5px 0; border-radius: 8px; }
        QPushButton { background-color: #334466; color: #EEEEFF; border: 1px solid #556688;
            border-radius: 4px; padding: 6px 12px; font-weight: bold; }
        QPushButton:hover { background-color: #445577; }
        QPushButton:pressed { background-color: #556688; }
        QTextEdit { background-color: #0A0A15; color: #88AACC; border: 1px solid #334455; }
    )");
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
    m_connectionStatus->setStyleSheet("color: #44FF44; font-weight: bold;");
    m_connectBtn->setText("Disconnect");
    logMessage("Connected to server");

    // Start audio devices
    m_audioPlayback->start();
    m_audioCapture->start();
    m_micStarted = true;

    // Send all current settings to server
    m_tcpClient->setFrequency(m_freqWidget->frequency());
    m_tcpClient->setSampleRate(m_sampleRate);
    m_tcpClient->setVgaGain(m_vgaGainSlider->value());
    m_tcpClient->setLnaGain(m_lnaGainSlider->value());
    m_tcpClient->setTxAmpGain(m_txGainSlider->value());
    m_tcpClient->setAmplitude(m_amplitudeSlider->value() / 100.0f);
    m_tcpClient->setModulationIndex(m_modIndexSlider->value() / 100.0f);
    m_tcpClient->switchToRx();
}

void RadioWindow::onDisconnected()
{
    m_connectionStatus->setText("Disconnected");
    m_connectionStatus->setStyleSheet("color: #FF4444; font-weight: bold;");
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
    for (size_t i = 0; i < n; i++)
        samples[i] = std::complex<float>(iq[i*2] / 128.0f, iq[i*2+1] / 128.0f);

    std::vector<float> audio;
    switch (m_currentModulation) {
    case FM_NB: case FM_WB: audio = m_fmDemod->demodulate(samples); break;
    case AM: audio = m_amDemod->demodulate(samples); break;
    }

    if (!audio.empty()) {
        float sumSq = 0.0f;
        for (const auto& s : audio) sumSq += s * s;
        float rms = std::sqrt(sumSq / audio.size());
        float level = std::min(rms * 5.0f, 1.0f);
        m_signalMeter->setLevel(level);

        if (level >= m_squelchLevel)
            m_audioPlayback->enqueueAudio(audio);
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

    // Send current slider values
    float amp = m_amplitudeSlider->value() / 100.0f;
    float modIdx = m_modIndexSlider->value() / 100.0f;
    m_tcpClient->setAmplitude(amp);
    m_tcpClient->setModulationIndex(modIdx);
    m_tcpClient->setTxAmpGain(m_txGainSlider->value());

    qDebug() << "TX params: amp=" << amp << "modIdx=" << modIdx << "txGain=" << m_txGainSlider->value();

    m_txRxIndicator->setText("TX - Transmitting");
    m_txRxIndicator->setStyleSheet(
        "font-size: 18px; font-weight: bold; color: #FF4444; "
        "background-color: #3A1A1A; border: 2px solid #FF4444; border-radius: 5px; padding: 8px;");
    logMessage("PTT ON");
}

void RadioWindow::onPttReleased()
{
    if (!m_isTx) return;

    qDebug() << "=== PTT RELEASED ===";
    m_isTx = false;

    m_tcpClient->switchToRx();
    m_tcpClient->setVgaGain(m_vgaGainSlider->value());
    m_tcpClient->setLnaGain(m_lnaGainSlider->value());

    m_txRxIndicator->setText("RX - Listening");
    m_txRxIndicator->setStyleSheet(
        "font-size: 18px; font-weight: bold; color: #00FF66; "
        "background-color: #1A3A1A; border: 2px solid #00FF66; border-radius: 5px; padding: 8px;");
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
        m_modeLabel->setText("NFM");
        m_modeLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #00FF66;");
        break;
    case 1:
        m_currentModulation = FM_WB;
        m_fmDemod->setBandwidth(150000.0);
        m_modeLabel->setText("WFM");
        m_modeLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #FFAA00;");
        break;
    case 2:
        m_currentModulation = AM;
        m_amDemod->setBandwidth(10000.0);
        m_modeLabel->setText("AM");
        m_modeLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #FF6666;");
        break;
    }
    m_sampleRate = 2000000;
    if (m_tcpClient->isConnected()) m_tcpClient->setSampleRate(m_sampleRate);
    m_fmDemod->setSampleRate(m_sampleRate);
    m_amDemod->setSampleRate(m_sampleRate);
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
