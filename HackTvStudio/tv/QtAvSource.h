#ifndef QTAVSOURCE_H
#define QTAVSOURCE_H

#include <QObject>
#include <QImage>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QAudioBufferOutput>
#include <QAudioFormat>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstdint>

#include "core/SpscRing.h"

extern "C" {
#include "hacktv/av.h"
}

/*
 * Video file source for the hacktv encoder, built on Qt Multimedia instead
 * of FFmpeg (Qt's own FFmpeg backend decodes mp4/mkv/avi/...).
 *
 *   GUI thread:  QMediaPlayer -> QVideoSink  -> latest frame (RGB32, encoder size)
 *                             -> QAudioBufferOutput (32 kHz stereo int16) -> ring
 *   TX thread:   hacktv calls read_video()/read_audio() through av_t
 *
 * The player runs in real time; the encoder takes the newest frame for every
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
    void close();

    bool isOpen() const { return m_open; }
    bool ended() const { return m_ended.load(); }
    QString errorString() const { return m_error; }

signals:
    void previewFrame(const QImage& frame);     // for the panel's preview (throttled)
    void playbackError(const QString& message);

private:
    static int     cbReadVideo(void* ctx, av_frame_t* frame);
    static int16_t* cbReadAudio(void* ctx, size_t* samples);
    static int     cbEof(void* ctx);
    static int     cbClose(void* ctx);

    void onVideoFrame(const QVideoFrame& frame);
    void onAudioBuffer(const QAudioBuffer& buffer);

    QMediaPlayer m_player;
    QVideoSink m_sink;
    QAudioBufferOutput* m_audioOut = nullptr;   // format is fixed at construction
    QAudioFormat m_audioFormat;

    int m_width = 0, m_height = 0;
    bool m_loop = false;
    bool m_open = false;
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
