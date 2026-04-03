#include "gainsettingsdialog.h"
#include "tcpclient.h"
#include "fmdemodulator.h"
#include "amdemodulator.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>

GainSettingsDialog::GainSettingsDialog(TcpClient* tcpClient, FMDemodulator* fmDemod, AMDemodulator* amDemod, QWidget *parent)
    : QWidget(parent)
    , m_tcpClient(tcpClient)
    , m_fmDemod(fmDemod)
    , m_amDemod(amDemod)
{
    setupUi();
}

void GainSettingsDialog::setupUi()
{
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setSpacing(0);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // Back button at top
    QPushButton* backBtn = new QPushButton("< Back");
    backBtn->setObjectName("settingsBackBtn");
    backBtn->setMinimumHeight(48);
    backBtn->setStyleSheet(
        "QPushButton#settingsBackBtn { background-color: #1A1A33; color: #8899CC; "
        "border: 1px solid #333355; border-radius: 8px; font-weight: bold; font-size: 15px; "
        "padding: 10px 20px; margin: 8px; }"
        "QPushButton#settingsBackBtn:pressed { background-color: #2A2A44; }");
    connect(backBtn, &QPushButton::clicked, this, &GainSettingsDialog::backClicked);
    outerLayout->addWidget(backBtn);

    // Scrollable content
    QScrollArea* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);

    QWidget* content = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(content);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 8, 16, 16);

    content->setStyleSheet(R"(
        QWidget { background-color: #0D0D1A; }
        QGroupBox { color: #AABBCC; font-weight: bold; border: 1px solid #334455;
            border-radius: 8px; margin-top: 10px; padding-top: 16px; font-size: 14px; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }
        QLabel { color: #CCDDEE; font-size: 13px; }
        QSlider::groove:horizontal { border: 1px solid #445566; height: 8px;
            background: #2A2A3E; border-radius: 4px; }
        QSlider::handle:horizontal { background: #5599FF; width: 28px; height: 28px;
            margin: -10px 0; border-radius: 14px; }
        QPushButton { background-color: #334466; color: #EEEEFF; border: 1px solid #556688;
            border-radius: 8px; padding: 12px 20px; font-weight: bold; font-size: 14px; }
        QPushButton:pressed { background-color: #556688; }
        QCheckBox { color: #CCDDEE; font-size: 13px; spacing: 8px; }
        QCheckBox::indicator { width: 28px; height: 28px; border-radius: 6px;
            border: 2px solid #556688; background: #2A2A3E; }
        QCheckBox::indicator:checked { background: #CC4422; border-color: #FF6644; }
    )");

    // === Connection ===
    QGroupBox* connGroup = new QGroupBox("Connection");
    QGridLayout* connGrid = new QGridLayout(connGroup);
    connGrid->setVerticalSpacing(10);
    connGrid->setHorizontalSpacing(10);

    m_hostEdit = new QLineEdit("192.168.1.5");
    m_hostEdit->setMinimumHeight(44);
    m_hostEdit->setPlaceholderText("Server IP address");
    m_hostEdit->setStyleSheet("QLineEdit { background-color: #1A1A2E; color: #EEEEFF; "
        "border: 1px solid #334455; border-radius: 6px; padding: 8px; font-size: 14px; }");
    connGrid->addWidget(new QLabel("Host:"), 0, 0);
    connGrid->addWidget(m_hostEdit, 0, 1);

    m_dataPortSpin = new QSpinBox();
    m_dataPortSpin->setRange(1, 65535); m_dataPortSpin->setValue(5000);
    m_dataPortSpin->setMinimumHeight(44);
    m_dataPortSpin->setStyleSheet("QSpinBox { background-color: #1A1A2E; color: #EEEEFF; "
        "border: 1px solid #334455; border-radius: 6px; padding: 8px; font-size: 14px; }");
    connGrid->addWidget(new QLabel("Data Port:"), 1, 0);
    connGrid->addWidget(m_dataPortSpin, 1, 1);

    m_controlPortSpin = new QSpinBox();
    m_controlPortSpin->setRange(1, 65535); m_controlPortSpin->setValue(5001);
    m_controlPortSpin->setMinimumHeight(44);
    m_controlPortSpin->setStyleSheet("QSpinBox { background-color: #1A1A2E; color: #EEEEFF; "
        "border: 1px solid #334455; border-radius: 6px; padding: 8px; font-size: 14px; }");
    connGrid->addWidget(new QLabel("Control Port:"), 2, 0);
    connGrid->addWidget(m_controlPortSpin, 2, 1);

    m_audioPortSpin = new QSpinBox();
    m_audioPortSpin->setRange(1, 65535); m_audioPortSpin->setValue(5002);
    m_audioPortSpin->setMinimumHeight(44);
    m_audioPortSpin->setStyleSheet("QSpinBox { background-color: #1A1A2E; color: #EEEEFF; "
        "border: 1px solid #334455; border-radius: 6px; padding: 8px; font-size: 14px; }");
    connGrid->addWidget(new QLabel("Audio Port:"), 3, 0);
    connGrid->addWidget(m_audioPortSpin, 3, 1);

    connGrid->setColumnMinimumWidth(0, 100);
    connGrid->setColumnStretch(1, 1);
    mainLayout->addWidget(connGroup);

    // === RX Parameters ===
    QGroupBox* rxGroup = new QGroupBox("RX Parameters");
    QGridLayout* rxGrid = new QGridLayout(rxGroup);
    rxGrid->setVerticalSpacing(14);
    rxGrid->setHorizontalSpacing(10);
    int row = 0;

    m_vgaGainSlider = new QSlider(Qt::Horizontal); m_vgaGainSlider->setRange(0, 62); m_vgaGainSlider->setValue(30);
    m_vgaGainLabel = new QLabel("30");
    connect(m_vgaGainSlider, &QSlider::valueChanged, [this](int v) {
        m_vgaGainLabel->setText(QString::number(v));
        if (m_tcpClient->isConnected()) m_tcpClient->setVgaGain(v);
        emit settingsChanged();
    });
    rxGrid->addWidget(new QLabel("VGA (RX):"), row, 0); rxGrid->addWidget(m_vgaGainSlider, row, 1); rxGrid->addWidget(m_vgaGainLabel, row, 2); row++;

    m_lnaGainSlider = new QSlider(Qt::Horizontal); m_lnaGainSlider->setRange(0, 40); m_lnaGainSlider->setValue(40);
    m_lnaGainLabel = new QLabel("40");
    connect(m_lnaGainSlider, &QSlider::valueChanged, [this](int v) {
        m_lnaGainLabel->setText(QString::number(v));
        if (m_tcpClient->isConnected()) m_tcpClient->setLnaGain(v);
        emit settingsChanged();
    });
    rxGrid->addWidget(new QLabel("LNA (RX):"), row, 0); rxGrid->addWidget(m_lnaGainSlider, row, 1); rxGrid->addWidget(m_lnaGainLabel, row, 2); row++;

    m_rxGainSlider = new QSlider(Qt::Horizontal); m_rxGainSlider->setRange(1, 100); m_rxGainSlider->setValue(10);
    m_rxGainLabel = new QLabel("1.0");
    connect(m_rxGainSlider, &QSlider::valueChanged, [this](int v) {
        float gain = v / 10.0f;
        m_rxGainLabel->setText(QString::number(gain, 'f', 1));
        m_fmDemod->setOutputGain(gain);
        emit settingsChanged();
    });
    rxGrid->addWidget(new QLabel("RX Gain:"), row, 0); rxGrid->addWidget(m_rxGainSlider, row, 1); rxGrid->addWidget(m_rxGainLabel, row, 2); row++;

    // IF Bandwidth slider: 1 kHz - 200 kHz
    m_ifBwSlider = new QSlider(Qt::Horizontal); m_ifBwSlider->setRange(1, 200); m_ifBwSlider->setValue(12);
    m_ifBwLabel = new QLabel("12.5 kHz");
    connect(m_ifBwSlider, &QSlider::valueChanged, [this](int v) {
        float bw;
        if (v <= 25) {
            bw = v * 500.0f;  // 500 Hz steps: 500 - 12500 Hz
        } else {
            bw = 12500.0f + (v - 25) * 1000.0f;  // 1 kHz steps above 12.5 kHz
        }
        if (bw >= 1000.0f)
            m_ifBwLabel->setText(QString("%1 kHz").arg(bw / 1000.0f, 0, 'f', 1));
        else
            m_ifBwLabel->setText(QString("%1 Hz").arg(static_cast<int>(bw)));
        m_fmDemod->setBandwidth(bw);
        m_amDemod->setBandwidth(bw);
        emit settingsChanged();
    });
    rxGrid->addWidget(new QLabel("IF BW:"), row, 0); rxGrid->addWidget(m_ifBwSlider, row, 1); rxGrid->addWidget(m_ifBwLabel, row, 2); row++;

    // RX Modulation Index slider: 0.1 - 5.0 (slider 1-50, /10)
    m_rxModIdxSlider = new QSlider(Qt::Horizontal); m_rxModIdxSlider->setRange(1, 50); m_rxModIdxSlider->setValue(30);
    m_rxModIdxLabel = new QLabel("3.0");
    connect(m_rxModIdxSlider, &QSlider::valueChanged, [this](int v) {
        float idx = v / 10.0f;
        m_rxModIdxLabel->setText(QString::number(idx, 'f', 1));
        m_fmDemod->setRxModIndex(idx);
        emit settingsChanged();
    });
    rxGrid->addWidget(new QLabel("RX ModIdx:"), row, 0); rxGrid->addWidget(m_rxModIdxSlider, row, 1); rxGrid->addWidget(m_rxModIdxLabel, row, 2); row++;

    m_deemphSlider = new QSlider(Qt::Horizontal); m_deemphSlider->setRange(0, 1000); m_deemphSlider->setValue(0);
    m_deemphLabel = new QLabel("OFF");
    connect(m_deemphSlider, &QSlider::valueChanged, [this](int v) {
        if (v == 0) m_deemphLabel->setText("OFF");
        else m_deemphLabel->setText(QString("%1us").arg(v));
        m_fmDemod->setDeemphTau(static_cast<float>(v));
        emit settingsChanged();
    });
    rxGrid->addWidget(new QLabel("DeEmph:"), row, 0); rxGrid->addWidget(m_deemphSlider, row, 1); rxGrid->addWidget(m_deemphLabel, row, 2); row++;

    rxGrid->setColumnMinimumWidth(0, 80);
    rxGrid->setColumnMinimumWidth(2, 50);
    rxGrid->setColumnStretch(1, 1);
    mainLayout->addWidget(rxGroup);

    // === TX Parameters ===
    QGroupBox* txGroup = new QGroupBox("TX Parameters");
    QGridLayout* txGrid = new QGridLayout(txGroup);
    txGrid->setVerticalSpacing(14);
    txGrid->setHorizontalSpacing(10);
    row = 0;

    m_txGainSlider = new QSlider(Qt::Horizontal); m_txGainSlider->setRange(0, 47); m_txGainSlider->setValue(47);
    m_txGainLabel = new QLabel("47");
    connect(m_txGainSlider, &QSlider::valueChanged, [this](int v) {
        m_txGainLabel->setText(QString::number(v));
        if (m_tcpClient->isConnected()) m_tcpClient->setTxAmpGain(v);
        emit settingsChanged();
    });
    txGrid->addWidget(new QLabel("TX Power:"), row, 0); txGrid->addWidget(m_txGainSlider, row, 1); txGrid->addWidget(m_txGainLabel, row, 2); row++;

    m_amplitudeSlider = new QSlider(Qt::Horizontal); m_amplitudeSlider->setRange(1, 100); m_amplitudeSlider->setValue(50);
    m_amplitudeLabel = new QLabel("0.50");
    connect(m_amplitudeSlider, &QSlider::valueChanged, [this](int v) {
        float amp = v / 100.0f;
        m_amplitudeLabel->setText(QString::number(amp, 'f', 2));
        if (m_tcpClient->isConnected()) m_tcpClient->setAmplitude(amp);
        emit settingsChanged();
    });
    txGrid->addWidget(new QLabel("TX Amp:"), row, 0); txGrid->addWidget(m_amplitudeSlider, row, 1); txGrid->addWidget(m_amplitudeLabel, row, 2); row++;

    m_modIndexSlider = new QSlider(Qt::Horizontal); m_modIndexSlider->setRange(1, 500); m_modIndexSlider->setValue(40);
    m_modIndexLabel = new QLabel("0.40");
    connect(m_modIndexSlider, &QSlider::valueChanged, [this](int v) {
        float idx = v / 100.0f;
        m_modIndexLabel->setText(QString::number(idx, 'f', 2));
        if (m_tcpClient->isConnected()) m_tcpClient->setModulationIndex(idx);
        emit settingsChanged();
    });
    txGrid->addWidget(new QLabel("Mod Idx:"), row, 0); txGrid->addWidget(m_modIndexSlider, row, 1); txGrid->addWidget(m_modIndexLabel, row, 2); row++;

    m_ampEnableCheck = new QCheckBox("RF Amp Enable (+14 dB)");
    m_ampEnableCheck->setChecked(false);
    connect(m_ampEnableCheck, &QCheckBox::toggled, [this](bool checked) {
        if (m_tcpClient->isConnected()) m_tcpClient->setAmpEnable(checked);
        emit ampEnableChanged(checked);
        emit settingsChanged();
    });
    txGrid->addWidget(m_ampEnableCheck, row, 0, 1, 3); row++;

    txGrid->setColumnMinimumWidth(0, 80);
    txGrid->setColumnMinimumWidth(2, 50);
    txGrid->setColumnStretch(1, 1);
    mainLayout->addWidget(txGroup);

    mainLayout->addStretch();

    scroll->setWidget(content);
    outerLayout->addWidget(scroll, 1);
}

// === Connection Accessors ===

QString GainSettingsDialog::host() const { return m_hostEdit->text(); }
int GainSettingsDialog::dataPort() const { return m_dataPortSpin->value(); }
int GainSettingsDialog::controlPort() const { return m_controlPortSpin->value(); }
int GainSettingsDialog::audioPort() const { return m_audioPortSpin->value(); }
void GainSettingsDialog::setHost(const QString& h) { m_hostEdit->setText(h); }
void GainSettingsDialog::setDataPort(int p) { m_dataPortSpin->setValue(p); }
void GainSettingsDialog::setControlPort(int p) { m_controlPortSpin->setValue(p); }
void GainSettingsDialog::setAudioPort(int p) { m_audioPortSpin->setValue(p); }

// === Accessors ===

int GainSettingsDialog::vgaGain() const { return m_vgaGainSlider->value(); }
int GainSettingsDialog::lnaGain() const { return m_lnaGainSlider->value(); }
int GainSettingsDialog::txGain() const { return m_txGainSlider->value(); }
int GainSettingsDialog::amplitude() const { return m_amplitudeSlider->value(); }
int GainSettingsDialog::modIndex() const { return m_modIndexSlider->value(); }
int GainSettingsDialog::rxGain() const { return m_rxGainSlider->value(); }
int GainSettingsDialog::deemph() const { return m_deemphSlider->value(); }
int GainSettingsDialog::ifBandwidth() const { return m_ifBwSlider->value(); }
int GainSettingsDialog::rxModIndex() const { return m_rxModIdxSlider->value(); }
bool GainSettingsDialog::ampEnabled() const { return m_ampEnableCheck->isChecked(); }

void GainSettingsDialog::setVgaGain(int v) { m_vgaGainSlider->setValue(v); }
void GainSettingsDialog::setLnaGain(int v) { m_lnaGainSlider->setValue(v); }
void GainSettingsDialog::setTxGain(int v) { m_txGainSlider->setValue(v); }
void GainSettingsDialog::setAmplitude(int v) { m_amplitudeSlider->setValue(v); }
void GainSettingsDialog::setModIndex(int v) { m_modIndexSlider->setValue(v); }
void GainSettingsDialog::setRxGain(int v) { m_rxGainSlider->setValue(v); }
void GainSettingsDialog::setDeemph(int v) { m_deemphSlider->setValue(v); }
void GainSettingsDialog::setIfBandwidth(int v) { m_ifBwSlider->setValue(v); }
void GainSettingsDialog::setRxModIndex(int v) { m_rxModIdxSlider->setValue(v); }
void GainSettingsDialog::setAmpEnabled(bool en) {
    m_ampEnableCheck->blockSignals(true);
    m_ampEnableCheck->setChecked(en);
    m_ampEnableCheck->blockSignals(false);
}

void GainSettingsDialog::sendTxParams()
{
    if (!m_tcpClient->isConnected()) return;
    float amp = m_amplitudeSlider->value() / 100.0f;
    float modIdx = m_modIndexSlider->value() / 100.0f;
    m_tcpClient->setAmplitude(amp);
    m_tcpClient->setModulationIndex(modIdx);
    m_tcpClient->setTxAmpGain(m_txGainSlider->value());
    m_tcpClient->setAmpEnable(m_ampEnableCheck->isChecked());
}

void GainSettingsDialog::sendRxParams()
{
    if (!m_tcpClient->isConnected()) return;
    m_tcpClient->setVgaGain(m_vgaGainSlider->value());
    m_tcpClient->setLnaGain(m_lnaGainSlider->value());
}
