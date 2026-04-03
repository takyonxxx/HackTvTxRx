#ifndef RADIOWINDOW_H
#define RADIOWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QStackedWidget>
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
    void onModulationChanged(int index);
    void onVolumeChanged(int value);
    void onSquelchChanged(int value);
    void onSettingsClicked();
    void onSettingsBack();
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

    // Page switching
    QStackedWidget* m_stackedWidget;

    // Connection UI
    QPushButton* m_connectBtn;
    QLabel* m_connectionStatus;

    // Frequency
    FrequencyWidget* m_freqWidget;

    // Band preset (cycling button)
    QPushButton* m_bandPresetBtn;
    int m_bandPresetIndex = 0;

    // Mode (cycling button)
    QPushButton* m_modulationBtn;
    int m_modulationIndex = 0;

    // BW (cycling button)
    QPushButton* m_bwBtn;
    int m_bwIndex = 0;

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

    // Settings page
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

    // BW data
    struct BwEntry { QString label; uint32_t rate; };
    QVector<BwEntry> m_bwEntries;

    // Band preset data
    struct BandEntry { QString label; uint64_t freq; };
    QVector<BandEntry> m_bandEntries;

    // Modulation data
    struct ModEntry { QString label; QString shortLabel; QString color; };
    QVector<ModEntry> m_modEntries;

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
