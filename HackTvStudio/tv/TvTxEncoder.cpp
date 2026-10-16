#include "TvTxEncoder.h"
#include "QtAvSource.h"

#include <QDebug>
#include <cstring>

extern "C" {
#include "hacktv/hacktv.h"
#include "hacktv/mac.h"
}

TvTxEncoder::TvTxEncoder(QObject* parent)
    : QObject(parent)
    , m_qt(new QtAvSource(this))
{
    connect(m_qt, &QtAvSource::playbackError, this, &TvTxEncoder::sourceError);
}

TvTxEncoder::~TvTxEncoder()
{
    txStop();
}

std::vector<std::pair<std::string, std::string>> TvTxEncoder::modes()
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const vid_configs_t* c = vid_configs; c->id != nullptr; ++c)
        out.emplace_back(c->id, c->desc ? c->desc : "");
    return out;
}

/* Port of HackTvLib::setVideo(): mode table lookup + option overrides. */
bool TvTxEncoder::buildConfig(vid_config_t& conf)
{
    const vid_configs_t* vc = vid_configs;
    for (; vc->id != nullptr; ++vc)
        if (m_cfg.mode == vc->id) break;
    if (vc->id == nullptr) { m_error = "Unknown TV mode: " + QString::fromStdString(m_cfg.mode); return false; }

    std::memcpy(&conf, vc->conf, sizeof(vid_config_t));

    if (!m_cfg.colour) {
        if (conf.colour_mode == VID_PAL || conf.colour_mode == VID_SECAM || conf.colour_mode == VID_NTSC)
            conf.colour_mode = VID_NONE;
    }
    if (!m_cfg.audio) {
        conf.fm_mono_level = conf.fm_left_level = conf.fm_right_level = 0;
        conf.am_audio_level = conf.nicam_level = conf.dance_level = 0;
        conf.fm_mono_carrier = conf.fm_left_carrier = conf.fm_right_carrier = 0;
        conf.nicam_carrier = conf.dance_carrier = conf.am_mono_carrier = 0;
    }
    if (!m_cfg.nicam) {
        conf.nicam_level = 0;
        conf.nicam_carrier = 0;
    }
    if (m_cfg.a2stereo) conf.a2stereo = 1;
    conf.level *= m_cfg.level;

    if (!m_cfg.teletext.empty()) {
        if (conf.lines != 625) { m_error = "Teletext needs a 625-line mode"; return false; }
        conf.teletext = const_cast<char*>(m_cfg.teletext.c_str());
    }
    if (!m_cfg.wss.empty()) {
        if (conf.type != VID_RASTER_625) { m_error = "WSS needs a 625-line raster mode"; return false; }
        conf.wss = const_cast<char*>(m_cfg.wss.c_str());
    }

    static const char* kFree = "free";
    if (m_cfg.scramble == "vc1" || m_cfg.scramble == "vc2" || m_cfg.scramble == "vcs" || m_cfg.scramble == "syster") {
        if (conf.lines != 625 || conf.colour_mode != VID_PAL) { m_error = "Scrambling needs a 625-line PAL mode"; return false; }
        if (m_cfg.scramble == "vc1")      conf.videocrypt  = const_cast<char*>(kFree);
        else if (m_cfg.scramble == "vc2") conf.videocrypt2 = const_cast<char*>(kFree);
        else if (m_cfg.scramble == "vcs") conf.videocrypts = const_cast<char*>(kFree);
        else { conf.syster = 1; conf.systeraudio = 0; }
    }
    if (m_cfg.acp) {
        if (conf.lines != 625 && conf.lines != 525) { m_error = "ACP needs a 525/625-line mode"; return false; }
        if (!m_cfg.scramble.empty()) { m_error = "ACP cannot be combined with scrambling"; return false; }
        conf.acp = 1;
    }
    if (m_cfg.vits) {
        if (conf.type != VID_RASTER_625 && conf.type != VID_RASTER_525) { m_error = "VITS needs a 525/625-line raster mode"; return false; }
        conf.vits = 1;
    }
    if (m_cfg.vitc) {
        if (conf.type != VID_RASTER_625 && conf.type != VID_RASTER_525) { m_error = "VITC needs a 525/625-line raster mode"; return false; }
        conf.vitc = 1;
    }
    if (conf.type == VID_MAC) {
        conf.mac_audio_stereo = MAC_STEREO;
        conf.mac_audio_quality = MAC_HIGH_QUALITY;
        conf.mac_audio_protection = MAC_FIRST_LEVEL_PROTECTION;
        conf.mac_audio_companded = MAC_COMPANDED;
    }
    if (m_cfg.filter) conf.vfilter = 1;

    conf.swap_iq = 0;
    conf.offset = 0;
    conf.passthru = nullptr;
    conf.invert_video = m_cfg.invert ? 1 : 0;
    conf.raw_bb_file = nullptr;
    conf.raw_bb_blanking_level = 0;
    conf.raw_bb_white_level = INT16_MAX;
    conf.secam_field_id = 0;
    return true;
}

