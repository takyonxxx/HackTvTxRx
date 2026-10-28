#ifndef QTAVSOURCE_H
#define QTAVSOURCE_H

#include <QObject>
#include <QImage>
#include <QMediaPlayer>
#include <QMediaCaptureSession>
#include <QVideoSink>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QAudioBufferOutput>
#endif
#include <QAudioFormat>
#include <QTimer>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstdint>

#include "core/SpscRing.h"

extern "C" {
#include "hacktv/av.h"
}

class QCamera;
class QAudioSource;
class QIODevice;
class QAudioDecoder;
class QAudioBuffer;

/*
 * Live A/V source for the hacktv encoder, built on Qt Multimedia instead of
 * FFmpeg. Two inputs share the same frame store and audio ring:
 *
 *   video file:  QMediaPlayer        -> QVideoSink         -> latest frame (RGB32, encoder size)
 *                                    -> QAudioBufferOutput -> ring (32 kHz stereo int16)
 *   USB camera:  QCamera / QMediaCaptureSession -> QVideoSink -> latest frame
 *                QAudioSource (the camera's microphone, converted here) -> ring
 *
 *   TX thread:   hacktv calls read_video()/read_audio() through av_t
 *
 * The input runs in real time; the encoder takes the newest frame for every
 * TV frame it builds (frames are duplicated or skipped as the rates differ)
 * and pulls audio from the ring, so the two stay in step with the HackRF clock.
 */
class QtAvSource : public QObject
{
    Q_OBJECT
public:
    explicit QtAvSource(QObject* parent = nullptr);
    ~QtAvSource() override;

    /* Installs the callbacks into `av` and starts playing `file`. GUI thread. */
    bool open(av_t* av, const QString& file, bool loop);

    /* Installs the callbacks into `av` and starts the camera `cameraId`
     * (QCameraDevice::id()); `audioId` is a QAudioDevice::id() or empty for
     * silence. GUI thread. */
    bool openCamera(av_t* av, const QByteArray& cameraId, const QByteArray& audioId);

    /* Installs the callbacks into `av` and shows a still picture (the test
     * card) with a 1 kHz stereo tone. GUI thread. */
    bool openStill(av_t* av, const QImage& picture);

    /* Sound sent with the test card. */
    enum class TestSound { Pips, Silent, Tone };

    /* Test card with a live 24 h clock (re-drawn every second). GUI thread. */
    bool openTestCard(av_t* av, const QString& label, TestSound sound);

    void close();

    bool isOpen() const { return m_open; }
    bool isFile() const { return m_open && !m_isCamera && !m_isStill; }

    /* File playback position (ms); seek() jumps the running transmission. GUI thread. */
    qint64 position() const { return m_player.position(); }
    qint64 duration() const { return m_player.duration(); }
    void   seek(qint64 ms);
    bool ended() const { return m_ended.load(); }
    QString errorString() const { return m_error; }

signals:
    void previewFrame(const QImage& frame);     // for the panel's preview (throttled)
    void positionChanged(qint64 ms);            // file playback only
    void durationChanged(qint64 ms);
    void playbackError(const QString& message);

private:
    static int     cbReadVideo(void* ctx, av_frame_t* frame);
    static int16_t* cbReadAudio(void* ctx, size_t* samples);
    static int     cbEof(void* ctx);
    static int     cbClose(void* ctx);

    bool prepare(av_t* av);
    void onVideoFrame(const QVideoFrame& frame);
    void onAudioBuffer(const QAudioBuffer& buffer);
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    void onDecoderBuffer();                     // Qt 6.5 file-audio path
#endif
    void onMicReadyRead();
    bool startMicrophone(const QByteArray& audioId);
    void stopMicrophone();

    QMediaPlayer m_player;
    QVideoSink m_sink;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QAudioBufferOutput* m_audioOut = nullptr;   // format is fixed at construction
#else
    // Qt 6.5 has no audio tap on QMediaPlayer, so the file's audio is decoded
    // separately into one PCM buffer and replayed in step with the video clock.
    QAudioDecoder* m_decoder = nullptr;
    std::vector<int16_t> m_fileAudioPcm;        // whole-file PCM, 32 kHz stereo int16 interleaved
    std::mutex m_fileAudioMutex;
    std::atomic<qint64> m_fileCursor{0};        // next output frame; resynced to player position()
#endif
    QAudioFormat m_audioFormat;                 // 32 kHz stereo int16 (what hacktv wants)

    // Camera path
    QMediaCaptureSession m_capture;
    QCamera* m_camera = nullptr;
    QAudioSource* m_mic = nullptr;
    QIODevice* m_micIo = nullptr;
    QAudioFormat m_micFormat;                   // whatever the microphone delivers
    double m_micPos = 0.0;                      // fractional resampler position
    float m_micPrevL = 0.0f, m_micPrevR = 0.0f;
    std::vector<float> m_micFrames;             // decoded stereo float frames of one read
    std::vector<int16_t> m_micOut;

    int m_width = 0, m_height = 0;
    bool m_loop = false;
    bool m_open = false;
    bool m_isCamera = false;
    bool m_isStill = false;
    QTimer m_clockTimer;
    QString m_cardLabel;
    int m_cardSecond = -1;
    void setStillImage(const QImage& picture);
    double m_tonePhase = 0.0;
    TestSound m_testSound = TestSound::Pips;
    qint64 m_audioStartMs = 0;       // wall clock (ms since midnight) at the first audio sample
    qint64 m_audioSamples = 0;       // audio frames produced so far (test card)
    QString m_error;

    std::mutex m_frameMutex;
    QImage m_latest;                     // RGB32, m_width x m_height, letterboxed
    std::vector<uint32_t> m_framebuffer; // handed to hacktv (TX thread owns it between frames)
    std::atomic<bool> m_ended{false};
    qint64 m_lastPreviewMs = 0;

    SpscRing<int16_t> m_audioRing{1u << 18};    // 128k stereo frames = 4 s at 32 kHz
    std::vector<int16_t> m_audioChunk;
};

#endif // QTAVSOURCE_H
