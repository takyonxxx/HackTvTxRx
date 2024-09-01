#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextBrowser>
#include <QCheckBox>
#include <QTimer>
#include <complex>
#include "hacktvlib.h"

class QGroupBox;
class QLineEdit;
class QComboBox;
class QPushButton;
class QFileDialog;

class FMDemodulator {
    float last_phase;
    float phase_accumulator;
public:
    FMDemodulator() : last_phase(0), phase_accumulator(0) {}

    std::vector<float> demodulate(const std::vector<std::complex<float>>& input) {
        std::vector<float> output(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            float phase = std::arg(input[i]);
            float delta_phase = phase - last_phase;
            last_phase = phase;

            // Faz farkını -π ile π arasında normalize et
            if (delta_phase > M_PI) delta_phase -= 2 * M_PI;
            if (delta_phase < -M_PI) delta_phase += 2 * M_PI;

            // Faz akümülatörünü sınırla
            phase_accumulator += delta_phase;
            if (phase_accumulator > M_PI) phase_accumulator -= 2 * M_PI;
            if (phase_accumulator < -M_PI) phase_accumulator += 2 * M_PI;

            output[i] = delta_phase; // Faz farkını kullan, akümülatörü değil
        }
        return output;
    }
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);

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
    FMDemodulator m_demodulator;

    void setupUi();
    QStringList buildCommand();
    void handleLog(const std::string& logMessage);
    void handleReceivedData(const int16_t* data, size_t samples);

private slots:
    void executeCommand();
    void chooseFile();
    void updateLogDisplay();
    void onInputTypeChanged(int index);
    void onRxTxTypeChanged(int index);
    void populateChannelCombo();
    void onChannelChanged(int index);
    void processReceivedData(const QVector<int16_t>& data);
};

#endif // MAINWINDOW_H
