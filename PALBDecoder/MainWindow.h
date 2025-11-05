#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QTimer>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QMessageBox>
#include <QMutex>
#include <QThreadPool>
#include <QElapsedTimer>
#include <atomic>
#include <memory>

#include "PALDecoder.h"
#include "audiooutput.h"
#include "AudioDemodulator.h"
#include "FrameBuffer.h"

class HackTvLib;

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

    void handleReceivedData(const int8_t* data, size_t len);
    void processDemod(const std::vector<std::complex<float>>& samples);

private slots:
    void onFrameReady(const QImage& frame);
    void onVideoGainChanged(int value);
    void onVideoOffsetChanged(int value);
    void onLnaGainChanged(int value);
    void onVgaGainChanged(int value);
    void onRxAmpGainChanged(int value);
    void updateStatus();
    void toggleHackRF();
    void onFrequencySliderChanged(int value);
    void onFrequencySpinBoxChanged(double value);
    void updateChannelLabel(uint64_t frequency);
    void onInvertVideoChanged(int state);
    void onSyncThresholdChanged(int value);
    void onSyncStatsUpdated(float syncRate, float peakLevel, float minLevel);
    void onSampleRateChanged(int index);
    void onAudioGainChanged(int value);
    void onAudioEnabledChanged(int state);
    void onAudioReady(const std::vector<float>& audioSamples);

private:
    void setupUI();
    void initHackRF();
    void applyFrequencyChange();
    void startPalVideoProcessing(std::shared_ptr<std::vector<std::complex<float>>> framePtr);
    void startPalAudioProcessing(std::shared_ptr<std::vector<std::complex<float>>> framePtr);

    QThreadPool* m_threadPool;

    // UI Components
    QLabel* m_videoLabel;
    QLabel* m_statusLabel;
    QLabel* m_fpsLabel;
    QSlider* m_videoGainSlider;
    QSlider* m_videoOffsetSlider;
    QDoubleSpinBox* m_videoGainSpinBox;
    QDoubleSpinBox* m_videoOffsetSpinBox;

    // HackRF Gain Controls
    QSpinBox* m_lnaGainSpinBox;
    QSpinBox* m_vgaGainSpinBox;
    QSpinBox* m_rxAmpGainSpinBox;
    QSlider* m_lnaGainSlider;
    QSlider* m_vgaGainSlider;
    QSlider* m_rxAmpGainSlider;

    // Audio Controls
    std::unique_ptr<AudioDemodulator> m_audioDemodulator;
    std::unique_ptr<AudioOutput> m_audioOutput;
    QSlider* m_audioGainSlider;
    QDoubleSpinBox* m_audioGainSpinBox;
    QCheckBox* m_audioEnabledCheckBox;

    QPushButton* m_startStopButton;
    QCheckBox* m_invertVideoCheckBox;
    QComboBox* m_sampleRateComboBox;

    // Core components
    std::shared_ptr<PALDecoder> m_palDecoder;
    std::unique_ptr<HackTvLib> m_hackTvLib;

    // Status tracking
    QTimer* m_statusTimer;
    int m_frameCount;
    QElapsedTimer m_fpsTimer;

    // Thread safety
    std::atomic<bool> m_shuttingDown;
    std::atomic<bool> m_hackRfRunning;

    // Current frame
    QImage m_currentFrame;
    QMutex m_frameMutex;

    uint64_t m_lastDroppedFrames = 0;

    // Frequency control
    QSlider* m_frequencySlider;
    QDoubleSpinBox* m_frequencySpinBox;
    QLabel* m_channelLabel;

    uint64_t m_currentFrequency;
    int m_currentSampleRate;

    QSlider* m_syncThresholdSlider;
    QDoubleSpinBox* m_syncThresholdSpinBox;
    QLabel* m_syncRateLabel;

    FrameBuffer* palFrameBuffer;
    QAtomicInt palDemodulationInProgress{0};
    QAtomicInt audioDemodulationInProgress{0};

    QMutex m_audioQueueMutex;
    std::deque<std::vector<std::complex<float>>> m_audioQueue;
    static constexpr size_t MAX_AUDIO_QUEUE = 10; // Max 10 chunk

    // Constants
    static constexpr uint64_t UHF_MIN_FREQ = 470000000ULL;
    static constexpr uint64_t UHF_MAX_FREQ = 862000000ULL;
    static constexpr uint64_t DEFAULT_FREQ = 478000000ULL;
    static constexpr uint32_t SAMP_RATE = 16000000;
};

#endif // MAINWINDOW_H
