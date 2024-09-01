#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextBrowser>
#include <QCheckBox>
#include <QTimer>
#include "hacktvlib.h"
#include "audiooutput.h"

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

    void testSineWaveAudio()
    {
        const int sampleRate = 44100;  // Adjust to match your audio output sample rate
        const float frequency = 440.0f;  // 440 Hz, A4 note
        const float amplitude = 0.5f;  // 50% amplitude
        const float duration = 1.0f;  // 1 second duration
        const int numSamples = static_cast<int>(sampleRate * duration);

        QByteArray audioData;
        audioData.reserve(numSamples * sizeof(float));

        for (int i = 0; i < numSamples; ++i)
        {
            float t = static_cast<float>(i) / sampleRate;
            float sample = amplitude * std::sin(2 * M_PI * frequency * t);

            // Append the float sample directly to the QByteArray
            audioData.append(reinterpret_cast<const char*>(&sample), sizeof(float));
        }

        // Output the audio data
        audioOutput->writeBuffer(audioData);
        qDebug() << "Sine wave audio output:" << audioData.size() / sizeof(float) << "samples.";
    }

    std::vector<float> design_lowpass_filter(float cutoff, float sample_rate, int num_taps) {
        std::vector<float> h(num_taps);
        float w_c = 2 * M_PI * cutoff / sample_rate;
        for (int i = 0; i < num_taps; i++) {
            if (i == num_taps / 2) {
                h[i] = w_c / M_PI;
            } else {
                h[i] = std::sin(w_c * (i - num_taps / 2)) / (M_PI * (i - num_taps / 2));
            }
            h[i] *= 0.54 - 0.46 * std::cos(2 * M_PI * i / (num_taps - 1));  // Hamming window
        }
        float sum = std::accumulate(h.begin(), h.end(), 0.0f);
        for (float& coeff : h) {
            coeff /= sum;
        }
        return h;
    }


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
    AudioOutput *audioOutput{};

    QTextBrowser *logBrowser;
    QTimer *logTimer;
    QStringList pendingLogs;    

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
