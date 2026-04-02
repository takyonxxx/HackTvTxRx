#ifndef SIGNALMETER_H
#define SIGNALMETER_H

#include <QWidget>
#include <QTimer>

class SignalMeter : public QWidget
{
    Q_OBJECT

public:
    explicit SignalMeter(QWidget *parent = nullptr);

    // RX mode: set signal level 0.0 - 1.0 (from IQ RMS)
    void setLevel(float level);
    float level() const { return m_level; }

    // TX mode: set estimated TX power in dBm
    // HackRF range: roughly -40 dBm to +10 dBm
    void setTxPowerDbm(float dbm);

    // Mode control
    void setTxMode(bool tx);
    bool isTxMode() const { return m_txMode; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    float m_level = 0.0f;
    float m_displayLevel = 0.0f;
    float m_peak = 0.0f;
    bool m_txMode = false;
    float m_txDbm = -40.0f;
    QTimer* m_animTimer;
};

#endif // SIGNALMETER_H
