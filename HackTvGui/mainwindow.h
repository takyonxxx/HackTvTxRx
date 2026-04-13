#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>
#include <QGroupBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTextBrowser>
#include <QFileDialog>
#include <QThreadPool>
#include <QTimer>
#include <QMessageBox>
#include <QAudioSource>
#include <QAudioFormat>
#include <QAudioDecoder>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QLabel>
#include <QKeyEvent>
#include <QTcpSocket>

#include <memory>
#include <vector>
#include <complex>
#include "hacktvlib.h"
#include "freqctrl.h"
#include "glplotter.h"
#include "meter.h"
#include "audiooutput.h"
#include "modulator.h"
#include "fmdemodulator.h"
#include "amdemodulator.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void updatePlotter(float* fft_data, int size);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onStartStopClicked();
    void onPttPressed();
    void onPttReleased();
    void chooseFile();
    void onOperatingModeChanged(int index);
    void onSampleRateChanged(int index);
    void onChannelChanged(int index);
    void onFreqCtrl_setFrequency(qint64 freq);
    void on_plotter_newDemodFreq(qint64 freq, qint64 delta);
    void on_plotter_newFilterFreq(int low, int high);
    void updateLogDisplay();
    void exitApp();
    void hardReset();

private:
    void initializeHackTvLib();
    void setupUi();
    void addDeviceGroup();
    void addRxGroup();
    void addRadioTxGroup();
    void addTvTxGroup();
    void saveSettings();
    void loadSettings();
    void populateChannelCombo();
    QStringList buildRxCommand();
    QStringList buildTvTxCommand();
    void setCurrentSampleRate(int sampleRate);
    void processFft(const std::vector<std::complex<float>>& samples);
    void processDemod(const std::vector<std::complex<float>>& samples);
    void handleReceivedData(const int8_t *data, size_t len);
    void startRx();
    void stopAll();
    void switchToTx();
    void switchToRx();
    void applyModePresets();
    void applyModeTheme();

    QVBoxLayout *mainLayout;

    // Device group
    QComboBox *outputCombo, *sampleRateCombo;
    QCheckBox *ampEnabled;
    QCheckBox *stereoEnabled;
    QLineEdit *tcpAddressEdit;
    QLabel *tcpAddressLabel;

    // Operating mode
    // 0=NFM Radio, 1=WFM Radio, 2=AM Radio, 3=FM File TX, 4=TV File TX, 5=TV Test TX, 6=TV RTSP TX
    QComboBox *operatingModeCombo;
    enum OperatingMode { MODE_NFM=0, MODE_WFM=1, MODE_AM=2, MODE_FM_FILE=3,
                         MODE_TV_FILE=4, MODE_TV_TEST=5, MODE_TV_RTSP=6 };
    int m_opMode = MODE_WFM;

    // RX group
    CFreqCtrl *freqCtrl;
    CPlotter *cPlotter;
    CMeter *cMeter;
    QLabel *m_stereoLabel;
    QSlider *volumeSlider, *lnaSlider, *vgaSlider, *rxAmpSlider;
    QLabel *volumeLevelLabel, *lnaLevelLabel, *vgaLevelLabel, *rxAmpLevelLabel;
    QSlider *rxGainSlider, *rxModIndexSlider, *rxDeemphSlider;
    QLabel *rxGainLevelLabel, *rxModIndexLevelLabel, *rxDeemphLevelLabel;
    QLabel *rxModIndexLabel, *rxDeemphLabel;
    QGroupBox *rxGroup;

    // Radio TX group (PTT + TX params)
    QGroupBox *radioTxGroup;
    QPushButton *pttButton;
    QLabel *txRxIndicator;
    QSlider *txAmplitudeSlider, *txModIndexSlider, *txPowerSlider;
    QLabel *txAmplitudeLevelLabel, *txModIndexLevelLabel, *txPowerLevelLabel;

    // TV TX group
    QGroupBox *tvTxGroup;
    QComboBox *tvModeCombo, *channelCombo;
    QLineEdit *inputFileEdit, *ffmpegOptionsEdit;
    QPushButton *chooseFileButton;
    QCheckBox *colorDisabled;

    // Bottom buttons
    QPushButton *startStopButton, *exitButton, *hardResetButton;

    // Log
    QTextBrowser *logBrowser;
    QTimer *logTimer;
    QStringList pendingLogs;

    // HackTvLib
    HackTvLib* m_hackTvLib = nullptr;
    std::unique_ptr<AudioOutput> audioOutput;
    QThreadPool* m_threadPool = nullptr;

    QString m_sSettingsFile;

    // State
    qint64 m_frequency = 145000000;
    int m_sampleRate = 2000000;
    int m_volumeLevel = 10;
    int m_lnaGain = 20;
    int m_vgaGain = 20;
    int m_txAmpGain = 47;
    int m_rxAmpGain = 0;
    float audioGain = 0.75f;
    float rxGain = 2.0f;
    float rxModIndex = 1.5f;
    int rxDeemph = 0;
    float tx_amplitude = 0.50f;
    float tx_modulation_index = 0.40f;

    int m_LowCutFreq = -12500;
    int m_HiCutFreq = 12500;
    int m_CutFreq = 12500;
    int m_rxBandwidth = 12500;

    bool m_isTx = false;
    bool m_pttHeld = false;
    bool m_isRadioMode = true;
    std::atomic<bool> m_shuttingDown{false};
    std::atomic<bool> m_isProcessing{false};
    bool m_initDone = false;
    bool m_forceMono = false;
    QAtomicInt m_fftUpdatePending{0};

    // Demod
    std::unique_ptr<FMDemodulator> fmDemodulator;
    std::unique_ptr<AMDemodulator> amDemodulator;

    // Mic capture
    QAudioSource* m_micSource = nullptr;
    QIODevice* m_micDevice = nullptr;
    QTimer* m_micFlushTimer = nullptr;
    bool m_micStarted = false;

    // File playback
    QTimer* m_filePlayTimer = nullptr;
    QAudioDecoder* m_audioDecoder = nullptr;
    std::vector<float> m_fileAudioData;
    size_t m_filePlayPos = 0;

    void startMicCapture();
    void stopMicCapture();
    void startFilePlayback(const QString& filePath);
    void startFilePlaybackTimer();
    void stopFilePlayback();

    QFileDialog *fileDialog;
    QString labelStyle;
    QGroupBox *deviceGroup;
    QString m_modeAccentColor;
    QString m_modeAccentDark;

    // TCP client for emulator mode
    bool isTcpMode() const {
        QString dev = outputCombo->currentData().toString();
        return (dev == "hackrftcp" || dev == "rtlsdrtcp");
    }
    bool isRtlTcpMode() const { return outputCombo->currentData().toString() == "rtlsdrtcp"; }
    void startTcpRx();
    void stopTcpRx();
    void sendTcpCommand(const QString& cmd);

    QTcpSocket *m_tcpDataSocket = nullptr;
    QTcpSocket *m_tcpCtrlSocket = nullptr;
    QTcpSocket *m_tcpAudioSocket = nullptr;
    QByteArray m_tcpBuffer;
    bool m_tcpConnected = false;
};

#endif // MAINWINDOW_H
