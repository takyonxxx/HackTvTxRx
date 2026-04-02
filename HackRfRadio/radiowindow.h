#ifndef RADIOWINDOW_H
#define RADIOWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <complex>
#include <vector>
#include <atomic>

#include "tcpclient.h"
#include "audiocapture.h"
#include "audioplayback.h"
#include "fmdemodulator.h"
#include "amdemodulator.h"
#include "frequencywidget.h"
#include "meter.h"
#include "glplotter.h"
#include "gainsettingsdialog.h"

class RadioWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit RadioWindow(QWidget *parent = nullptr);
    ~RadioWindow();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onConnectClicked();
    void onConnected();
    void onDisconnected();
    void onConnectionError(const QString& error);
    void onControlResponse(const QString& response);
    void onIqDataReceived(const QByteArray& data);
    void onPttPressed();
    void onPttReleased();
    void onAudioCaptured(const std::vector<float>& samples);
    void onFrequencyChanged(uint64_t freq);
    void onBandPresetChanged(int index);
    void onModulationChanged(int index);
    void onVolumeChanged(int value);
    void onSquelchChanged(int value);
    void onSettingsClicked();
    void onBwChanged(int index);

private:
    void setupUi();
    void applyDarkStyle();
    void processIqBuffer();
    void logMessage(const QString& msg);
    void saveSettings();
    void loadSettings();

    TcpClient* m_tcpClient;
    AudioCapture* m_audioCapture;
    AudioPlayback* m_audioPlayback;
    FMDemodulator* m_fmDemod;
    AMDemodulator* m_amDemod;

    // Connection
    QLineEdit* m_hostEdit;
    QSpinBox* m_dataPortSpin;
    QSpinBox* m_controlPortSpin;
    QSpinBox* m_audioPortSpin;
    QPushButton* m_connectBtn;
    QLabel* m_connectionStatus;

    // Frequency
    FrequencyWidget* m_freqWidget;
    QComboBox* m_bandPreset;

    // Mode
    QComboBox* m_modulationCombo;
    QComboBox* m_bwCombo;
    QLabel* m_modeLabel;
    QLabel* m_stereoLabel;

    // Volume & Squelch & IF BW (on main screen)
    QSlider* m_volumeSlider;    QLabel* m_volumeLabel;
    QSlider* m_squelchSlider;   QLabel* m_squelchLabel;
    QSlider* m_mainIfBwSlider;  QLabel* m_mainIfBwLabel;

    // PTT
    QPushButton* m_pttButton;
    QLabel* m_txRxIndicator;

    // Signal meter & spectrum
    CMeter* m_cMeter;
    CPlotter* m_cPlotter;

    // Settings dialog
    GainSettingsDialog* m_gainDialog;
    QPushButton* m_settingsBtn;
    QPushButton* m_rfAmpBtn;

    // State
    enum Modulation { FM_NB, FM_WB, AM };
    Modulation m_currentModulation = FM_NB;
    bool m_isTx = false;
    bool m_pttHeld = false;
    bool m_micStarted = false;
    bool m_forceMono = false;
    uint32_t m_sampleRate = 2000000;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    float m_squelchLevel = 0.0f;
    float m_lastSignalLevel = 0.0f;

    QByteArray m_iqAccumulator;
    static constexpr int IQ_PROCESS_THRESHOLD = 32768;
    QAtomicInt m_fftUpdatePending{0};

public slots:
    void updatePlotter(float* fft_data, int size);
};

#endif // RADIOWINDOW_H
