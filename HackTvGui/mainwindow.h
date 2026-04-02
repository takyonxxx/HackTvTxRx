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

#include <memory>
#include <vector>
#include <complex>
#include "hacktvlib.h"
#include "freqctrl.h"
#include "glplotter.h"
#include "meter.h"
#include "audiooutput.h"
#include "modulator.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    struct AtomicGuard {
        QAtomicInt& counter;
        // CRITICAL: Kilit açma işlemini sadece yıkıcı (destructor) içinde yapıyoruz.
        AtomicGuard(QAtomicInt& c) : counter(c) {}
        ~AtomicGuard() { counter.storeRelease(0); }
    };

public slots:
    void updatePlotter(float* fft_data, int size);

private slots:
    void executeCommand();
    void chooseFile();
    void onInputTypeChanged(int index);
    void onRxTxTypeChanged(int index);
    void onSampleRateChanged(int index);
    void onChannelChanged(int index);
    void onFreqCtrl_setFrequency(qint64 freq);
    void on_plotter_newDemodFreq(qint64 freq, qint64 delta);
    void on_plotter_newFilterFreq(int low, int high);
    void handleSamples(const std::vector<std::complex<float>>& samples);
    void updateLogDisplay();
    void onVolumeSliderValueChanged(int value);
    void onLnaSliderValueChanged(int value);
    void onVgaSliderValueChanged(int value);
    void onRxAmpSliderValueChanged(int value);
    void clear();
    void exitApp();
    void hardReset();

private:
    void initializeHackTvLib();
    void setupUi();
    void addOutputGroup();
    void addinputTypeGroup();
    void addModeGroup();
    void addRxGroup();
    void saveSettings();
    void loadSettings();
    void populateChannelCombo();
    QStringList buildCommand();
    void setCurrentSampleRate(int sampleRate);
    void processFft(const std::vector<std::complex<float>>& samples);
    void processDemod(const std::vector<std::complex<float>>& samples);    
    void handleLog(const std::string& logMessage);
    void handleReceivedData(const int8_t *data, size_t len);
    void updateGainControlsForDevice(const QString& device);

    QVBoxLayout *mainLayout;

    // Invert checkbox in fourth row
    QComboBox *outputCombo, *channelCombo, *sampleRateCombo, *rxtxCombo, *inputTypeCombo, *modeCombo;
    QCheckBox *ampEnabled, *colorDisabled;
    QLineEdit *frequencyEdit, *inputFileEdit, *ffmpegOptionsEdit;
    QLabel *channelLabel;
    QPushButton *chooseFileButton, *executeButton, *exitButton, *clearButton, *hardResetButton;
    QSlider *txAmplitudeSlider, *txFilterSizeSlider, *txModulationIndexSlider, *txInterpolationSlider;
    QDoubleSpinBox *txAmplitudeSpinBox, *txFilterSizeSpinBox, *txModulationIndexSpinBox, *txInterpolationSpinBox;
    QSpinBox *txAmpSpinBox;
    QLabel *volumeLabel, *volumeLevelLabel, *lnaLabel, *lnaLevelLabel, *vgaLabel, *vgaLevelLabel,
        *rxAmpLabel, *rxAmpLevelLabel, *rtlPpmLabel, *rtlDirectLabel,
        *rxGainLabel, *rxGainLevelLabel, *rxModIndexLabel, *rxModIndexLevelLabel, *rxDeemphLabel, *rxDeemphLevelLabel;
    QSlider *volumeSlider, *lnaSlider, *vgaSlider, *txAmpSlider, *rxAmpSlider,
        *rxGainSlider, *rxModIndexSlider, *rxDeemphSlider;
    QSpinBox *rtlPpmSpinBox;
    QComboBox *rtlDirectCombo;
    QCheckBox *rtlOffsetCheck;
    QFileDialog *fileDialog;
    CFreqCtrl *freqCtrl;
    CPlotter *cPlotter;
    CMeter *cMeter;

    QString sliderStyle, labelStyle;

    // Layouts and Groups
    QGridLayout *txControlsLayout;
    QGroupBox *outputGroup, *inputTypeGroup, *modeGroup, *rxGroup;


    QTextBrowser *logBrowser;
    //std::unique_ptr<HackTvLib> m_hackTvLib;
    HackTvLib* m_hackTvLib;
    std::unique_ptr<AudioOutput> audioOutput;

    QThreadPool* m_threadPool;
    QTimer *logTimer;
    QString m_sSettingsFile;
    QStringList pendingLogs;

    qint64 m_frequency;
    int m_sampleRate;
    int m_volumeLevel = 10;
    int m_lnaGain = 20;
    int m_vgaGain = 30;
    int m_txAmpGain = 47;
    int m_rxAmpGain = 11;

    QString mode;
    bool isTx, isFmTransmit, isFmFile, isFile, isTest, isFFmpeg;

    float audioGain = 0.75f;
    float rxGain = 1.0f;
    float rxModIndex = 2.0f;
    int rxDeemph = 0;
    int m_LowCutFreq = -120e3;
    int m_HiCutFreq = 120e3;
    int m_CutFreq = 120e3;
    int flo = -5000;
    int fhi = 5000;
    int click_res = 100;
    int fftrate = 50;

    QFrame* tx_line;
    float tx_amplitude = 0.50;       // HackRfRadio default: 0.50
    float tx_filter_size = 0;
    float tx_modulation_index = 0.40; // HackRfRadio default: 0.40
    float tx_interpolation = 48;
    double transitionWidth = 50e3;
    double quadratureRate = 480e3;
    int audioDecimation = 12;
    int interpolation = 4;
    int decimation = 2;

    std::atomic<bool> m_shuttingDown{false};
    std::atomic<bool> m_isProcessing;
    bool m_initDone = false;  // guard: prevent saveSettings during constructor
    QAtomicInt m_fftUpdatePending{0};  // Frame-drop: skip queuing if update already pending

    std::unique_ptr<LowPassFilter> lowPassFilter;
    std::unique_ptr<RationalResampler> rationalResampler;
    std::unique_ptr<FMDemodulator> fmDemodulator;
    std::unique_ptr<WBFMDemodulator> wbfmDemodulator;
    QImage currentFrame;

    // GUI-side audio capture for FM TX (replaces PortAudio in DLL)
    QAudioSource* m_micSource = nullptr;
    QIODevice* m_micDevice = nullptr;
    QTimer* m_micFlushTimer = nullptr;
    QTimer* m_filePlayTimer = nullptr;
    QAudioDecoder* m_audioDecoder = nullptr;
    std::vector<float> m_fileAudioData;
    size_t m_filePlayPos = 0;
    bool m_fileLoop = true;

    void startMicCapture();
    void stopMicCapture();
    void startFilePlayback(const QString& filePath);
    void startFilePlaybackTimer();
    void stopFilePlayback();

protected:
    void closeEvent(QCloseEvent *event) override;

};

#endif // MAINWINDOW_H
