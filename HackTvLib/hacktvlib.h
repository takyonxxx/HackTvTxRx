#ifndef HACKTVLIB_H
#define HACKTVLIB_H
#include <stdint.h>
#include "hacktv/video.h"
#include "hacktv/rf.h"
#include <functional>
#include <string>

#define HACKTV_OK             0
#define HACKTV_ERROR         -1
#define HACKTV_OUT_OF_MEMORY -2

/* Standard audio sample rate */
#define HACKTV_AUDIO_SAMPLE_RATE 32000

/* Program state */
typedef struct {

    /* Configuration */
    char *output_type;
    char *output;
    char *mode;
    int samplerate;
    int pixelrate;
    float level;
    float deviation;
    float gamma;
    int interlace;
    av_fit_mode_t fit_mode;
    rational_t min_aspect;
    rational_t max_aspect;
    int repeat;
    int shuffle;
    int verbose;
    char *teletext;
    char *wss;
    char *videocrypt;
    char *videocrypt2;
    char *videocrypts;
    int syster;
    int systeraudio;
    char *eurocrypt;
    int acp;
    int vits;
    int vitc;
    int filter;
    int nocolour;
    int noaudio;
    int nonicam;
    int a2stereo;
    int scramble_video;
    int scramble_audio;
    uint64_t frequency;
    int amp;
    int gain;
    char *antenna;
    int file_type;
    int chid;
    int mac_audio_stereo;
    int mac_audio_quality;
    int mac_audio_protection;
    int mac_audio_companded;
    char *sis;
    int swap_iq;
    int64_t offset;
    char *passthru;
    int invert_video;
    char *raw_bb_file;
    int16_t raw_bb_blanking_level;
    int16_t raw_bb_white_level;
    int secam_field_id;
    int list_modes;
    int json;
    char *ffmt;
    char *fopts;

    /* Video encoder state */
    vid_t vid;

    /* RF sink interface */
    rf_t rf;

} hacktv_t;

static hacktv_t s;
const vid_configs_t *vid_confs;
vid_config_t vid_conf;
char *pre, *sub;
int l;
int r;

class HackTvLib {
public:
    using LogCallback = std::function<void(const std::string&)>;

    HackTvLib();
    ~HackTvLib();
    void start(int argc, char *argv[]);
    void stop();
    void setLogCallback(LogCallback callback);

private:
    LogCallback m_logCallback;
    void log(const char* format, ...);
};

#endif // HACKTVLIB_H
