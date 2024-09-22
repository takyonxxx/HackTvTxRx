#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextBrowser>
#include <QCheckBox>
#include <QDockWidget>
#include <QThreadPool>
#include <QTimer>
#include "hacktvlib.h"
#include "audiooutput.h"
#include "fmdemodulator.h"
#include "audiooutput.h"
#include "lowpassfilter.h"
#include "rationalresampler.h"
#include "cplotter.h"
#include "freqctrl.h"
#include "signalprocessor.h"

class QGroupBox;
class QLineEdit;
class QComboBox;
class QPushButton;
class QFileDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
private:
    QThreadPool m_threadPool;

    QGroupBox *modeGroup;
    QGroupBox *inputTypeGroup;

    QLineEdit *frequencyEdit;
    QComboBox *sampleRateCombo;
    QComboBox *outputCombo;
    QComboBox *modeCombo;
    QComboBox *rxtxCombo;
    QComboBox *channelCombo;
    QLineEdit *inputFileEdit;
    QPushButton *chooseFileButton;
    QPushButton *executeButton;
    QPushButton *exitButton;
    QFileDialog *fileDialog;
    QComboBox *inputTypeCombo;
    QLineEdit *ffmpegOptionsEdit;
    QCheckBox *ampEnabled;
    QCheckBox *a2Stereo;
    QCheckBox *repeat;
    QCheckBox *acp;
    QCheckBox *filter;
    QCheckBox *colorDisabled;    
    std::unique_ptr<HackTvLib> m_hackTvLib;

    QString m_sSettingsFile;

    QTextBrowser *logBrowser;
    QTimer *logTimer;
    QStringList pendingLogs;

    std::unique_ptr<LowPassFilter> lowPassFilter;
    std::unique_ptr<FMDemodulator> fmDemodulator;
    std::unique_ptr<AudioOutput> audioOutput;
    std::unique_ptr<RationalResampler> rationalResampler;

    CPlotter *cPlotter;
    CFreqCtrl *freqCtrl;

    float audioGain = 0.5f;
    int m_LowCutFreq = -100e3;
    int m_HiCutFreq = 100e3;
    int flo = -5000;
    int fhi = 5000;
    int click_res = 100;
    int fftrate = 50;

    uint64_t m_frequency;
    uint32_t m_sampleRate;
    float decimation;
    QString mode;
    std::atomic<bool> m_isProcessing;

    SignalProcessor* m_signalProcessor;
    static const int MAX_FFT_SIZE = 2048;
    static const int BUFFER_SIZE = 1048576; // 1 MB

    void setupUi();
    QStringList buildCommand();
    void handleLog(const std::string& logMessage);
    void handleReceivedData(const int8_t *data, size_t len);
    void loadSettings();
    void saveSettings();
    void setCurrentSampleRate(int sampleRate);

private slots:
    void executeCommand();
    void chooseFile();
    void updateLogDisplay();
    void onInputTypeChanged(int index);
    void onRxTxTypeChanged(int index);
    void onSampleRateChanged(int index);
    void populateChannelCombo();
    void onChannelChanged(int index);
    void processReceivedData(const int8_t *data, size_t len);
    void on_plotter_newDemodFreq(qint64 freq, qint64 delta);
    void on_plotter_newFilterFreq(int low, int high);
    void onFreqCtrl_setFrequency(qint64 freq);
    void handleSamples(const std::vector<std::complex<float>>& samples);
    void processFft(const std::vector<std::complex<float>>& samples);
    void processDemod(const std::vector<std::complex<float>>& samples);
    void processAudio(const std::vector<float>& demodulatedSamples);
};

#endif // MAINWINDOW_H
