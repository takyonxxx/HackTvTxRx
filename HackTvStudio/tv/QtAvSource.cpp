#include "QtAvSource.h"

#include <QAudioBuffer>
#include <QDateTime>
#include <QDebug>
#include <QPainter>
#include <QUrl>
#include <QVideoFrame>
#include <algorithm>

QtAvSource::QtAvSource(QObject* parent)
    : QObject(parent)
{
    m_audioFormat.setSampleRate(32000);     // HACKTV_AUDIO_SAMPLE_RATE
    m_audioFormat.setChannelCount(2);
    m_audioFormat.setSampleFormat(QAudioFormat::Int16);

    m_audioOut = new QAudioBufferOutput(m_audioFormat, this);   // Qt resamples to 32 kHz stereo int16
    m_player.setVideoSink(&m_sink);
    m_player.setAudioBufferOutput(m_audioOut);
    connect(&m_sink, &QVideoSink::videoFrameChanged, this, &QtAvSource::onVideoFrame);
    connect(m_audioOut, &QAudioBufferOutput::audioBufferReceived, this, &QtAvSource::onAudioBuffer);
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus st) {
        if (st == QMediaPlayer::EndOfMedia && !m_loop) m_ended.store(true);
        if (st == QMediaPlayer::InvalidMedia) {
            m_error = "Invalid or unsupported media";
            emit playbackError(m_error);
        }
    });
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& msg) {
        m_error = msg;
        emit playbackError(msg);
    });
}

QtAvSource::~QtAvSource()
{
    close();
}

bool QtAvSource::open(av_t* av, const QString& file, bool loop)
{
    close();
    if (!av || av->width <= 0 || av->height <= 0) return false;

    m_width = av->width;
    m_height = av->height;
    m_loop = loop;
    m_ended.store(false);
    m_error.clear();
    m_audioRing.clear();
    {
        std::lock_guard<std::mutex> lk(m_frameMutex);
        m_latest = QImage(m_width, m_height, QImage::Format_RGB32);
        m_latest.fill(Qt::black);
    }
    m_framebuffer.assign(static_cast<size_t>(m_width) * m_height, 0u);

    av->av_source_ctx = this;
    av->read_video = &QtAvSource::cbReadVideo;
    av->read_audio = &QtAvSource::cbReadAudio;
    av->eof = &QtAvSource::cbEof;
    av->close = &QtAvSource::cbClose;

    // No speaker output on purpose (audio goes to the encoder only)
    m_player.setLoops(loop ? QMediaPlayer::Infinite : 1);
    m_player.setSource(QUrl::fromLocalFile(file));
    m_player.play();
    m_open = true;
    return true;
}

void QtAvSource::close()
{
    if (!m_open) return;
    m_player.stop();
    m_player.setSource(QUrl());
    m_open = false;
}

// ---- GUI thread ------------------------------------------------------------

void QtAvSource::onVideoFrame(const QVideoFrame& frame)
{
    if (!m_open || !frame.isValid()) return;
    QImage img = frame.toImage();
    if (img.isNull()) return;

    // Letterbox / pillarbox into the encoder's active picture size
    QImage canvas(m_width, m_height, QImage::Format_RGB32);
    canvas.fill(Qt::black);
    const QSize fit = img.size().scaled(m_width, m_height, Qt::KeepAspectRatio);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(QRect((m_width - fit.width()) / 2, (m_height - fit.height()) / 2, fit.width(), fit.height()), img);
    p.end();

    {
        std::lock_guard<std::mutex> lk(m_frameMutex);
        m_latest = canvas;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastPreviewMs >= 80) {        // ~12 fps preview is plenty
        m_lastPreviewMs = now;
        emit previewFrame(canvas);
    }
}

void QtAvSource::onAudioBuffer(const QAudioBuffer& buffer)
{
    if (!m_open || !buffer.isValid()) return;
    if (buffer.format().sampleFormat() != QAudioFormat::Int16 || buffer.format().channelCount() != 2) return;
    const int16_t* d = buffer.constData<int16_t>();
    m_audioRing.write(d, static_cast<size_t>(buffer.sampleCount()));   // dropped if the ring is full
}

// ---- TX thread (hacktv callbacks) -----------------------------------------

int QtAvSource::cbReadVideo(void* ctx, av_frame_t* frame)
{
    QtAvSource* s = static_cast<QtAvSource*>(ctx);
    {
        std::lock_guard<std::mutex> lk(s->m_frameMutex);
        const QImage& img = s->m_latest;
        if (!img.isNull() && img.width() == s->m_width && img.height() == s->m_height) {
            for (int y = 0; y < s->m_height; ++y)
                std::memcpy(&s->m_framebuffer[static_cast<size_t>(y) * s->m_width],
                            img.constScanLine(y), static_cast<size_t>(s->m_width) * 4);
        }
    }
    av_frame_init(frame, s->m_width, s->m_height, s->m_framebuffer.data(), 1, s->m_width);
    frame->pixel_aspect_ratio = rational_t{ 1, 1 };
    frame->interlaced = 0;
    return AV_OK;
}

int16_t* QtAvSource::cbReadAudio(void* ctx, size_t* samples)
{
    QtAvSource* s = static_cast<QtAvSource*>(ctx);
    constexpr size_t MAX_FRAMES = 1024;
    s->m_audioChunk.resize(MAX_FRAMES * 2);
    const size_t got = s->m_audioRing.read(s->m_audioChunk.data(), MAX_FRAMES * 2) / 2;
    if (got == 0) {
        *samples = 0;
        return nullptr;        // hacktv treats NULL as silence
    }
    *samples = got;
    return s->m_audioChunk.data();
}

int QtAvSource::cbEof(void* ctx)
{
    QtAvSource* s = static_cast<QtAvSource*>(ctx);
    return s->m_ended.load() ? 1 : 0;
}

int QtAvSource::cbClose(void* ctx)
{
    // The player is stopped by the owner (GUI thread); nothing to free here.
    (void)ctx;
    return AV_OK;
}
