#ifndef TVRXPANEL_H
#define TVRXPANEL_H

#include "ModePanel.h"
#include "core/RxEngine.h"
#include "core/WorkerQueue.h"
#include "PALDecoder.h"
#include "audiodemodulator.h"
#include "audiooutput.h"
#include "FrameBuffer.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QTimer>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class VideoView;
class TvRecorder;
class QPushButton;

/*
 * PAL-B/G receive pipeline: FrameBuffer -> PALDecoder (video thread)
 *                                        -> AudioDemodulator (audio thread)
 * sinkProcess() runs on the RxEngine thread and only moves data; the two
 * WorkerQueues do the heavy lifting with back-pressure (video frames are
 * dropped when the decoder is behind, audio is never dropped).
 */
class TvRxPipeline : public QObject, public IRxSink
{
    Q_OBJECT
public:
    explicit TvRxPipeline(QObject* parent = nullptr);
    ~TvRxPipeline() override;

    PALDecoder*       decoder()    { return m_decoder; }
    AudioDemodulator* audioDemod() { return m_audioDemod; }
    AudioOutput*      audioOut()   { return m_audioOut.get(); }

    uint64_t droppedFrames() const { return m_droppedFrames.load(); }
    /* Running average of the per-frame processing time (ms), video and audio stages. */
    float videoMsPerFrame() const { return m_videoMs.load(); }
    float audioMsPerFrame() const { return m_audioMs.load(); }

    // IRxSink (RxEngine thread)
    void sinkStart(uint32_t sampleRate) override;
    void sinkProcess(const std::complex<float>* iq, size_t count) override;
    void sinkStop() override;

private:
    PALDecoder*       m_decoder;      // QObject children of the pipeline
    AudioDemodulator* m_audioDemod;
    std::unique_ptr<AudioOutput> m_audioOut;
    std::unique_ptr<FrameBuffer> m_frameBuffer;
    std::vector<std::complex<float>> m_chunk;

    WorkerQueue m_videoWorker{"pal-video", 2};   // absorbs USB bursts; oldest frame dropped when 2 are waiting
    WorkerQueue m_audioWorker{"pal-audio", 10};
    std::atomic<uint64_t> m_droppedFrames{0};
    std::atomic<float> m_videoMs{0.0f};
    std::atomic<float> m_audioMs{0.0f};
};

class TvRxPanel : public ModePanel
{
    Q_OBJECT
public:
    explicit TvRxPanel(QWidget* parent = nullptr);
    ~TvRxPanel() override;

    QString  title() const override { return "TV RX"; }
    IRxSink* rxSink() override { return m_pipe; }
    uint32_t preferredSampleRate() const override { return 16000000; }
    QLayout* spectrumSlot() override { return m_specSlot; }
    bool     meterRange(uint64_t tuneHz, int& lowHz, int& highHz) const override
    {
        // The whole TV channel (7/8 MHz) when the tuning falls inside a known one,
        // otherwise video carrier - 1.25 MHz .. + 6.75 MHz around the tuning.
        const TvChannel ch = BandPlan::tvChannel(tuneHz);
        if (ch.valid) {
            lowHz  = static_cast<int>(static_cast<int64_t>(ch.channelStartHz) - static_cast<int64_t>(tuneHz));
            highHz = static_cast<int>(static_cast<int64_t>(ch.channelStartHz + ch.channelWidthHz) - static_cast<int64_t>(tuneHz));
        } else {
            lowHz = -1250000; highHz = 6750000;
        }
        return true;
    }

    void onActivated() override;
    void onFrequencyChanged(uint64_t hz) override;
    void onSampleRateChanged(uint32_t hz) override;
    void onDeviceStarted() override;
    void onDeviceStopped() override;

    void saveSettings(QSettings& s) override;
    void loadSettings(QSettings& s) override;

private slots:
    void onFrameReady(const QImage& frame);
    void onSyncStatsUpdated(float syncRate, float peakLevel, float minLevel);
    void onAudioReady(const std::vector<float>& samples);
    void updateStatus();
    void toggleFullscreen();
    void toggleRecording();

private:
    void buildUi();
    QWidget* buildVideoProcessingGroup();
    QWidget* buildAudioGroup();
    QWidget* buildSyncGroup();
    QWidget* buildRecordGroup();

    TvRxPipeline* m_pipe;
    TvRecorder* m_recorder = nullptr;
    QPushButton* m_recButton = nullptr;
    QLabel* m_recLabel = nullptr;

    VideoView* m_video = nullptr;
    VideoView* m_fullscreen = nullptr;
    QGroupBox* m_videoGroup = nullptr;
    QVBoxLayout* m_specSlot = nullptr;

    QSlider* m_videoGainSlider = nullptr;
    QDoubleSpinBox* m_videoGainSpin = nullptr;
    QSlider* m_videoOffsetSlider = nullptr;
    QDoubleSpinBox* m_videoOffsetSpin = nullptr;
    QCheckBox* m_invertCheck = nullptr;
    QCheckBox* m_colorCheck = nullptr;
    QSlider* m_chromaSlider = nullptr;
    QDoubleSpinBox* m_chromaSpin = nullptr;
    QCheckBox* m_wideCheck = nullptr;

    QCheckBox* m_audioEnabledCheck = nullptr;
    QSlider* m_audioGainSlider = nullptr;
    QDoubleSpinBox* m_audioGainSpin = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QLabel* m_volumeLabel = nullptr;

    QLabel* m_syncRateLabel = nullptr;
    QSlider* m_syncThresholdSlider = nullptr;
    QDoubleSpinBox* m_syncThresholdSpin = nullptr;
    QCheckBox* m_autoSyncCheck = nullptr;
    QCheckBox* m_afcCheck = nullptr;
    QCheckBox* m_syncDemodCheck = nullptr;
    QCheckBox* m_vsbCheck = nullptr;
    QCheckBox* m_denoiseCheck = nullptr;

    QTimer* m_statusTimer = nullptr;
    QElapsedTimer m_fpsTimer;
    int m_frameCount = 0;
    bool m_running = false;
    uint64_t m_frequency = 479250000ULL;
    uint32_t m_sampleRate = 16000000;
};

#endif // TVRXPANEL_H
