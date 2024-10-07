#ifndef HACKTVLIB_H
#define HACKTVLIB_H
#include <QStringList>
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <stdint.h>
#include <vector>
#include <mutex>
#include <cstring>
#include "hacktv/video.h"
#include "hacktv/rf.h"
#include "hackrfdevice.h"
#include "rtlsdrdevice.h"

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


class HackTvLib
{

public:
    using LogCallback = std::function<void(const std::string&)>;
    using DataCallback = std::function<void(const int8_t*, size_t)>;

     HackTvLib();
    ~HackTvLib();
    bool start();
    bool stop();
    void setLogCallback(LogCallback callback);
    void setReceivedDataCallback(DataCallback callback);
    bool setArguments(const std::vector<std::string>& args);
    void setMicEnabled(bool newMicEnabled);
    void setFrequency(uint64_t frequency_hz);
    void setSampleRate(uint32_t sample_rate);
    void setAmplitude(float newAmplitude);
    void setFilter_size(float newFilter_size);
    void setModulation_index(float newModulation_index);
    void setDecimation(int newDecimation);
    void setInterpolation(float newInterpolation);
    void setLnaGain(unsigned int lna_gain);
    void setVgaGain(unsigned int vga_gain);
    void setTxAmpGain(unsigned int tx_amp_gain);
    void setRxAmpGain(unsigned int rx_amp_gain);

private slots:
    void emitReceivedData(const int8_t *data, size_t data_len);
    void dataReceived(const int8_t* data, size_t data_len);
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
    bool micEnabled = false;
    void log(const char* format, ...);
    void cleanupArgv();
    void rfTxLoop();
    void rfRxLoop();
    HackRfDevice *hackRfDevice{};
    RTLSDRDevice *rtlSdrDevice{};
};

#endif // HACKTVLIB_H
