#ifndef GAINSETTINGSDIALOG_H
#define GAINSETTINGSDIALOG_H

#include <QDialog>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>

class TcpClient;
class FMDemodulator;
class AMDemodulator;

class GainSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GainSettingsDialog(TcpClient* tcpClient, FMDemodulator* fmDemod, AMDemodulator* amDemod, QWidget *parent = nullptr);

    // Accessors for save/load
    int vgaGain() const;
    int lnaGain() const;
    int txGain() const;
    int amplitude() const;
    int modIndex() const;
    int rxGain() const;
    int deemph() const;
    int ifBandwidth() const;
    int rxModIndex() const;
    bool ampEnabled() const;

    void setVgaGain(int v);
    void setLnaGain(int v);
    void setTxGain(int v);
    void setAmplitude(int v);
    void setModIndex(int v);
    void setRxGain(int v);
    void setDeemph(int v);
    void setIfBandwidth(int v);
    void setRxModIndex(int v);
    void setAmpEnabled(bool en);

    // Called on PTT press to push TX params to server
    void sendTxParams();

    // Called on PTT release to push RX params to server
    void sendRxParams();

signals:
    void ampEnableChanged(bool enabled);
    void settingsChanged();

private:
    void setupUi();

    TcpClient* m_tcpClient;
    FMDemodulator* m_fmDemod;
    AMDemodulator* m_amDemod;

    QSlider* m_vgaGainSlider;   QLabel* m_vgaGainLabel;
    QSlider* m_lnaGainSlider;   QLabel* m_lnaGainLabel;
    QSlider* m_txGainSlider;    QLabel* m_txGainLabel;
    QSlider* m_amplitudeSlider; QLabel* m_amplitudeLabel;
    QSlider* m_modIndexSlider;  QLabel* m_modIndexLabel;
    QSlider* m_rxGainSlider;    QLabel* m_rxGainLabel;
    QSlider* m_deemphSlider;    QLabel* m_deemphLabel;
    QSlider* m_ifBwSlider;      QLabel* m_ifBwLabel;
    QSlider* m_rxModIdxSlider;  QLabel* m_rxModIdxLabel;
    QCheckBox* m_ampEnableCheck;
};

#endif // GAINSETTINGSDIALOG_H
