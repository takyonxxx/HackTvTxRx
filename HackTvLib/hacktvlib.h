#ifndef HACKTVLIB_H
#define HACKTVLIB_H
#include <QStringList>
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <stdint.h>
#include <complex>
#include <vector>
#include <mutex>
#include "hacktv/video.h"
#include "hacktv/rf.h"

/* Return codes */
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

rxtx_mode m_rxTxMode;

class HackTvLib{
public:
    using LogCallback = std::function<void(const std::string&)>;
    using DataCallback = std::function<void(const int16_t*, size_t)>;

     HackTvLib();
    ~HackTvLib();
    bool start();
    bool stop();
    void setLogCallback(LogCallback callback);
    void setReceivedDataCallback(DataCallback callback);
    bool setArguments(const std::vector<std::string>& args);    

private:
    LogCallback m_logCallback;
    DataCallback m_dataCallback;
    std::thread m_thread;
    std::mutex m_mutex;
    std::atomic<bool> m_abort;
    std::atomic<int> m_signal;
    std::vector<char*> m_argv;
    bool openDevice();
    bool setVideo();
    bool initAv();
    bool parseArguments();
    void log(const char* format, ...);
    void cleanupArgv();
    void rfTxLoop();
    void rfRxLoop();
    void emitReceivedData(const int16_t* data, size_t samples);
};

#endif // HACKTVLIB_H
