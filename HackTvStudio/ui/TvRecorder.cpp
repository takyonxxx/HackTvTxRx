#include "TvRecorder.h"

#include <QAudioBuffer>
#include <QDebug>
#include <QMediaFormat>
#include <QUrl>
#include <QVideoFrame>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QAudioBufferInput>
#include <QVideoFrameInput>
#endif
#include <algorithm>
#include <cmath>

TvRecorder::TvRecorder(QObject* parent)
    : QObject(parent)
{
    connect(&m_recorder, &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString& msg) {
                qWarning() << "[TvRecorder]" << msg;
                emit errorOccurred(msg);
                if (m_recording) stop();
            });
}

TvRecorder::~TvRecorder()
{
    stop();
}

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)

// Qt 6.5 (older head units) has no QVideoFrameInput / QAudioBufferInput, so an
// app-fed media recorder is not available. The receiver still works; only the
// record-to-mp4 button is disabled.
bool TvRecorder::start(const QString&, const QSize&, int, int)
{
    m_codecInfo = "kayit yok";
    emit errorOccurred("Kayit bu surumde (Qt 6.5 / eski Android) desteklenmiyor");
    return false;
}

void TvRecorder::pushFrame(const QImage&) {}
void TvRecorder::pushAudio(const std::vector<float>&) {}

#else

bool TvRecorder::start(const QString& filePath, const QSize& videoSize, int fps, int audioRate)
{
    if (m_recording) return false;

    m_videoSize = videoSize;
    m_fps = fps;
    m_path = filePath;
    m_audioSamplesSent = 0;
    m_audioBaseUs = -1;
    m_audioBuffersSent = 0;
    m_audioBuffersRejected = 0;
    m_framesSent = 0;
    m_framesDropped = 0;

    m_audioFormat.setSampleRate(audioRate);
    m_audioFormat.setChannelCount(1);
    m_audioFormat.setSampleFormat(QAudioFormat::Int16);

    // Fresh inputs for every recording (the session takes non-owning pointers)
    delete m_videoIn;
    delete m_audioIn;
    m_videoIn = new QVideoFrameInput(this);
    // No format hint on purpose: with a hint Qt 6.11 never emits readyToSendAudioBuffer
    // (every sendAudioBuffer() fails and the track is dropped). Without it the encoder
    // initialises from the first buffer we send, which carries m_audioFormat.
    m_audioIn = new QAudioBufferInput(this);
    m_session.setVideoFrameInput(m_videoIn);
    m_session.setAudioBufferInput(m_audioIn);
    m_session.setRecorder(&m_recorder);

    // Prefer MP4 / H.264 / AAC; fall back to whatever this platform can encode
    QMediaFormat fmt(QMediaFormat::MPEG4);
    fmt.setVideoCodec(QMediaFormat::VideoCodec::H264);
    fmt.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    if (!fmt.isSupported(QMediaFormat::Encode)) {
        fmt = QMediaFormat(QMediaFormat::MPEG4);
        fmt.resolveForEncoding(QMediaFormat::RequiresVideo);
        if (!fmt.isSupported(QMediaFormat::Encode)) {
            fmt = QMediaFormat();
            fmt.resolveForEncoding(QMediaFormat::RequiresVideo);
        }
    }
    m_codecInfo = QString("%1 / %2 / %3")
                      .arg(QMediaFormat::fileFormatName(fmt.fileFormat()),
                           QMediaFormat::videoCodecName(fmt.videoCodec()),
                           QMediaFormat::audioCodecName(fmt.audioCodec()));

    m_recorder.setMediaFormat(fmt);
    m_recorder.setQuality(QMediaRecorder::HighQuality);
    m_recorder.setVideoResolution(videoSize);
    m_recorder.setVideoFrameRate(fps);
    m_recorder.setAudioSampleRate(audioRate);
    m_recorder.setAudioChannelCount(1);
    m_recorder.setOutputLocation(QUrl::fromLocalFile(filePath));

    m_recorder.record();
    if (m_recorder.error() != QMediaRecorder::NoError) {
        emit errorOccurred(m_recorder.errorString());
        return false;
    }

    m_clock.start();
    m_recording = true;
    emit recordingChanged(true);
    qDebug() << "[TvRecorder] recording" << filePath << m_codecInfo;
    return true;
}

#endif  // QT_VERSION >= 6.8 (start)

void TvRecorder::stop()
{
    if (!m_recording) return;
    m_recording = false;
    m_recorder.stop();
    qDebug() << "[TvRecorder] stopped:" << m_framesSent << "frames," << m_framesDropped << "dropped,"
             << m_audioSamplesSent << "audio samples";
    emit recordingChanged(false);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)

void TvRecorder::pushFrame(const QImage& frame)
{
    if (!m_recording || !m_videoIn || frame.isNull()) return;

    QImage img = frame;
    if (img.size() != m_videoSize)
        img = img.scaled(m_videoSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32)
        img = img.convertToFormat(QImage::Format_RGB32);

    QVideoFrame vf(img);
    const qint64 nowUs = m_clock.nsecsElapsed() / 1000;
    vf.setStartTime(nowUs);
    vf.setEndTime(nowUs + 1000000 / std::max(1, m_fps));

    if (m_videoIn->sendVideoFrame(vf))
        ++m_framesSent;
    else
        ++m_framesDropped;   // encoder queue full: skip this frame rather than block the GUI
}

void TvRecorder::pushAudio(const std::vector<float>& mono)
{
    if (!m_recording || !m_audioIn || mono.empty()) return;

    QByteArray data(static_cast<int>(mono.size() * sizeof(qint16)), Qt::Uninitialized);
    qint16* out = reinterpret_cast<qint16*>(data.data());
    for (size_t i = 0; i < mono.size(); ++i)
        out[i] = static_cast<qint16>(std::clamp(mono[i], -1.0f, 1.0f) * 32767.0f);

    // Audio timestamps: contiguous by sample count, anchored on the same wall clock
    // as the video at the moment the encoder accepted the first buffer (the first
    // few buffers are rejected while the encoder initialises).
    const qint64 nowUs = m_clock.nsecsElapsed() / 1000;
    const qint64 baseUs = (m_audioBaseUs >= 0) ? m_audioBaseUs : nowUs;
    const qint64 startUs = baseUs + m_audioSamplesSent * 1000000 / m_audioFormat.sampleRate();
    QAudioBuffer buf(data, m_audioFormat, startUs);
    if (m_audioIn->sendAudioBuffer(buf)) {
        if (m_audioBaseUs < 0) m_audioBaseUs = baseUs;
        m_audioSamplesSent += static_cast<qint64>(mono.size());
        ++m_audioBuffersSent;
    } else {
        ++m_audioBuffersRejected;
    }
}

#endif  // QT_VERSION >= 6.8 (pushFrame / pushAudio)
