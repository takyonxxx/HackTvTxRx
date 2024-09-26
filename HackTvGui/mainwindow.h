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

#include <memory>
#include <vector>
#include <complex>
#include "hacktvlib.h"
#include "freqctrl.h"
#include "cplotter.h"
#include "meter.h"
#include "audiooutput.h"
#include "signalprocessor.h"
#include "modulator.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

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
    void processReceivedData(const int8_t *data, size_t len);
    void processAudio(const std::vector<float>& demodulatedSamples);
    void onVolumeSliderValueChanged(int value);
    void onLnaSliderValueChanged(int value);
    void onVgaSliderValueChanged(int value);
    void onRxAmpSliderValueChanged(int value);

private:
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

    QVBoxLayout *mainLayout;

    // UI Elements
    QComboBox *outputCombo, *channelCombo, *sampleRateCombo, *rxtxCombo, *inputTypeCombo, *modeCombo;
    QCheckBox *ampEnabled, *colorDisabled;
    QLineEdit *frequencyEdit, *inputFileEdit, *ffmpegOptionsEdit;
    QPushButton *chooseFileButton, *executeButton, *exitButton;
    QSlider *txAmplitudeSlider, *txFilterSizeSlider, *txModulationIndexSlider, *txInterpolationSlider;
    QDoubleSpinBox *txAmplitudeSpinBox, *txFilterSizeSpinBox, *txModulationIndexSpinBox, *txInterpolationSpinBox;
    QSpinBox *txAmpSpinBox;;
    QTextBrowser *logBrowser;
    QLabel *volumeLabel, *volumeLevelLabel, *lnaLabel, *lnaLevelLabel, *vgaLabel, *vgaLevelLabel,
           *rxAmpLabel, *rxAmpLevelLabel;
    QSlider *volumeSlider, *lnaSlider, *vgaSlider, *txAmpSlider, *rxAmpSlider;
    QFileDialog *fileDialog;
    CFreqCtrl *freqCtrl;
    CPlotter *cPlotter;
    CMeter *cMeter;

    QString sliderStyle, labelStyle;    

    // Layouts and Groups
    QGridLayout *txControlsLayout;
    QGroupBox *outputGroup, *inputTypeGroup, *modeGroup, *rxGroup;

    // Member variables
    std::unique_ptr<HackTvLib> m_hackTvLib;
    std::unique_ptr<AudioOutput> audioOutput;

    SignalProcessor *m_signalProcessor;
    QThreadPool m_threadPool;
    QTimer *logTimer;
    QString m_sSettingsFile;
    QStringList pendingLogs;

    qint64 m_frequency;
    int m_sampleRate;
    int m_volumeLevel = 50;
    int m_lnaGain = 40;
    int m_vgaGain = 40;
    int m_txAmpGain = 40;
    int m_rxAmpGain = 0;

    QString mode;
    bool isTx, isFmTransmit, isFile, isTest, isFFmpeg;

    float audioGain = 0.75f;
    int m_LowCutFreq = -75e3;
    int m_HiCutFreq = 75e3;
    int m_CutFreq = 75e3;
    int flo = -5000;
    int fhi = 5000;
    int click_res = 100;
    int fftrate = 50;

    QFrame* tx_line;
    float tx_amplitude = 1.0;
    float tx_filter_size = 0;
    float tx_modulation_index = 5.0;
    float tx_interpolation = 48;
    double transitionWidth = 50e3;
    double quadratureRate = 480e3;
    int audioDecimation = 12;
    int interpolation = 4;
    int decimation = 2;

    int defaultWidth, defaultHeight;
    std::atomic<bool> m_isProcessing;

    std::unique_ptr<LowPassFilter> lowPassFilter;
    std::unique_ptr<RationalResampler> rationalResampler;
    std::unique_ptr<FMDemodulator> fmDemodulator;
};

#endif // MAINWINDOW_H
