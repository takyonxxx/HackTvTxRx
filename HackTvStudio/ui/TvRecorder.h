#ifndef TVRECORDER_H
#define TVRECORDER_H

#include <QObject>
#include <QAudioFormat>
#include <QElapsedTimer>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QSize>
#include <QString>
#include <vector>

class QVideoFrameInput;
class QAudioBufferInput;

/*
 * Records the decoded TV picture and sound to an .mp4 (H.264 + AAC when the
 * platform encoders are available, otherwise whatever Qt can encode).
 *
 * Frames and audio are pushed from the GUI thread, exactly as they reach the
 * screen / speaker; Qt Multimedia's FFmpeg backend does the encoding on its
 * own threads. Timestamps come from one wall clock so audio and video stay
 * in step.
 */
class TvRecorder : public QObject
{
    Q_OBJECT
public:
    explicit TvRecorder(QObject* parent = nullptr);
    ~TvRecorder() override;

    bool start(const QString& filePath, const QSize& videoSize, int fps, int audioRate);
    void stop();

    bool    isRecording() const { return m_recording; }
    qint64  durationMs() const { return m_recording ? m_clock.elapsed() : 0; }
    QString filePath() const { return m_path; }
    QString codecInfo() const { return m_codecInfo; }
    qint64  audioBuffersSent() const { return m_audioBuffersSent; }
    qint64  audioBuffersRejected() const { return m_audioBuffersRejected; }
    qint64  framesSent() const { return m_framesSent; }
    qint64  framesDropped() const { return m_framesDropped; }

    void pushFrame(const QImage& frame);               // any format; converted / scaled here
    void pushAudio(const std::vector<float>& mono);    // float mono at audioRate

signals:
    void recordingChanged(bool recording);
    void errorOccurred(const QString& message);

private:
    QMediaCaptureSession m_session;
    QMediaRecorder       m_recorder;
    QVideoFrameInput*    m_videoIn = nullptr;
    QAudioBufferInput*   m_audioIn = nullptr;
    QAudioFormat         m_audioFormat;
    QElapsedTimer        m_clock;
    QSize   m_videoSize;
    int     m_fps = 25;
    qint64  m_audioSamplesSent = 0;
    qint64  m_audioBaseUs = -1;      // wall-clock time of the first accepted audio buffer
    qint64  m_audioBuffersSent = 0;
    qint64  m_audioBuffersRejected = 0;
    qint64  m_framesSent = 0;
    qint64  m_framesDropped = 0;
    bool    m_recording = false;
    QString m_path;
    QString m_codecInfo;
};

#endif // TVRECORDER_H
