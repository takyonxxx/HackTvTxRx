#include "gainsettingsdialog.h"
#include "tcpclient.h"
#include "fmdemodulator.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QScroller>

GainSettingsDialog::GainSettingsDialog(TcpClient* tcpClient, FMDemodulator* fmDemod, QWidget *parent)
    : QDialog(parent)
    , m_tcpClient(tcpClient)
    , m_fmDemod(fmDemod)
{
    setWindowTitle("Gain & TX Parameters");
    setModal(false);
    setupUi();
}

void GainSettingsDialog::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // Apply dark style matching main window
    setStyleSheet(R"(
        QDialog { background-color: #1A1A2E; }
        QGroupBox { color: #AABBCC; font-weight: bold; border: 1px solid #334455;
            border-radius: 8px; margin-top: 10px; padding-top: 16px;
            font-size: 14px; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }
        QLabel { color: #CCDDEE; font-size: 13px; }
        QSlider::groove:horizontal { border: 1px solid #445566; height: 8px;
            background: #2A2A3E; border-radius: 4px; }
        QSlider::handle:horizontal { background: #5599FF; width: 28px; height: 28px;
            margin: -10px 0; border-radius: 14px; }
        QPushButton { background-color: #334466; color: #EEEEFF; border: 1px solid #556688;
            border-radius: 8px; padding: 12px 20px; font-weight: bold; font-size: 14px; }
        QPushButton:pressed { background-color: #556688; }
    )");

    // === RX Gain Group ===
    QGroupBox* rxGroup = new QGroupBox("RX Parameters");
    QGridLayout* rxGrid = new QGridLayout(rxGroup);
    rxGrid->setVerticalSpacing(14);
    rxGrid->setHorizontalSpacing(10);
    int row = 0;

    m_vgaGainSlider = new QSlider(Qt::Horizontal); m_vgaGainSlider->setRange(0, 62); m_vgaGainSlider->setValue(40);
    m_vgaGainLabel = new QLabel("40");
    connect(m_vgaGainSlider, &QSlider::valueChanged, [this](int v) {
        m_vgaGainLabel->setText(QString::number(v));
        if (m_tcpClient->isConnected()) m_tcpClient->setVgaGain(v);
    });
    rxGrid->addWidget(new QLabel("VGA (RX):"), row, 0); rxGrid->addWidget(m_vgaGainSlider, row, 1); rxGrid->addWidget(m_vgaGainLabel, row, 2); row++;

    m_lnaGainSlider = new QSlider(Qt::Horizontal); m_lnaGainSlider->setRange(0, 40); m_lnaGainSlider->setValue(40);
    m_lnaGainLabel = new QLabel("40");
    connect(m_lnaGainSlider, &QSlider::valueChanged, [this](int v) {
        m_lnaGainLabel->setText(QString::number(v));
        if (m_tcpClient->isConnected()) m_tcpClient->setLnaGain(v);
    });
    rxGrid->addWidget(new QLabel("LNA (RX):"), row, 0); rxGrid->addWidget(m_lnaGainSlider, row, 1); rxGrid->addWidget(m_lnaGainLabel, row, 2); row++;

    m_rxGainSlider = new QSlider(Qt::Horizontal); m_rxGainSlider->setRange(1, 100); m_rxGainSlider->setValue(45);
    m_rxGainLabel = new QLabel("4.5");
    connect(m_rxGainSlider, &QSlider::valueChanged, [this](int v) {
        float gain = v / 10.0f;
        m_rxGainLabel->setText(QString::number(gain, 'f', 1));
        m_fmDemod->setOutputGain(gain);
    });
    rxGrid->addWidget(new QLabel("RX Gain:"), row, 0); rxGrid->addWidget(m_rxGainSlider, row, 1); rxGrid->addWidget(m_rxGainLabel, row, 2); row++;

    m_deemphSlider = new QSlider(Qt::Horizontal); m_deemphSlider->setRange(0, 1000); m_deemphSlider->setValue(750);
    m_deemphLabel = new QLabel("750us");
    connect(m_deemphSlider, &QSlider::valueChanged, [this](int v) {
        if (v == 0) m_deemphLabel->setText("OFF");
        else m_deemphLabel->setText(QString("%1us").arg(v));
        m_fmDemod->setDeemphTau(static_cast<float>(v));
    });
    rxGrid->addWidget(new QLabel("DeEmph:"), row, 0); rxGrid->addWidget(m_deemphSlider, row, 1); rxGrid->addWidget(m_deemphLabel, row, 2); row++;

    rxGrid->setColumnMinimumWidth(0, 80);
    rxGrid->setColumnMinimumWidth(2, 50);
    rxGrid->setColumnStretch(1, 1);
    mainLayout->addWidget(rxGroup);

    // === TX Gain Group ===
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
    });
    txGrid->addWidget(new QLabel("TX Power:"), row, 0); txGrid->addWidget(m_txGainSlider, row, 1); txGrid->addWidget(m_txGainLabel, row, 2); row++;

    m_amplitudeSlider = new QSlider(Qt::Horizontal); m_amplitudeSlider->setRange(1, 100); m_amplitudeSlider->setValue(10);
    m_amplitudeLabel = new QLabel("0.10");
    connect(m_amplitudeSlider, &QSlider::valueChanged, [this](int v) {
        float amp = v / 100.0f;
        m_amplitudeLabel->setText(QString::number(amp, 'f', 2));
        if (m_tcpClient->isConnected()) m_tcpClient->setAmplitude(amp);
    });
    txGrid->addWidget(new QLabel("TX Amp:"), row, 0); txGrid->addWidget(m_amplitudeSlider, row, 1); txGrid->addWidget(m_amplitudeLabel, row, 2); row++;

    m_modIndexSlider = new QSlider(Qt::Horizontal); m_modIndexSlider->setRange(1, 500); m_modIndexSlider->setValue(40);
    m_modIndexLabel = new QLabel("0.40");
    connect(m_modIndexSlider, &QSlider::valueChanged, [this](int v) {
        float idx = v / 100.0f;
        m_modIndexLabel->setText(QString::number(idx, 'f', 2));
        if (m_tcpClient->isConnected()) m_tcpClient->setModulationIndex(idx);
    });
    txGrid->addWidget(new QLabel("Mod Idx:"), row, 0); txGrid->addWidget(m_modIndexSlider, row, 1); txGrid->addWidget(m_modIndexLabel, row, 2); row++;

    // RF Amp Enable checkbox
    m_ampEnableCheck = new QCheckBox("RF Amp Enable (+14 dB)");
    m_ampEnableCheck->setChecked(false);
    m_ampEnableCheck->setStyleSheet(
        "QCheckBox { color: #CCDDEE; font-size: 13px; spacing: 8px; }"
        "QCheckBox::indicator { width: 28px; height: 28px; border-radius: 6px; "
        "  border: 2px solid #556688; background: #2A2A3E; }"
        "QCheckBox::indicator:checked { background: #CC4422; border-color: #FF6644; }");
    connect(m_ampEnableCheck, &QCheckBox::toggled, [this](bool checked) {
        if (m_tcpClient->isConnected()) m_tcpClient->setAmpEnable(checked);
        emit ampEnableChanged(checked);
    });
    txGrid->addWidget(m_ampEnableCheck, row, 0, 1, 3); row++;

    txGrid->setColumnMinimumWidth(0, 80);
    txGrid->setColumnMinimumWidth(2, 50);
    txGrid->setColumnStretch(1, 1);
    mainLayout->addWidget(txGroup);

    // Close button
    QPushButton* closeBtn = new QPushButton("Close");
    closeBtn->setMinimumHeight(48);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    mainLayout->addWidget(closeBtn);

    mainLayout->addStretch();
}

// === Accessors ===

int GainSettingsDialog::vgaGain() const { return m_vgaGainSlider->value(); }
int GainSettingsDialog::lnaGain() const { return m_lnaGainSlider->value(); }
int GainSettingsDialog::txGain() const { return m_txGainSlider->value(); }
int GainSettingsDialog::amplitude() const { return m_amplitudeSlider->value(); }
int GainSettingsDialog::modIndex() const { return m_modIndexSlider->value(); }
int GainSettingsDialog::rxGain() const { return m_rxGainSlider->value(); }
int GainSettingsDialog::deemph() const { return m_deemphSlider->value(); }
bool GainSettingsDialog::ampEnabled() const { return m_ampEnableCheck->isChecked(); }

void GainSettingsDialog::setVgaGain(int v) { m_vgaGainSlider->setValue(v); }
void GainSettingsDialog::setLnaGain(int v) { m_lnaGainSlider->setValue(v); }
void GainSettingsDialog::setTxGain(int v) { m_txGainSlider->setValue(v); }
void GainSettingsDialog::setAmplitude(int v) { m_amplitudeSlider->setValue(v); }
void GainSettingsDialog::setModIndex(int v) { m_modIndexSlider->setValue(v); }
void GainSettingsDialog::setRxGain(int v) { m_rxGainSlider->setValue(v); }
void GainSettingsDialog::setDeemph(int v) { m_deemphSlider->setValue(v); }
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
