#ifndef TVTXENCODER_H
#define TVTXENCODER_H

#include <QObject>
#include <QString>
#include <atomic>
#include <string>
#include <vector>
#include <cstdint>

#include "core/TxEngine.h"

extern "C" {
#include "hacktv/video.h"
}

class QtAvSource;

/* Everything the TV transmitter can be told, mirroring hacktv's options. */
struct TvTxConfig {
    std::string mode = "b";         // vid_configs id: b, g, i, pal-d, m, l, ...
    bool filter   = true;           // VSB/FM output filter
    bool colour   = true;
    bool audio    = true;
    bool a2stereo = false;
    bool nicam    = true;
    bool acp      = false;
    bool vits     = false;
    bool vitc     = false;
    std::string wss;                // "", "4:3", "16:9", "14:9-letterbox", "16:9-letterbox", "auto"
    std::string teletext;           // path to .tti files or "", 625-line modes only
    std::string scramble;           // "", "vc1", "vc2", "vcs", "syster"
    float level = 1.0f;             // 0.1 .. 1.0
    bool invert = false;
};

/*
 * hacktv's video encoder as a TX source.
 *
 *   txStart()   (GUI thread, before the TX thread exists): vid_init() + open the AV source
 *   txProduce() (TX thread): vid_next_line() -> int16 complex -> int8 IQ
 *   txStop()    (GUI thread, after the TX thread joined): close the source, vid_free()
 */
class TvTxEncoder : public QObject, public ITxSource
{
    Q_OBJECT
public:
    enum class Source { TestPattern, VideoFile, Camera };

    explicit TvTxEncoder(QObject* parent = nullptr);
    ~TvTxEncoder() override;

    void setConfig(const TvTxConfig& c) { m_cfg = c; }
    void setSource(Source s, const QString& file, bool loop) { m_source = s; m_file = file; m_loop = loop; }
    /* Camera source: QCameraDevice::id() and the QAudioDevice::id() of its microphone (empty = silence). */
    void setCamera(const QByteArray& cameraId, const QByteArray& audioId) { m_cameraId = cameraId; m_audioId = audioId; }
    /* 0 = time pips every minute, 1 = silent, 2 = continuous 1 kHz tone */
    void setTestSound(int s) { m_testSound = s; }

    QtAvSource* videoSource() { return m_qt; }
    QString lastError() const { return m_error; }
    const vid_t* vid() const { return m_inited ? &m_vid : nullptr; }

    /* Names and descriptions of every mode hacktv knows (id, description). */
    static std::vector<std::pair<std::string, std::string>> modes();

    // ITxSource
    bool   txStart(uint32_t sampleRate) override;
    size_t txProduce(int8_t* iq, size_t bytes) override;
    void   txStop() override;

signals:
    void sourceError(const QString& message);

private:
    bool buildConfig(vid_config_t& conf);
    bool openSource();

    TvTxConfig m_cfg;
    Source m_source = Source::TestPattern;
    QString m_file;
    bool m_loop = true;
    QByteArray m_cameraId, m_audioId;
    int m_testSound = 0;
    QString m_error;

    vid_t m_vid{};
    bool m_inited = false;
    bool m_sourceOpen = false;
    QtAvSource* m_qt = nullptr;

    std::vector<int8_t> m_carry;   // int8 IQ not yet handed out
    size_t m_carryPos = 0;
    std::atomic<bool> m_eof{false};
};

#endif // TVTXENCODER_H
