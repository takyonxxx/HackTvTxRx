#ifndef RADIOTXPANEL_H
#define RADIOTXPANEL_H

#include "ModePanel.h"
#include "dsp/RadioModulator.h"

#include <QAudioDecoder>
#include <QAudioSource>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QTimer>
#include <atomic>
#include <memory>
#include <vector>

/*
 * Radio transmitter: WFM stereo / NFM / AM from the microphone (push-to-talk),
 * an audio file or a test tone. The modulator runs on the TX engine thread;
 * this panel only feeds it audio at 48 kHz.
 */
class RadioTxPanel : public ModePanel
{
    Q_OBJECT
public:
    explicit RadioTxPanel(QWidget* parent = nullptr);
    ~RadioTxPanel() override;

    QString    title() const override { return "RADIO TX"; }
    bool       isTransmit() const override { return true; }
    ITxSource* txSource() override;
    uint32_t   preferredSampleRate() const override { return 2000000; }

    void onActivated() override;
    void onBandChanged(const BandInfo& band) override;
    void onDeviceStarted() override;
    void onDeviceStopped() override;
    void saveSettings(QSettings& s) override;
    void loadSettings(QSettings& s) override;

private slots:
    void updateStatus();

private:
    void buildUi();
    void applyMode();
    void startMic();
    void stopMic();
    void browseFile();
    void decodeFile(const QString& path);

    RadioModulator m_mod;

    // mode
    QRadioButton* m_wfmRadio = nullptr;
    QRadioButton* m_nfmRadio = nullptr;
    QRadioButton* m_amRadio = nullptr;
    QCheckBox* m_followBandCheck = nullptr;
    QDoubleSpinBox* m_devSpin = nullptr;     // kHz (FM)
    QSlider* m_depthSlider = nullptr;        // % (AM)
    QLabel* m_depthLabel = nullptr;
    QComboBox* m_preemphCombo = nullptr;
    QSlider* m_ampSlider = nullptr;
    QLabel* m_ampLabel = nullptr;

    // source
    QRadioButton* m_micRadio = nullptr;
    QRadioButton* m_fileRadio = nullptr;
    QRadioButton* m_toneRadio = nullptr;
    QComboBox* m_micCombo = nullptr;
    QPushButton* m_pttButton = nullptr;
    QLineEdit* m_fileEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QCheckBox* m_loopCheck = nullptr;
    QLabel* m_fileInfo = nullptr;
    QDoubleSpinBox* m_toneSpin = nullptr;
    QSlider* m_gainSlider = nullptr;
    QLabel* m_gainLabel = nullptr;
    QCheckBox* m_agcCheck = nullptr;
    QProgressBar* m_vu = nullptr;
    QLabel* m_statusLabel = nullptr;

    // audio sources
    std::unique_ptr<QAudioSource> m_micSource;
    QIODevice* m_micIo = nullptr;
    QAudioDecoder* m_decoder = nullptr;
    // Shared with the TX thread (pull source). m_fileData is frozen while transmitting.
    std::vector<int16_t> m_fileData;     // stereo interleaved 48 kHz
    std::atomic<size_t> m_filePos{0};
    bool m_fileReady = false;
    std::atomic<int> m_pullKind{0};      // 0 none (mic), 1 file, 2 tone
    std::atomic<bool> m_pullLoop{true};
    std::atomic<float> m_toneHz{1000.0f};
    double m_tonePhase = 0.0;            // TX thread only
    size_t pullAudio(float* stereo, size_t frames);   // TX thread
    void setSourceWidgetsEnabled(bool on);
    QTimer* m_statusTimer = nullptr;
    bool m_running = false;
};

#endif // RADIOTXPANEL_H
