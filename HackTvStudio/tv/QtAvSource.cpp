#include "QtAvSource.h"

#include <QAudioBuffer>
#include <QAudioDevice>
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
#include <QAudioDecoder>
#endif
#include <QAudioSource>
#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QDateTime>
#include <QTime>
#include "TestCard.h"
#include <QDebug>
#include <QIODevice>
#include <QMediaDevices>
#include <QPainter>
#include <QUrl>
#include <QVideoFrame>
#include <algorithm>
#include <cmath>
#include <cstring>

QtAvSource::QtAvSource(QObject* parent)
    : QObject(parent)
{
    m_audioFormat.setSampleRate(32000);     // HACKTV_AUDIO_SAMPLE_RATE
    m_audioFormat.setChannelCount(2);
    m_audioFormat.setSampleFormat(QAudioFormat::Int16);

    m_player.setVideoSink(&m_sink);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    m_audioOut = new QAudioBufferOutput(m_audioFormat, this);   // Qt resamples to 32 kHz stereo int16
    m_player.setAudioBufferOutput(m_audioOut);
    connect(m_audioOut, &QAudioBufferOutput::audioBufferReceived, this, &QtAvSource::onAudioBuffer);
#else
    // Qt 6.5: decode the file's audio track ourselves (QMediaPlayer has no tap).
    m_decoder = new QAudioDecoder(this);
    m_decoder->setAudioFormat(m_audioFormat);
    connect(m_decoder, &QAudioDecoder::bufferReady, this, &QtAvSource::onDecoderBuffer);
    // (No error-signal hook: on an unsupported track the decoder stays silent and
    //  video still plays. Qt 6.5's error signal name differs across patch levels.)
#endif
    connect(&m_sink, &QVideoSink::videoFrameChanged, this, &QtAvSource::onVideoFrame);
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
    connect(&m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) { if (isFile()) emit positionChanged(ms); });
    m_clockTimer.setInterval(100);
    connect(&m_clockTimer, &QTimer::timeout, this, [this] {
        const QTime now = QTime::currentTime();
        if (now.second() == m_cardSecond) return;
        m_cardSecond = now.second();
        setStillImage(TestCard::render(m_width, m_height, m_cardLabel, now.toString("HH:mm:ss")));
    });
    connect(&m_player, &QMediaPlayer::durationChanged, this, [this](qint64 ms) { if (isFile()) emit durationChanged(ms); });
}

void QtAvSource::seek(qint64 ms)
{
    if (!isFile()) return;
    m_audioRing.clear();            // drop sound that belongs to the old position
    m_player.setPosition(std::max<qint64>(0, ms));
}

QtAvSource::~QtAvSource()
{
    close();
}

/* Shared part of open()/openCamera(): frame store, ring and hacktv callbacks. */
bool QtAvSource::prepare(av_t* av)
{
    close();
    if (!av || av->width <= 0 || av->height <= 0) return false;

    m_width = av->width;
    m_height = av->height;
    m_ended.store(false);
    m_error.clear();
    m_audioRing.clear();
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    if (m_decoder) m_decoder->stop();
    {
        std::lock_guard<std::mutex> lk(m_fileAudioMutex);
        m_fileAudioPcm.clear();
    }
    m_fileCursor.store(0);
#endif
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
    return true;
}

bool QtAvSource::openStill(av_t* av, const QImage& picture)
{
    if (!prepare(av)) return false;
    m_loop = true;
    m_isCamera = false;
    m_isStill = true;
    m_tonePhase = 0.0;

    setStillImage(picture);
    m_open = true;
    return true;
}