bool TvTxEncoder::openSource()
{
    if (m_source == Source::TestPattern) {
        if (av_test_open(&m_vid.av) != AV_OK) { m_error = "Test pattern source failed"; return false; }
    } else if (m_source == Source::Camera) {
        if (!m_qt->openCamera(&m_vid.av, m_cameraId, m_audioId)) {
            m_error = m_qt->errorString().isEmpty() ? "Cannot open camera" : m_qt->errorString();
            return false;
        }
    } else {
        if (m_file.isEmpty()) { m_error = "No video file selected"; return false; }
        if (!m_qt->open(&m_vid.av, m_file, m_loop)) { m_error = "Cannot open video file"; return false; }
    }
    m_sourceOpen = true;
    m_eof.store(false);
    return true;
}

bool TvTxEncoder::txStart(uint32_t sampleRate)
{
    txStop();
    m_error.clear();

    vid_config_t conf;
    if (!buildConfig(conf)) return false;

    std::memset(&m_vid, 0, sizeof(m_vid));
    if (vid_init(&m_vid, sampleRate, 0, &conf) != VID_OK) {
        m_error = "vid_init() failed (unsupported mode / sample rate combination)";
        return false;
    }
    m_inited = true;
    vid_info(&m_vid);

    // Port of HackTvLib::initAv(): what the source must deliver
    m_vid.av = av_t{};
    m_vid.av.width  = m_vid.active_width;
    m_vid.av.height = m_vid.conf.active_lines;
    m_vid.av.frame_rate = rational_t{ m_vid.conf.frame_rate.num * (m_vid.conf.interlace ? 2 : 1),
                                        m_vid.conf.frame_rate.den };
    m_vid.av.display_aspect_ratios[0] = m_vid.conf.frame_aspects[0];
    m_vid.av.display_aspect_ratios[1] = m_vid.conf.frame_aspects[1];
    m_vid.av.fit_mode = AV_FIT_FIT;
    m_vid.av.min_display_aspect_ratio = rational_t{ 4, 3 };
    m_vid.av.max_display_aspect_ratio = rational_t{ 16, 9 };
    m_vid.av.sample_rate = rational_t{ m_vid.audio ? HACKTV_AUDIO_SAMPLE_RATE : 0, 1 };
    if ((m_vid.conf.frame_orientation & 3) == VID_ROTATE_90 || (m_vid.conf.frame_orientation & 3) == VID_ROTATE_270) {
        m_vid.av.width  = m_vid.conf.active_lines;
        m_vid.av.height = m_vid.active_width;
    }

    if (!openSource()) {
        vid_free(&m_vid);
        m_inited = false;
        return false;
    }

    m_carry.clear();
    m_carryPos = 0;
    qDebug() << "[TvTx] mode" << QString::fromStdString(m_cfg.mode) << "video" << m_vid.av.width << "x" << m_vid.av.height
             << "sample rate" << m_vid.sample_rate << "audio" << m_vid.audio;
    return true;
}

size_t TvTxEncoder::txProduce(int8_t* iq, size_t bytes)
{
    if (!m_inited) return 0;
    size_t done = 0;

    while (done < bytes) {
        if (m_carryPos >= m_carry.size()) {
            size_t samples = 0;
            int16_t* data = vid_next_line(&m_vid, &samples);
            if (!data || samples == 0) {
                // End of the source (non-looping file). Keep the carrier up with silence.
                m_eof.store(true);
                std::memset(iq + done, 0, bytes - done);
                return done > 0 ? bytes : 0;
            }
            // Complex int16 -> int8 (same conversion as hacktv's rf_hackrf.c)
            m_carry.resize(samples * 2);
            for (size_t i = 0; i < samples * 2; ++i)
                m_carry[i] = static_cast<int8_t>(data[i] >> 8);
            m_carryPos = 0;
        }
        const size_t n = std::min(bytes - done, m_carry.size() - m_carryPos);
        std::memcpy(iq + done, m_carry.data() + m_carryPos, n);
        m_carryPos += n;
        done += n;
    }
    return bytes;
}

void TvTxEncoder::txStop()
{
    if (m_sourceOpen) {
        av_close(&m_vid.av);
        if (m_source != Source::TestPattern) m_qt->close();
        m_sourceOpen = false;
    }
    if (m_inited) {
        vid_free(&m_vid);
        m_inited = false;
    }
    m_carry.clear();
    m_carryPos = 0;
}
