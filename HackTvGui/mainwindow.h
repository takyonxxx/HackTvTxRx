#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextBrowser>
#include <QCheckBox>
#include <QDockWidget>
#include <QTimer>
#include "hacktvlib.h"
#include "audiooutput.h"
#include "fmdemodulator.h"
#include "audiooutput.h"
#include "lowpassfilter.h"
#include "rationalresampler.h"

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
    QGroupBox *modeGroup;
    QGroupBox *inputTypeGroup;

    QLineEdit *frequencyEdit;
    QLineEdit *sampleRateEdit;
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
    QLineEdit *gainEdit;
    std::unique_ptr<HackTvLib> m_hackTvLib;

    QTextBrowser *logBrowser;
    QTimer *logTimer;
    QStringList pendingLogs;

    std::unique_ptr<LowPassFilter> lowPassFilter;
    std::unique_ptr<FMDemodulator> fmDemodulator;
    std::unique_ptr<AudioOutput> audioOutput;
    std::unique_ptr<RationalResampler> rationalResampler;
    float audioGain = 0.75f;

    std::atomic<bool> m_isProcessing;

    void setupUi();
    QStringList buildCommand();
    void handleLog(const std::string& logMessage);
    void handleReceivedData(const int8_t *data, size_t len);

private slots:
    void executeCommand();
    void chooseFile();
    void updateLogDisplay();
    void onInputTypeChanged(int index);
    void onRxTxTypeChanged(int index);
    void populateChannelCombo();
    void onChannelChanged(int index);
    void processReceivedData(const int8_t *data, size_t len);
};

#endif // MAINWINDOW_H