void QtAvSource::setStillImage(const QImage& picture)
{
    QImage img = picture.convertToFormat(QImage::Format_RGB32);
    if (img.size() != QSize(m_width, m_height))
        img = img.scaled(m_width, m_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    {
        std::lock_guard<std::mutex> lk(m_frameMutex);
        m_latest = img;
    }
    emit previewFrame(img);
}

bool QtAvSource::openTestCard(av_t* av, const QString& label, TestSound sound)
{
    const QTime now = QTime::currentTime();
    m_cardLabel = label;
    m_cardSecond = now.second();
    m_testSound = sound;
    m_audioStartMs = now.msecsSinceStartOfDay();
    m_audioSamples = 0;
    if (!openStill(av, TestCard::render(av->width, av->height, label, now.toString("HH:mm:ss")))) return false;
    m_clockTimer.start();
    return true;
}

bool QtAvSource::open(av_t* av, const QString& file, bool loop)
{
    if (!prepare(av)) return false;
    m_loop = loop;
    m_isCamera = false;
    m_isStill = false;

    // No speaker output on purpose (audio goes to the encoder only)
    m_player.setVideoSink(&m_sink);
    m_player.setLoops(loop ? QMediaPlayer::Infinite : 1);
    m_player.setSource(QUrl::fromLocalFile(file));
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    // Decode the audio track up front; it outpaces real-time playback and fills
    // m_fileAudioPcm, which cbReadAudio replays in step with the player clock.
    m_decoder->setSource(QUrl::fromLocalFile(file));
    m_decoder->start();
#endif
    m_player.play();
    m_open = true;
    return true;
}

bool QtAvSource::openCamera(av_t* av, const QByteArray& cameraId, const QByteArray& audioId)
{
    if (!prepare(av)) return false;
    m_loop = true;          // a camera never ends
    m_isCamera = true;
    m_isStill = false;

    QCameraDevice dev;
    for (const QCameraDevice& d : QMediaDevices::videoInputs())
        if (d.id() == cameraId) { dev = d; break; }
    if (dev.isNull()) {
        m_error = cameraId.isEmpty() ? "No camera selected" : "Camera not found (unplugged?)";
        return false;
    }

    m_camera = new QCamera(dev, this);

    // Pick the capture format closest to the TV picture: at least the active
    // height when possible, the fewest pixels beyond that, 25 fps or better.
    QCameraFormat best;
    long long bestScore = -1;
    for (const QCameraFormat& f : dev.videoFormats()) {
        const QSize r = f.resolution();
        if (r.isEmpty() || f.maxFrameRate() < 24.0f) continue;
        long long score = static_cast<long long>(r.width()) * r.height();
        if (r.height() < m_height) score += 100000000LL;             // too small: last resort
        if (f.pixelFormat() == QVideoFrameFormat::Format_Jpeg) score += 1;   // prefer raw at equal size
        if (bestScore < 0 || score < bestScore) { bestScore = score; best = f; }
    }
    if (!best.isNull()) m_camera->setCameraFormat(best);

    connect(m_camera, &QCamera::errorOccurred, this, [this](QCamera::Error, const QString& msg) {
        m_error = msg;
        emit playbackError(msg);
    });

    m_player.setVideoSink(nullptr);          // the sink now belongs to the capture session
    m_capture.setCamera(m_camera);
    m_capture.setVideoSink(&m_sink);
    m_camera->start();

    if (!audioId.isEmpty() && !startMicrophone(audioId))
        qWarning() << "QtAvSource: camera microphone unavailable, transmitting silence";

    m_open = true;
    return true;
}

void QtAvSource::close()
{
    if (!m_open) return;
    if (m_isStill) {
        m_clockTimer.stop();
    } else if (m_isCamera) {
        stopMicrophone();
        if (m_camera) {
            m_camera->stop();
            m_capture.setCamera(nullptr);
            m_capture.setVideoSink(nullptr);
            delete m_camera;
            m_camera = nullptr;
        }
        m_player.setVideoSink(&m_sink);
    } else {
        m_player.stop();
        m_player.setSource(QUrl());
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
        if (m_decoder) m_decoder->stop();
#endif
    }
    m_open = false;
}

// ---- Camera microphone -------------------------------------------------------

bool QtAvSource::startMicrophone(const QByteArray& audioId)
{
    QAudioDevice dev;
    for (const QAudioDevice& d : QMediaDevices::audioInputs())
        if (d.id() == audioId) { dev = d; break; }
    if (dev.isNull()) return false;

    // Ask for what hacktv wants; otherwise take the device's own format and convert here
    m_micFormat = m_audioFormat;
    if (!dev.isFormatSupported(m_micFormat)) {
        m_micFormat = dev.preferredFormat();
        if (!m_micFormat.isValid()) return false;
    }

    m_mic = new QAudioSource(dev, m_micFormat, this);
    m_mic->setBufferSize(m_micFormat.bytesForDuration(200000));   // 200 ms
    m_micPos = 0.0;
    m_micPrevL = m_micPrevR = 0.0f;
    m_micIo = m_mic->start();
    if (!m_micIo) { delete m_mic; m_mic = nullptr; return false; }
    connect(m_micIo, &QIODevice::readyRead, this, &QtAvSource::onMicReadyRead);
    return true;
}

void QtAvSource::stopMicrophone()
{
    if (m_mic) {
        m_mic->stop();
        delete m_mic;
        m_mic = nullptr;
    }
    m_micIo = nullptr;
}

/* GUI thread: decode whatever the microphone delivers into stereo float,
 * resample linearly to 32 kHz and push int16 stereo into the ring. */
void QtAvSource::onMicReadyRead()
{
    if (!m_micIo || !m_open) return;
    const QByteArray raw = m_micIo->readAll();
    const int ch = m_micFormat.channelCount();
    const int bps = m_micFormat.bytesPerSample();
    if (ch <= 0 || bps <= 0) return;
    const int frames = raw.size() / (ch * bps);
    if (frames <= 0) return;

    m_micFrames.resize(static_cast<size_t>(frames) * 2);
    const char* p = raw.constData();
    auto sample = [&](int frame, int c) -> float {
        const char* s = p + (static_cast<size_t>(frame) * ch + c) * bps;
        switch (m_micFormat.sampleFormat()) {
        case QAudioFormat::UInt8: return (static_cast<float>(*reinterpret_cast<const uint8_t*>(s)) - 128.0f) / 128.0f;
        case QAudioFormat::Int16: return static_cast<float>(*reinterpret_cast<const int16_t*>(s)) / 32768.0f;
        case QAudioFormat::Int32: return static_cast<float>(*reinterpret_cast<const int32_t*>(s)) / 2147483648.0f;
        case QAudioFormat::Float: return *reinterpret_cast<const float*>(s);
        default: return 0.0f;
        }
    };
    for (int i = 0; i < frames; ++i) {
        const float l = sample(i, 0);
        const float r = ch > 1 ? sample(i, 1) : l;
        m_micFrames[2 * i]     = l;
        m_micFrames[2 * i + 1] = r;
    }

    const double step = static_cast<double>(m_micFormat.sampleRate()) / 32000.0;   // input frames per output frame
    m_micOut.clear();
    double pos = m_micPos;
    while (pos < frames) {
        const int idx = static_cast<int>(pos);
        const float frac = static_cast<float>(pos - idx);
        const float aL = idx == 0 ? m_micPrevL : m_micFrames[2 * (idx - 1)];
        const float aR = idx == 0 ? m_micPrevR : m_micFrames[2 * (idx - 1) + 1];
        const float bL = m_micFrames[2 * idx];
        const float bR = m_micFrames[2 * idx + 1];
        const float l = std::clamp(aL + (bL - aL) * frac, -1.0f, 1.0f);
        const float r = std::clamp(aR + (bR - aR) * frac, -1.0f, 1.0f);
        m_micOut.push_back(static_cast<int16_t>(std::lrint(l * 32767.0f)));
        m_micOut.push_back(static_cast<int16_t>(std::lrint(r * 32767.0f)));
        pos += step;
    }
    m_micPos = pos - frames;
    m_micPrevL = m_micFrames[2 * (frames - 1)];
    m_micPrevR = m_micFrames[2 * (frames - 1) + 1];

    if (!m_micOut.empty())
        m_audioRing.write(m_micOut.data(), m_micOut.size());   // dropped if the ring is full
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

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
/* GUI thread (Qt 6.5): collect the decoded file audio into one growing PCM
 * buffer. Decoding runs ahead of playback, so the buffer is always filled past
 * the player's current position by the time cbReadAudio reaches it. */
void QtAvSource::onDecoderBuffer()
{
    while (m_decoder && m_decoder->bufferAvailable()) {
        const QAudioBuffer buf = m_decoder->read();
        if (!buf.isValid()) continue;
        if (buf.format().sampleFormat() != QAudioFormat::Int16 || buf.format().channelCount() != 2) continue;
        const int16_t* d = buf.constData<int16_t>();
        const int n = buf.sampleCount();           // interleaved int16 count (frames * 2)
        std::lock_guard<std::mutex> lk(m_fileAudioMutex);
        m_fileAudioPcm.insert(m_fileAudioPcm.end(), d, d + n);
    }
}
#endif

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
    if (s->m_isStill) {
        constexpr double TWO_PI = 2.0 * 3.14159265358979;
        if (s->m_testSound == TestSound::Silent) {
            std::fill(s->m_audioChunk.begin(), s->m_audioChunk.end(), int16_t(0));
            s->m_audioSamples += MAX_FRAMES;
            *samples = MAX_FRAMES;
            return s->m_audioChunk.data();
        }
        const double step = TWO_PI * 1000.0 / 32000.0;
        double ph = s->m_tonePhase;
        for (size_t i = 0; i < MAX_FRAMES; ++i) {
            double amp = 0.5;                                   // continuous tone: -6 dBFS
            if (s->m_testSound == TestSound::Pips) {
                // Time signal: short pips at :55..:59, a longer one on the minute, -18 dBFS,
                // 5 ms fades so they do not click. Derived from the wall clock.
                const qint64 ms = s->m_audioStartMs + (s->m_audioSamples + static_cast<qint64>(i)) * 1000 / 32000;
                const int sec = static_cast<int>((ms / 1000) % 60);
                const int msInSec = static_cast<int>(ms % 1000);
                const int pipLen = sec == 0 ? 500 : (sec >= 55 ? 100 : 0);
                if (pipLen == 0 || msInSec >= pipLen) amp = 0.0;
                else amp = 0.125 * std::min(1.0, std::min(msInSec, pipLen - msInSec) / 5.0);
            }
            const int16_t v = static_cast<int16_t>(std::sin(ph) * amp * 32000.0);
            s->m_audioChunk[2 * i] = s->m_audioChunk[2 * i + 1] = v;
            ph += step;
            if (ph > TWO_PI) ph -= TWO_PI;
        }
        s->m_tonePhase = ph;
        s->m_audioSamples += MAX_FRAMES;
        *samples = MAX_FRAMES;
        return s->m_audioChunk.data();
    }
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    if (!s->m_isCamera) {
        // Qt 6.5 file path: replay the decoded PCM, indexed by the player clock
        // so audio tracks video. Advance monotonically for smooth output; resync
        // to position() when they drift (a seek or a loop wrap).
        const qint64 posFrames = s->m_player.position() * 32000 / 1000;
        qint64 cur = s->m_fileCursor.load();
        if (cur < 0 || qAbs(cur - posFrames) > 9600) cur = posFrames;   // > 0.3 s drift
        std::lock_guard<std::mutex> lk(s->m_fileAudioMutex);
        const qint64 avail = static_cast<qint64>(s->m_fileAudioPcm.size()) / 2;
        const qint64 n = std::min<qint64>(MAX_FRAMES, avail - cur);
        if (n <= 0) { *samples = 0; return nullptr; }                   // not decoded yet / ended
        std::memcpy(s->m_audioChunk.data(), &s->m_fileAudioPcm[cur * 2],
                    static_cast<size_t>(n) * 2 * sizeof(int16_t));
        s->m_fileCursor.store(cur + n);
        *samples = static_cast<size_t>(n);
        return s->m_audioChunk.data();
    }
#endif
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
    // The player / camera is stopped by the owner (GUI thread); nothing to free here.
    (void)ctx;
    return AV_OK;
}
