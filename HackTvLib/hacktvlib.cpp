#ifdef _WIN32
#include <windows.h>

// CRITICAL FIX: MinGW with -mthreads expects DllEntryPoint
extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)hinstDLL;
    (void)fdwReason;
    (void)lpvReserved;
    return TRUE;
}

// Also provide standard DllMain for compatibility
extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    return DllEntryPoint(hinstDLL, fdwReason, lpvReserved);
}

#endif

#include "hacktvlib.h"
#include <QThread>
#include <getopt.h>
#include <cstdarg>
#include <cstdio>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include "hacktv/av.h"
#include "hacktv/rf.h"

#define VERSION "1.0"

enum {
    _OPT_TELETEXT = 1000,
    _OPT_WSS,
    _OPT_VIDEOCRYPT,
    _OPT_VIDEOCRYPT2,
    _OPT_VIDEOCRYPTS,
    _OPT_SYSTER,
    _OPT_SYSTERAUDIO,
    _OPT_EUROCRYPT,
    _OPT_ACP,
    _OPT_VITS,
    _OPT_VITC,
    _OPT_FILTER,
    _OPT_NOCOLOUR,
    _OPT_NOAUDIO,
    _OPT_NONICAM,
    _OPT_A2STEREO,
    _OPT_SINGLE_CUT,
    _OPT_DOUBLE_CUT,
    _OPT_SCRAMBLE_AUDIO,
    _OPT_CHID,
    _OPT_MAC_AUDIO_STEREO,
    _OPT_MAC_AUDIO_MONO,
    _OPT_MAC_AUDIO_HIGH_QUALITY,
    _OPT_MAC_AUDIO_MEDIUM_QUALITY,
    _OPT_MAC_AUDIO_COMPANDED,
    _OPT_MAC_AUDIO_LINEAR,
    _OPT_MAC_AUDIO_L1_PROTECTION,
    _OPT_MAC_AUDIO_L2_PROTECTION,
    _OPT_SIS,
    _OPT_SWAP_IQ,
    _OPT_OFFSET,
    _OPT_PASSTHRU,
    _OPT_INVERT_VIDEO,
    _OPT_RAW_BB_FILE,
    _OPT_RAW_BB_BLANKING,
    _OPT_RAW_BB_WHITE,
    _OPT_SECAM_FIELD_ID,
    _OPT_FFMT,
    _OPT_FOPTS,
    _OPT_PIXELRATE,
    _OPT_LIST_MODES,
    _OPT_JSON,
    _OPT_SHUFFLE,
    _OPT_FIT,
    _OPT_MIN_ASPECT,
    _OPT_MAX_ASPECT,
    _OPT_LETTERBOX,
    _OPT_PILLARBOX,
    _OPT_VERSION,
    _OPT_MODE,
};


static int _parse_ratio(rational_t *r, const char *s)
{
    int i;
    int64_t e;

    i = sscanf(s, "%d%*[:/]%d", &r->num, &r->den);
    if(i != 2 || r->den == 0)
    {
        return(HACKTV_ERROR);
    }

    e = gcd(r->num, r->den);
    r->num /= e;
    r->den /= e;

    return(HACKTV_OK);
}

static void print_version(void)
{
    printf("hacktv %s\n", VERSION);
}

static void print_usage(void)
{
    printf(
        "\n"
        "Usage: hacktv [options] input [input...]\n"
        "\n"
        );
}

HackTvLib::HackTvLib(QObject *parent)
    : QObject(parent)
    , m_logCallback()
    , m_dataCallback()
    , m_abort(false)
    , m_signal(0)
    , s(nullptr)
    , m_rxTxMode(RX_MODE)
    , micEnabled(false)
    , hackRfDevice(nullptr)
    , rtlSdrDevice(nullptr)
{
    hackRfDevice = nullptr;
    rtlSdrDevice = nullptr;
    s = nullptr;

    fprintf(stderr, "HackTvLib initialized.\n");
    fflush(stderr);
}

HackTvLib::~HackTvLib()
{
    fprintf(stderr, "=== HackTvLib Destructor ===\n");
    fflush(stderr);

    // Only stop if not already stopped
    if (m_txThread.joinable() || hackRfDevice || rtlSdrDevice) {
        fprintf(stderr, "Calling stop() from destructor\n");
        fflush(stderr);
        stop();
    } else {
        fprintf(stderr, "Already stopped, skipping stop() call\n");
        fflush(stderr);
    }

    // Final cleanup
    if (hackRfDevice) {
        fprintf(stderr, "Cleaning up hackRfDevice in destructor\n");
        fflush(stderr);
        delete hackRfDevice;
        hackRfDevice = nullptr;
    }

    if (rtlSdrDevice) {
        fprintf(stderr, "Cleaning up rtlSdrDevice in destructor\n");
        fflush(stderr);
        delete rtlSdrDevice;
        rtlSdrDevice = nullptr;
    }

    fprintf(stderr, "=== HackTvLib Destructor Complete ===\n");
    fflush(stderr);
}

void HackTvLib::setLogCallback(LogCallback callback) {
    m_logCallback = std::move(callback);
}

void HackTvLib::log(const char* format, ...) {
    if (m_logCallback) {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        m_logCallback(buffer);
    }
}

const char* getRxTxModeString(rxtx_mode mode) {
    switch (mode) {
    case TX_MODE:
        return "TX";
    case RX_MODE:
        return "RX";
    default:
        return "Unknown";
    }
}

const char* getBoolString(int value) {
    return value ? "True" : "False";
}

void HackTvLib::setFrequency(uint64_t frequency_hz)
{
    if (!s) return;

    log("Set Frequency : %d", frequency_hz);

    if(strcmp(s->output_type, "hackrf") == 0)
    {
        if (hackRfDevice) {
            hackRfDevice->setFrequency(frequency_hz);
        }
    }
    else if(strcmp(s->output_type, "rtlsdr") == 0)
    {
        if (rtlSdrDevice) {
            rtlSdrDevice->setFrequency(frequency_hz);
        }
    }
}

void HackTvLib::setSampleRate(uint32_t sample_rate)
{
    if (!s) return;

    log("Set SampleRate : %d", sample_rate);

    if(strcmp(s->output_type, "hackrf") == 0)
    {
        if (hackRfDevice) {
            hackRfDevice->setSampleRate(sample_rate);
        }
    }
    else if(strcmp(s->output_type, "rtlsdr") == 0)
    {
        if (rtlSdrDevice) {
            rtlSdrDevice->setSampleRate(sample_rate);
        }
    }
}

void HackTvLib::setAmplitude(float newAmplitude)
{
    if (hackRfDevice) {
        hackRfDevice->setAmplitude(newAmplitude);
    }
}

void HackTvLib::setFilter_size(float newFilter_size)
{
    if (hackRfDevice) {
        hackRfDevice->setFilter_size(newFilter_size);
    }
}

void HackTvLib::setModulation_index(float newModulation_index)
{
    if (hackRfDevice) {
        hackRfDevice->setModulation_index(newModulation_index);
    }
}

void HackTvLib::setDecimation(int newDecimation)
{
    if (hackRfDevice) {
        hackRfDevice->setDecimation(newDecimation);
    }
}

void HackTvLib::setInterpolation(float newInterpolation)
{
    if (hackRfDevice) {
        hackRfDevice->setInterpolation(newInterpolation);
    }
}

void HackTvLib::setLnaGain(unsigned int lna_gain)
{
    if (hackRfDevice) {
        hackRfDevice->setLnaGain(lna_gain);
    }
}

void HackTvLib::setVgaGain(unsigned int vga_gain)
{
    if (hackRfDevice) {
        hackRfDevice->setVgaGain(vga_gain);
    }
}

void HackTvLib::setTxAmpGain(unsigned int tx_amp_gain)
{
    if (hackRfDevice) {
        hackRfDevice->setTxAmpGain(tx_amp_gain);
    }
}

void HackTvLib::setRxAmpGain(unsigned int rx_amp_gain)
{
    if (hackRfDevice) {
        hackRfDevice->setRxAmpGain(rx_amp_gain);
    }
}

void HackTvLib::dataReceived(const int8_t *data, size_t len)
{
    emitReceivedData(data, len);
}

void HackTvLib::cleanupArgv()
{
    for (char* arg : m_argv) {
        free(arg);
    }
    m_argv.clear();
}

bool HackTvLib::setArguments(const std::vector<std::string>& args)
{
    cleanupArgv();
    m_argv.resize(args.size() + 1);
    m_argv[0] = strdup("HackTv");
    for (size_t i = 0; i < args.size(); ++i) {
        m_argv[i + 1] = strdup(args[i].c_str());
    }
    return true;
}

bool HackTvLib::openDevice()
{
    if(strcmp(s->output_type, "hackrf") == 0)
    {
#ifdef HAVE_HACKRF
        if(rf_hackrf_open(m_rxTxMode, &s->rf, s->output, s->vid.sample_rate, s->frequency, s->amp) != RF_OK)
        {
            vid_free(&s->vid);
            log("Could not open HackRF. Please check the device.");
            return false;
        }
#else
        fprintf(stderr, "HackRF support is not available in this build of hacktv.\n");
        vid_free(&s->vid);
        return false;
#endif
    }
    else if(strcmp(s->output_type, "file") == 0)
    {
        if(rf_file_open(&s->rf, s->output, s->file_type, s->vid.conf.output_type == RF_INT16_COMPLEX) != RF_OK)
        {
            vid_free(&s->vid);
            return false;
        }
    }

    return true;
}

bool HackTvLib::setVideo()
{
    const vid_configs_t *vid_confs;  // Global'den local'e taşındı
    vid_config_t vid_conf;  // Global'den local'e taşındı
    int r;  // Global'den local'e taşındı

    /* Load the mode configuration */
    for(vid_confs = vid_configs; vid_confs->id != NULL; vid_confs++)
    {
        if(strcmp(s->mode, vid_confs->id) == 0) break;
    }

    if(vid_confs->id == NULL)
    {
        fprintf(stderr, "Unrecognised TV mode.\n");
        return false;
    }

    memcpy(&vid_conf, vid_confs->conf, sizeof(vid_config_t));

    if(s->deviation > 0)
    {
        /* Override the FM deviation value */
        vid_conf.fm_deviation = s->deviation;
    }

    if(s->gamma > 0)
    {
        /* Override the gamma value */
        vid_conf.gamma = s->gamma;
    }

    if(s->interlace)
    {
        vid_conf.interlace = 1;
    }

    if(s->nocolour)
    {
        if(vid_conf.colour_mode == VID_PAL ||
            vid_conf.colour_mode == VID_SECAM ||
            vid_conf.colour_mode == VID_NTSC)
        {
            vid_conf.colour_mode = VID_NONE;
        }
    }

    if(s->noaudio > 0)
    {
        /* Disable all audio sub-carriers */
        vid_conf.fm_mono_level = 0;
        vid_conf.fm_left_level = 0;
        vid_conf.fm_right_level = 0;
        vid_conf.am_audio_level = 0;
        vid_conf.nicam_level = 0;
        vid_conf.dance_level = 0;
        vid_conf.fm_mono_carrier = 0;
        vid_conf.fm_left_carrier = 0;
        vid_conf.fm_right_carrier = 0;
        vid_conf.nicam_carrier = 0;
        vid_conf.dance_carrier = 0;
        vid_conf.am_mono_carrier = 0;
    }

    if(s->nonicam > 0)
    {
        /* Disable the NICAM sub-carrier */
        vid_conf.nicam_level = 0;
        vid_conf.nicam_carrier = 0;
    }

    if(s->a2stereo > 0)
    {
        vid_conf.a2stereo = 1;
    }

    vid_conf.scramble_video = s->scramble_video;
    vid_conf.scramble_audio = s->scramble_audio;

    vid_conf.level *= s->level;

    if(s->teletext)
    {
        if(vid_conf.lines != 625)
        {
            fprintf(stderr, "Teletext is only available with 625 line modes->\n");
            return false;
        }

        vid_conf.teletext = s->teletext;
    }

    if(s->wss)
    {
        if(vid_conf.type != VID_RASTER_625)
        {
            fprintf(stderr, "WSS is only supported for 625 line raster modes->\n");
            return false;
        }

        vid_conf.wss = s->wss;
    }

    if(s->videocrypt)
    {
        if(vid_conf.lines != 625 && vid_conf.colour_mode != VID_PAL)
        {
            fprintf(stderr, "Videocrypt I is only compatible with 625 line PAL modes->\n");
            return false;
        }

        vid_conf.videocrypt = s->videocrypt;
    }

    if(s->videocrypt2)
    {
        if(vid_conf.lines != 625 && vid_conf.colour_mode != VID_PAL)
        {
            fprintf(stderr, "Videocrypt II is only compatible with 625 line PAL modes->\n");
            return false;
        }

        /* Only allow both VC1 and VC2 if both are in free-access mode */
        if(s->videocrypt && !(strcmp(s->videocrypt, "free") == 0 && strcmp(s->videocrypt2, "free") == 0))
        {
            fprintf(stderr, "Videocrypt I and II cannot be used together except in free-access mode.\n");
            return false;
        }

        vid_conf.videocrypt2 = s->videocrypt2;
    }

    if(s->videocrypts)
    {
        if(vid_conf.lines != 625 && vid_conf.colour_mode != VID_PAL)
        {
            fprintf(stderr, "Videocrypt S is only compatible with 625 line PAL modes->\n");
            return false;
        }

        if(s->videocrypt || s->videocrypt2)
        {
            fprintf(stderr, "Using multiple scrambling modes is not supported.\n");
            return false;
        }

        vid_conf.videocrypts = s->videocrypts;
    }

    if(s->syster)
    {
        if(vid_conf.lines != 625 && vid_conf.colour_mode != VID_PAL)
        {
            fprintf(stderr, "Nagravision Syster is only compatible with 625 line PAL modes->\n");
            return false;
        }

        if(vid_conf.videocrypt || vid_conf.videocrypt2 || vid_conf.videocrypts)
        {
            fprintf(stderr, "Using multiple scrambling modes is not supported.\n");
            return false;
        }

        vid_conf.syster = 1;
        vid_conf.systeraudio = s->systeraudio;
    }

    if(s->eurocrypt)
    {
        if(vid_conf.type != VID_MAC)
        {
            fprintf(stderr, "Eurocrypt is only compatible with D/D2-MAC modes->\n");
            return false;
        }

        if(vid_conf.scramble_video == 0)
        {
            /* Default to single-cut scrambling if none was specified */
            vid_conf.scramble_video = 1;
        }

        vid_conf.eurocrypt = s->eurocrypt;
    }

    if(s->acp)
    {
        if(vid_conf.lines != 625 && vid_conf.lines != 525)
        {
            fprintf(stderr, "Analogue Copy Protection is only compatible with 525 and 625 line modes->\n");
            return false;
        }

        if(vid_conf.videocrypt || vid_conf.videocrypt2 || vid_conf.videocrypts || vid_conf.syster)
        {
            fprintf(stderr, "Analogue Copy Protection cannot be used with video scrambling enabled.\n");
            return false;
        }

        vid_conf.acp = 1;
    }

    if(s->vits)
    {
        if(vid_conf.type != VID_RASTER_625 &&
            vid_conf.type != VID_RASTER_525)
        {
            fprintf(stderr, "VITS is only currently supported for 625 and 525 line raster modes->\n");
            return false;
        }

        vid_conf.vits = 1;
    }

    if(s->vitc)
    {
        if(vid_conf.type != VID_RASTER_625 &&
            vid_conf.type != VID_RASTER_525)
        {
            fprintf(stderr, "VITC is only currently supported for 625 and 525 line raster modes->\n");
            return false;
        }

        vid_conf.vitc = 1;
    }

    if(vid_conf.type == VID_MAC)
    {
        if(s->chid >= 0)
        {
            vid_conf.chid = (uint16_t) s->chid;
        }

        vid_conf.mac_audio_stereo = s->mac_audio_stereo;
        vid_conf.mac_audio_quality = s->mac_audio_quality;
        vid_conf.mac_audio_protection = s->mac_audio_protection;
        vid_conf.mac_audio_companded = s->mac_audio_companded;
    }

    if(s->filter)
    {
        vid_conf.vfilter = 1;
    }

    if(s->sis)
    {
        if(vid_conf.lines != 625)
        {
            fprintf(stderr, "SiS is only available with 625 line modes->\n");
            return false;
        }

        vid_conf.sis = s->sis;
    }

    vid_conf.swap_iq = s->swap_iq;
    vid_conf.offset = s->offset;
    vid_conf.passthru = s->passthru;
    vid_conf.invert_video = s->invert_video;
    vid_conf.raw_bb_file = s->raw_bb_file;
    vid_conf.raw_bb_blanking_level = s->raw_bb_blanking_level;
    vid_conf.raw_bb_white_level = s->raw_bb_white_level;
    vid_conf.secam_field_id = s->secam_field_id;

    /* Setup video encoder */
    r = vid_init(&s->vid, s->samplerate, s->pixelrate, &vid_conf);
    if(r != VID_OK)
    {
        fprintf(stderr, "Unable to initialise video encoder.\n");
        return false;
    }

    vid_info(&s->vid);

    log("Video: %dx%d %.2f fps (full frame %dx%d)",
        s->vid.active_width, s->vid.conf.active_lines,
        (double) s->vid.conf.frame_rate.num / s->vid.conf.frame_rate.den,
        s->vid.width, s->vid.conf.lines
        );
    if(s->vid.sample_rate != s->vid.pixel_rate)
    {
        log("Pixel rate: %d", s->vid.pixel_rate);
    }
    log("Sample rate: %d", s->vid.sample_rate);

    return true;
}

bool HackTvLib::initAv()
{
    av_ffmpeg_init();

    /* Configure AV source settings */
    s->vid.av = (av_t) {
        .width = s->vid.active_width,
        .height = s->vid.conf.active_lines,
        .frame_rate = (rational_t) {
            .num = s->vid.conf.frame_rate.num * (s->vid.conf.interlace ? 2 : 1),
            .den = s->vid.conf.frame_rate.den,
        },
        .display_aspect_ratios = {
            s->vid.conf.frame_aspects[0],
            s->vid.conf.frame_aspects[1]
        },
        .fit_mode = s->fit_mode,
        .min_display_aspect_ratio = s->min_aspect,
        .max_display_aspect_ratio = s->max_aspect,
        .default_frame = {0},
        .frames = 0,
        .sample_rate = (rational_t) {
            .num = (s->vid.audio ? HACKTV_AUDIO_SAMPLE_RATE : 0),
            .den = 1,
        },
        .samples = 0,
        .av_source_ctx = NULL,
        .read_video = NULL,
        .read_audio = NULL,
        .eof = NULL,
        .close = NULL
    };

    if((s->vid.conf.frame_orientation & 3) == VID_ROTATE_90 ||
        (s->vid.conf.frame_orientation & 3) == VID_ROTATE_270)
    {
        /* Flip dimensions if the lines are scanned vertically */
        s->vid.av.width = s->vid.conf.active_lines;
        s->vid.av.height = s->vid.active_width;
    }

    // Set FFmpeg options for better interrupt handling
    if (!s->fopts) {
        s->fopts = strdup("analyzeduration=1000000:probesize=1000000:timeout=5000000");
    }

    return true;
}

bool HackTvLib::parseArguments()
{
    // long_options global scope'tan buraya taşındı (DLL entry point sorunu için)
    static struct option long_options[] = {
        { "output",         required_argument, 0, 'o' },
        { "mode",           required_argument, 0, 'm' },
        { "list-modes",     no_argument,       0, _OPT_LIST_MODES },
        { "samplerate",     required_argument, 0, 's' },
        { "pixelrate",      required_argument, 0, _OPT_PIXELRATE },
        { "level",          required_argument, 0, 'l' },
        { "deviation",      required_argument, 0, 'D' },
        { "gamma",          required_argument, 0, 'G' },
        { "interlace",      no_argument,       0, 'i' },
        { "fit",            required_argument, 0, _OPT_FIT },
        { "min-aspect",     required_argument, 0, _OPT_MIN_ASPECT },
        { "max-aspect",     required_argument, 0, _OPT_MAX_ASPECT },
        { "letterbox",      no_argument,       0, _OPT_LETTERBOX },
        { "pillarbox",      no_argument,       0, _OPT_PILLARBOX },
        { "repeat",         no_argument,       0, 'r' },
        { "shuffle",        no_argument,       0, _OPT_SHUFFLE },
        { "verbose",        no_argument,       0, 'v' },
        { "teletext",       required_argument, 0, _OPT_TELETEXT },
        { "wss",            required_argument, 0, _OPT_WSS },
        { "videocrypt",     required_argument, 0, _OPT_VIDEOCRYPT },
        { "videocrypt2",    required_argument, 0, _OPT_VIDEOCRYPT2 },
        { "videocrypts",    required_argument, 0, _OPT_VIDEOCRYPTS },
        { "syster",         no_argument,       0, _OPT_SYSTER },
        { "systeraudio",    no_argument,       0, _OPT_SYSTERAUDIO },
        { "acp",            no_argument,       0, _OPT_ACP },
        { "vits",           no_argument,       0, _OPT_VITS },
        { "vitc",           no_argument,       0, _OPT_VITC },
        { "filter",         no_argument,       0, _OPT_FILTER },
        { "nocolour",       no_argument,       0, _OPT_NOCOLOUR },
        { "nocolor",        no_argument,       0, _OPT_NOCOLOUR },
        { "noaudio",        no_argument,       0, _OPT_NOAUDIO },
        { "nonicam",        no_argument,       0, _OPT_NONICAM },
        { "a2stereo",       no_argument,       0, _OPT_A2STEREO },
        { "single-cut",     no_argument,       0, _OPT_SINGLE_CUT },
        { "double-cut",     no_argument,       0, _OPT_DOUBLE_CUT },
        { "eurocrypt",      required_argument, 0, _OPT_EUROCRYPT },
        { "scramble-audio", no_argument,       0, _OPT_SCRAMBLE_AUDIO },
        { "chid",           required_argument, 0, _OPT_CHID },
        { "mac-audio-stereo", no_argument,     0, _OPT_MAC_AUDIO_STEREO },
        { "mac-audio-mono", no_argument,       0, _OPT_MAC_AUDIO_MONO },
        { "mac-audio-high-quality", no_argument, 0, _OPT_MAC_AUDIO_HIGH_QUALITY },
        { "mac-audio-medium-quality", no_argument, 0, _OPT_MAC_AUDIO_MEDIUM_QUALITY },
        { "mac-audio-companded", no_argument,  0, _OPT_MAC_AUDIO_COMPANDED },
        { "mac-audio-linear", no_argument,     0, _OPT_MAC_AUDIO_LINEAR },
        { "mac-audio-l1-protection", no_argument, 0, _OPT_MAC_AUDIO_L1_PROTECTION },
        { "mac-audio-l2-protection", no_argument, 0, _OPT_MAC_AUDIO_L2_PROTECTION },
        { "sis",            required_argument, 0, _OPT_SIS },
        { "swap-iq",        no_argument,       0, _OPT_SWAP_IQ },
        { "offset",         required_argument, 0, _OPT_OFFSET },
        { "passthru",       required_argument, 0, _OPT_PASSTHRU },
        { "invert-video",   no_argument,       0, _OPT_INVERT_VIDEO },
        { "raw-bb-file",    required_argument, 0, _OPT_RAW_BB_FILE },
        { "raw-bb-blanking", required_argument, 0, _OPT_RAW_BB_BLANKING },
        { "raw-bb-white",   required_argument, 0, _OPT_RAW_BB_WHITE },
        { "secam-field-id", no_argument,       0, _OPT_SECAM_FIELD_ID },
        { "json",           no_argument,       0, _OPT_JSON },
        { "ffmt",           required_argument, 0, _OPT_FFMT },
        { "fopts",          required_argument, 0, _OPT_FOPTS },
        { "frequency",      required_argument, 0, 'f' },
        { "amp",            no_argument,       0, 'a' },
        { "gain",           required_argument, 0, 'g' },
        { "antenna",        required_argument, 0, 'A' },
        { "type",           required_argument, 0, 't' },
        { "version",        no_argument,       0, _OPT_VERSION },
        { "rx-tx-mode",     required_argument, 0, _OPT_MODE },
        { 0,                0,                 0,  0  }
    };  // long_options dizisi sonu

    char *pre, *sub;  // Global'den local'e taşındı
    int l;  // Global'den local'e taşındı

    int c;
    int option_index;
    opterr = 0;
    optind = 1;

    while ((c = getopt_long(m_argv.size(), m_argv.data(), "o:m:s:D:G:irvf:al:g:A:t:", long_options, &option_index)) != -1)
    {
        switch(c)
        {
        case 'o': /* -o, --output <[type:]target> */

            /* Get a pointer to the output prefix and target */
            pre = optarg;
            sub = strchr(pre, ':');

            if(sub != NULL)
            {
                /* Split the optarg into two */
                *sub = '\0';
                sub++;
            }

            /* Try to match the prefix with a known type */
            if(strcmp(pre, "file") == 0)
            {
                s->output_type = "file";
                s->output = sub;
            }
            else if(strcmp(pre, "hackrf") == 0)
            {
                s->output_type = "hackrf";
                s->output = sub;
            }
            else if(strcmp(pre, "rtlsdr") == 0)
            {
                s->output_type = "rtlsdr";
                s->output = sub;
            }
            else
            {
                /* Unrecognised output type, default to file */
                if(sub != NULL)
                {
                    /* Recolonise */
                    sub--;
                    *sub = ':';
                }

                s->output_type = "file";
                s->output = pre;
            }

            break;

        case 'm': /* -m, --mode <name> */
            s->mode = optarg;
            break;

        case _OPT_LIST_MODES: /* --list-modes */
            s->list_modes = 1;
            break;

        case 's': /* -s, --samplerate <value> */
            s->samplerate = atoi(optarg);
            break;

        case _OPT_PIXELRATE: /* --pixelrate <value> */
            s->pixelrate = atoi(optarg);
            break;

        case 'l': /* -l, --level <value> */
            s->level = atof(optarg);
            break;

        case 'D': /* -D, --deviation <value> */
            s->deviation = atof(optarg);
            break;

        case 'G': /* -G, --gamma <value> */
            s->gamma = atof(optarg);
            break;

        case 'i': /* -i, --interlace */
            s->interlace = 1;
            break;

        case _OPT_FIT: /* --fit <mode> */

            if(strcmp(optarg, "stretch") == 0) s->fit_mode = AV_FIT_STRETCH;
            else if(strcmp(optarg, "fill") == 0) s->fit_mode = AV_FIT_FILL;
            else if(strcmp(optarg, "fit") == 0) s->fit_mode = AV_FIT_FIT;
            else if(strcmp(optarg, "none") == 0) s->fit_mode = AV_FIT_NONE;
            else
            {
                fprintf(stderr, "Unrecognised fit mode '%s'.\n", optarg);
                return false;
            }

            break;

        case _OPT_MIN_ASPECT: /* --min-aspect <value> */

            if(_parse_ratio(&s->min_aspect, optarg) != HACKTV_OK)
            {
                fprintf(stderr, "Invalid minimum aspect\n");
                return false;
            }

            break;

        case _OPT_MAX_ASPECT: /* --max-aspect <value> */

            if(_parse_ratio(&s->max_aspect, optarg) != HACKTV_OK)
            {
                fprintf(stderr, "Invalid maximum aspect\n");
                return false;
            }

            break;

        case _OPT_LETTERBOX: /* --letterbox */

            /* For compatiblity with CJ fork */
            s->fit_mode = AV_FIT_FIT;

            break;

        case _OPT_PILLARBOX: /* --pillarbox */

            /* For compatiblity with CJ fork */
            s->fit_mode = AV_FIT_FILL;

            break;

        case 'r': /* -r, --repeat */
            s->repeat = 1;
            break;

        case _OPT_SHUFFLE: /* --shuffle */
            s->shuffle = 1;
            break;

        case 'v': /* -v, --verbose */
            s->verbose = 1;
            break;

        case _OPT_TELETEXT: /* --teletext <path> */
            s->teletext = optarg;
            break;

        case _OPT_WSS: /* --wss <mode> */
            s->wss = optarg;
            break;

        case _OPT_VIDEOCRYPT: /* --videocrypt */
            s->videocrypt = optarg;
            break;

        case _OPT_VIDEOCRYPT2: /* --videocrypt2 */
            s->videocrypt2 = optarg;
            break;

        case _OPT_VIDEOCRYPTS: /* --videocrypts */
            s->videocrypts = optarg;
            break;

        case _OPT_SYSTER: /* --syster */
            s->syster = 1;
            break;

        case _OPT_SYSTERAUDIO: /* --systeraudio */
            s->systeraudio = 1;
            break;

        case _OPT_ACP: /* --acp */
            s->acp = 1;
            break;

        case _OPT_VITS: /* --vits */
            s->vits = 1;
            break;

        case _OPT_VITC: /* --vitc */
            s->vitc = 1;
            break;

        case _OPT_FILTER: /* --filter */
            s->filter = 1;
            break;

        case _OPT_NOCOLOUR: /* --nocolour / --nocolor */
            s->nocolour = 1;
            break;

        case _OPT_NOAUDIO: /* --noaudio */
            s->noaudio = 1;
            break;

        case _OPT_NONICAM: /* --nonicam */
            s->nonicam = 1;
            break;

        case _OPT_A2STEREO: /* --a2stereo */
            s->a2stereo = 1;
            break;

        case _OPT_SINGLE_CUT: /* --single-cut */
            s->scramble_video = 1;
            break;

        case _OPT_DOUBLE_CUT: /* --double-cut */
            s->scramble_video = 2;
            break;

        case _OPT_EUROCRYPT: /* --eurocrypt */
            s->eurocrypt = optarg;
            break;

        case _OPT_SCRAMBLE_AUDIO: /* --scramble-audio */
            s->scramble_audio = 1;
            break;

        case _OPT_CHID: /* --chid <id> */
            s->chid = strtol(optarg, NULL, 0);
            break;

        case _OPT_MAC_AUDIO_STEREO: /* --mac-audio-stereo */
            s->mac_audio_stereo = MAC_STEREO;
            break;

        case _OPT_MAC_AUDIO_MONO: /* --mac-audio-mono */
            s->mac_audio_stereo = MAC_MONO;
            break;

        case _OPT_MAC_AUDIO_HIGH_QUALITY: /* --mac-audio-high-quality */
            s->mac_audio_quality = MAC_HIGH_QUALITY;
            break;

        case _OPT_MAC_AUDIO_MEDIUM_QUALITY: /* --mac-audio-medium-quality */
            s->mac_audio_quality = MAC_MEDIUM_QUALITY;
            break;

        case _OPT_MAC_AUDIO_COMPANDED: /* --mac-audio-companded */
            s->mac_audio_companded = MAC_COMPANDED;
            break;

        case _OPT_MAC_AUDIO_LINEAR: /* --mac-audio-linear */
            s->mac_audio_companded = MAC_LINEAR;
            break;

        case _OPT_MAC_AUDIO_L1_PROTECTION: /* --mac-audio-l1-protection */
            s->mac_audio_protection = MAC_FIRST_LEVEL_PROTECTION;
            break;

        case _OPT_MAC_AUDIO_L2_PROTECTION: /* --mac-audio-l2-protection */
            s->mac_audio_protection = MAC_SECOND_LEVEL_PROTECTION;
            break;

        case _OPT_SIS: /* --sis <mode> */
            s->sis = optarg;
            break;

        case _OPT_SWAP_IQ: /* --swap-iq */
            s->swap_iq = 1;
            break;

        case _OPT_OFFSET: /* --offset <value Hz> */
            s->offset = (int64_t) strtod(optarg, NULL);
            break;

        case _OPT_PASSTHRU: /* --passthru <path> */
            s->passthru = optarg;
            break;

        case _OPT_INVERT_VIDEO: /* --invert-video */
            s->invert_video = 1;
            break;

        case _OPT_RAW_BB_FILE: /* --raw-bb-file <file> */
            s->raw_bb_file = optarg;
            break;

        case _OPT_RAW_BB_BLANKING: /* --raw-bb-blanking <value> */
            s->raw_bb_blanking_level = strtol(optarg, NULL, 0);
            break;

        case _OPT_RAW_BB_WHITE: /* --raw-bb-white <value> */
            s->raw_bb_white_level = strtol(optarg, NULL, 0);
            break;

        case _OPT_SECAM_FIELD_ID: /* --secam-field-id */
            s->secam_field_id = 1;
            break;

        case _OPT_JSON: /* --json */
            s->json = 1;
            break;

        case _OPT_FFMT: /* --ffmt <format> */
            s->ffmt = optarg;
            break;

        case _OPT_FOPTS: /* --fopts <option=value:[option2=value...]> */
            s->fopts = optarg;
            break;

        case 'f': /* -f, --frequency <value> */
            s->frequency = (uint64_t) strtod(optarg, NULL);
            break;

        case 'a': /* -a, --amp */
            s->amp = 1;
            break;

        case 'g': /* -g, --gain <value> */
            s->gain = atoi(optarg);
            break;

        case 'A': /* -A, --antenna <name> */
            s->antenna = optarg;
            break;

        case 't': /* -t, --type <type> */

            if(strcmp(optarg, "uint8") == 0)
            {
                s->file_type = RF_UINT8;
            }
            else if(strcmp(optarg, "int8") == 0)
            {
                s->file_type = RF_INT8;
            }
            else if(strcmp(optarg, "uint16") == 0)
            {
                s->file_type = RF_UINT16;
            }
            else if(strcmp(optarg, "int16") == 0)
            {
                s->file_type = RF_INT16;
            }
            else if(strcmp(optarg, "int32") == 0)
            {
                s->file_type = RF_INT32;
            }
            else if(strcmp(optarg, "float") == 0)
            {
                s->file_type = RF_FLOAT;
            }
            else
            {
                fprintf(stderr, "Unrecognised file data type.\n");
                return false;
            }

            break;

        case _OPT_VERSION: /* --version */
            print_version();
            return true;

        case _OPT_MODE:
            if (strcmp(optarg, "rx") == 0) {
                m_rxTxMode = RX_MODE;
            } else if (strcmp(optarg, "tx") == 0) {
                m_rxTxMode = TX_MODE;
            } else {
                fprintf(stderr, "Invalid mode. Use 'rx' or 'tx'.\n");
                return false;
            }
            break;

        case '?':
            print_usage();
            return true;
        }
    }
    return true;
}

void HackTvLib::setMicEnabled(bool newMicEnabled)
{
    micEnabled = newMicEnabled;
}

int16_t* convertFloatToInt16(const std::vector<float>& float_buffer) {
    size_t size = float_buffer.size();
    int16_t* fm_buffer = new int16_t[size];
    for (size_t i = 0; i < size; ++i) {
        fm_buffer[i] = static_cast<int16_t>(std::clamp(float_buffer[i], -1.0f, 1.0f) * 32767.0f);
    }
    return fm_buffer;
}

void HackTvLib::setReceivedDataCallback(DataCallback callback)
{
    m_dataCallback = std::move(callback);
}

void HackTvLib::clearCallbacks()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_logCallback = nullptr;
    m_dataCallback = nullptr;
}

void HackTvLib::emitReceivedData(const int8_t *data, size_t len)
{
    if (m_dataCallback) {
        try {
            m_dataCallback(data, len);
        } catch (...) {
            log("EXCEPTION in m_dataCallback!");
        }
    }
}

void HackTvLib::rfTxLoop()
{
    fprintf(stderr, "[rfTxLoop] Thread started\n");
    fflush(stderr);

    char *pre, *sub;
    size_t l;
    int r;

    do
    {
        // Check abort flag at the start of each iteration
        if (m_abort.load()) {
            fprintf(stderr, "[rfTxLoop] Abort flag detected (outer loop)\n");
            fflush(stderr);
            break;
        }

        if (s->shuffle)
        {
            // Shuffle the input source list
            for (int c = optind; c < m_argv.size() - 1; c++)
            {
                int l = c + (rand() % (m_argv.size() - c - (c == optind ? 1 : 0)));
                std::swap(m_argv[c], m_argv[l]);
            }
        }

        for (size_t c = optind; c < m_argv.size() && !m_abort.load(); c++)
        {
            // Check abort before processing each source
            if (m_abort.load()) {
                fprintf(stderr, "[rfTxLoop] Abort flag detected (source loop)\n");
                fflush(stderr);
                break;
            }

            char* pre = m_argv[c];
            char* sub = strchr(pre, ':');
            size_t l;
            if (sub != NULL)
            {
                l = sub - pre;
                sub++;
            }
            else
            {
                l = strlen(pre);
            }

            fprintf(stderr, "[rfTxLoop] Opening source: %s\n", pre);
            fflush(stderr);

            int r = HACKTV_ERROR;
            if (strncmp(pre, "test", l) == 0)
            {
                r = av_test_open(&s->vid.av);
            }
            else if (strncmp(pre, "ffmpeg", l) == 0)
            {
                r = av_ffmpeg_open(&s->vid.av, sub, s->ffmt, s->fopts, s->audio_gain);
            }
            else
            {
                r = av_ffmpeg_open(&s->vid.av, pre, s->ffmt, s->fopts, s->audio_gain);
            }

            if (r != HACKTV_OK)
            {
                fprintf(stderr, "[rfTxLoop] Failed to open source: %d\n", r);
                fflush(stderr);
                continue;
            }

            fprintf(stderr, "[rfTxLoop] Source opened, entering transmission loop\n");
            fflush(stderr);

            while (!m_abort.load())
            {
                // CRITICAL: Check abort flag at the START
                if (m_abort.load()) {
                    break;
                }

                size_t samples;
                int16_t* data = nullptr;

                // Wrap vid_next_line in try-catch
                try {
                    data = vid_next_line(&s->vid, &samples);
                } catch (const std::exception& e) {

                    break;
                } catch (...) {
                    break;
                }

                if (data == NULL) {
                    break;
                }

                // Check abort BEFORE writing
                if (m_abort.load()) {
                    break;
                }

                if (rf_write(&s->rf, data, samples) != RF_OK) {
                    break;
                }

                // Check abort AFTER writing
                if (m_abort.load()) {
                    break;
                }
            }

            fflush(stderr);

            if (m_signal.load() != 0)
            {
                fprintf(stderr, "[rfTxLoop] Caught signal %d\n", m_signal.load());
                fflush(stderr);
                m_signal.store(0);
            }

            fprintf(stderr, "[rfTxLoop] Closing AV source\n");
            fflush(stderr);
            av_close(&s->vid.av);

            // Break if abort was requested
            if (m_abort.load()) {
                fprintf(stderr, "[rfTxLoop] Breaking source loop due to abort\n");
                fflush(stderr);
                break;
            }
        }

        // Check abort before potentially repeating
        if (m_abort.load()) {
            fprintf(stderr, "[rfTxLoop] Breaking repeat loop due to abort\n");
            fflush(stderr);
            break;
        }

    } while (s->repeat && !m_abort.load());

    fprintf(stderr, "[rfTxLoop] Thread exiting normally\n");
    fflush(stderr);
}

void HackTvLib::rfRxLoop()
{
    const size_t SAMPLES_PER_READ = 131072;  // Number of I/Q pairs to read
    const size_t BUFFER_SIZE = SAMPLES_PER_READ * 2;  // Correct, as each I/Q pair is 2 int8_t values
    std::vector<int16_t> buffer(BUFFER_SIZE);

    while (!m_abort)
    {
        size_t bytes_read = rf_read(&s->rf, buffer.data(), BUFFER_SIZE);
        if (bytes_read != 0)
        {
            log("Bytes read: %zu", bytes_read);
        }
        QThread::msleep(10);
    }

    if (m_signal.load() != 0)
    {
        log("Caught signal %d", m_signal.load());
        m_signal.store(0);
    }
}

bool HackTvLib::stop()
{
    fprintf(stderr, "\n========================================\n");
    fprintf(stderr, "HackTvLib::stop() - ENTRY\n");
    fprintf(stderr, "========================================\n");
    fflush(stderr);

    // ========================================
    // RX MODE or FM TX (Microphone) - Stop HackRF/RTL-SDR
    // ========================================
    if (m_rxTxMode == RX_MODE || micEnabled) {
        fprintf(stderr, "[1] Stopping RX/FM TX mode...\n");
        fflush(stderr);

        if (!s) {
            fprintf(stderr, "'s' is NULL, already stopped?\n");
            fflush(stderr);
            return true; // Consider it success if already cleaned up
        }

        if (strcmp(s->output_type, "hackrf") == 0) {
            fprintf(stderr, "[2] Stopping HackRF...\n");
            fflush(stderr);

            if (hackRfDevice) {
                int result = hackRfDevice->stop();
                fprintf(stderr, "[3] hackRfDevice->stop() returned: %d\n", result);
                fflush(stderr);

                if (result == 0) {
                    delete hackRfDevice;
                    hackRfDevice = nullptr;

                    // Reset mic state
                    micEnabled = false;

                    fprintf(stderr, "[4] HackRF stopped\n");
                    fflush(stderr);
                    log("HackTvLib stopped.");
                    return true;
                } else {
                    fprintf(stderr, "Failed to stop HackRF\n");
                    fflush(stderr);
                    return false;
                }
            } else {
                fprintf(stderr, "hackRfDevice is NULL, already stopped?\n");
                fflush(stderr);
                return true; // Already stopped
            }
        }
        else if (strcmp(s->output_type, "rtlsdr") == 0) {
            fprintf(stderr, "[5] Stopping RTL-SDR...\n");
            fflush(stderr);

            if (rtlSdrDevice) {
                rtlSdrDevice->stop();
                delete rtlSdrDevice;
                rtlSdrDevice = nullptr;

                fprintf(stderr, "[6]RTL-SDR stopped\n");
                fflush(stderr);
                log("RTL-SDR stopped.");
                return true;
            } else {
                fprintf(stderr, "rtlSdrDevice is NULL, already stopped?\n");
                fflush(stderr);
                return true; // Already stopped
            }
        }

        fprintf(stderr, "Unknown output type\n");
        fflush(stderr);
        return false;
    }

    // ========================================
    // VIDEO TX MODE - Stop Thread
    // ========================================
    fprintf(stderr, "[7] Stopping VIDEO TX mode...\n");
    fflush(stderr);

    // CRITICAL: Check if already stopped
    if (!m_txThread.joinable()) {
        fprintf(stderr, "TX thread not joinable (already stopped or never started)\n");
        fflush(stderr);

        // Still set abort flag for safety
        m_abort.store(true, std::memory_order_release);

        // Return success - thread is already stopped
        return true;
    }

    // Set abort flag FIRST (before taking mutex)
    fprintf(stderr, "[8] Setting abort flag...\n");
    fflush(stderr);
    m_abort.store(true, std::memory_order_release);

    // Give thread time to see the flag
    fprintf(stderr, "[8.5] Waiting for thread to notice abort flag...\n");
    fflush(stderr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Try to join with timeout
    fprintf(stderr, "[9] Waiting for thread to finish (with timeout)...\n");
    fflush(stderr);

    bool joined = false;
    const int TIMEOUT_SECONDS = 3;

    // Simple polling approach
    auto start_time = std::chrono::steady_clock::now();

    while (m_txThread.joinable()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time
        ).count();

        if (elapsed >= TIMEOUT_SECONDS) {
            fprintf(stderr, "Thread did not exit within %d seconds, forcing cleanup...\n", TIMEOUT_SECONDS);
            fflush(stderr);
            break;
        }

        if (elapsed > 0 && elapsed % 1 == 0) {
            fprintf(stderr, "     Waiting... %lld/%d seconds\n", elapsed, TIMEOUT_SECONDS);
            fflush(stderr);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Check if thread is still joinable
    if (m_txThread.joinable()) {
        fprintf(stderr, "Thread still running after timeout, detaching...\n");
        fflush(stderr);

        // Last resort: detach the thread
        try {
            m_txThread.detach();
            fprintf(stderr, "Thread detached (will terminate with process)\n");
            fflush(stderr);
        } catch (const std::exception& e) {
            fprintf(stderr, "Failed to detach thread: %s\n", e.what());
            fflush(stderr);
        } catch (...) {
            fprintf(stderr, "Failed to detach thread: unknown exception\n");
            fflush(stderr);
        }
    } else {
        // Thread finished, join it
        fprintf(stderr, "[10] Thread is no longer joinable, attempting final join...\n");
        fflush(stderr);

        try {
            // This should be a no-op if thread already finished
            // But we need to call it to clean up thread resources
            if (m_txThread.joinable()) {
                m_txThread.join();
                fprintf(stderr, "[10.5]Thread joined successfully\n");
                fflush(stderr);
            }
            joined = true;
        } catch (const std::exception& e) {
            fprintf(stderr, "Exception joining thread: %s\n", e.what());
            fflush(stderr);
        } catch (...) {
            fprintf(stderr, "Unknown exception joining thread\n");
            fflush(stderr);
        }
    }

    // Clean up resources
    fprintf(stderr, "[11] Cleaning up resources...\n");
    fflush(stderr);

    try {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (s) {
            rf_close(&s->rf);
            vid_free(&s->vid);
        }
        av_ffmpeg_deinit();

        fprintf(stderr, "[12]Resources cleaned up\n");
        fflush(stderr);
    } catch (const std::exception& e) {
        fprintf(stderr, "Exception during cleanup: %s\n", e.what());
        fflush(stderr);
    } catch (...) {
        fprintf(stderr, "Unknown exception during cleanup\n");
        fflush(stderr);
    }

    fprintf(stderr, "[13] VIDEO TX STOPPED\n");
    fflush(stderr);

    log("HackTvLib stopped.");
    return true; // Always return true since thread is stopped
}

bool HackTvLib::start()
{
    fprintf(stderr, "\n========================================\n");
    fprintf(stderr, "HackTvLib::start() - ENTRY\n");
    fprintf(stderr, "========================================\n");
    fflush(stderr);

#ifdef WIN32
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
#endif

    // CRITICAL: Clean up existing hacktv_t if exists
    if (s) {
        fprintf(stderr, "[1] Cleaning up existing 's'...\n");
        fflush(stderr);
        free(s);
        s = nullptr;
    }

    // Allocate fresh hacktv_t
    s = (hacktv_t*)calloc(1, sizeof(hacktv_t));
    if (!s) {
        fprintf(stderr, "FATAL: calloc failed!\n");
        fflush(stderr);
        return false;
    }

    fprintf(stderr, "[2]Allocated hacktv_t at: %p\n", (void*)s);
    fflush(stderr);

    // Default configuration
    s->output_type = strdup("hackrf");
    s->output = nullptr;
    s->mode = strdup("b");
    s->samplerate = 16000000;
    s->pixelrate = 0;
    s->level = 1.0;
    s->deviation = -1;
    s->gamma = -1;
    s->interlace = 0;
    s->fit_mode = AV_FIT_FIT;

    s->min_aspect.num = 4;
    s->min_aspect.den = 3;
    s->max_aspect.num = 16;
    s->max_aspect.den = 9;

    s->repeat = 0;
    s->shuffle = 0;
    s->verbose = 0;
    s->teletext = nullptr;
    s->wss = nullptr;
    s->videocrypt = nullptr;
    s->videocrypt2 = nullptr;
    s->videocrypts = nullptr;
    s->eurocrypt = nullptr;
    s->syster = 0;
    s->systeraudio = 0;
    s->acp = 0;
    s->vits = 0;
    s->vitc = 0;
    s->filter = 0;
    s->nocolour = 0;
    s->noaudio = 0;
    s->nonicam = 0;
    s->a2stereo = 0;
    s->scramble_video = 0;
    s->scramble_audio = 0;
    s->chid = -1;
    s->mac_audio_stereo = MAC_STEREO;
    s->mac_audio_quality = MAC_HIGH_QUALITY;
    s->mac_audio_companded = MAC_COMPANDED;
    s->mac_audio_protection = MAC_FIRST_LEVEL_PROTECTION;
    s->swap_iq = 0;
    s->offset = 0;
    s->invert_video = 0;
    s->frequency = 0;
    s->amp = 0;
    s->gain = 0;
    s->antenna = nullptr;
    s->file_type = RF_INT16;
    s->sis = nullptr;
    s->passthru = nullptr;
    s->raw_bb_file = nullptr;
    s->raw_bb_blanking_level = 0;
    s->raw_bb_white_level = INT16_MAX;
    s->secam_field_id = 0;
    s->list_modes = 0;
    s->json = 0;
    s->ffmt = nullptr;
    s->fopts = nullptr;
    s->audio_gain = 3.0;

    m_rxTxMode = RX_MODE;
    m_abort = false;
    m_signal = 0;

    log("HackTvLib starting.");

    fprintf(stderr, "[3] Parsing arguments...\n");
    fflush(stderr);

    if (!parseArguments()) {
        fprintf(stderr, "parseArguments failed!\n");
        fflush(stderr);
        return false;
    }

    fprintf(stderr, "[4]Arguments parsed\n");
    fprintf(stderr, "     mode = %d (%s)\n", m_rxTxMode, getRxTxModeString(m_rxTxMode));
    fprintf(stderr, "     micEnabled = %d\n", micEnabled);
    fprintf(stderr, "     output_type = '%s'\n", s->output_type ? s->output_type : "NULL");
    fflush(stderr);

    log("Freq: %.3f MHz, Sample: %.1f MHz, Gain: %d, Amp: %s, RxTx: %s, Device: %s",
        s->frequency / 1e6,
        s->samplerate / 1e6,
        s->gain,
        getBoolString(s->amp),
        getRxTxModeString(m_rxTxMode),
        s->output_type);

    // ========================================
    // RX MODE
    // ========================================
    if (m_rxTxMode == RX_MODE) {
        fprintf(stderr, "[5] === RX MODE ===\n");
        fflush(stderr);

        if (strcmp(s->output_type, "hackrf") == 0) {
            fprintf(stderr, "[6] Setting up HackRF...\n");
            fflush(stderr);

            // Clean up existing device
            if (hackRfDevice) {
                fprintf(stderr, "     Deleting existing HackRfDevice...\n");
                fflush(stderr);
                try {
                    delete hackRfDevice;
                } catch (...) {
                    fprintf(stderr, "     Exception during delete\n");
                    fflush(stderr);
                }
                hackRfDevice = nullptr;
            }

            // Create new device
            try {
                hackRfDevice = new HackRfDevice();
                fprintf(stderr, "     Created HackRfDevice: %p\n", (void*)hackRfDevice);
                fflush(stderr);
            } catch (const std::exception& e) {
                fprintf(stderr, "Exception creating HackRfDevice: %s\n", e.what());
                fflush(stderr);
                return false;
            }

            // Set callback
            hackRfDevice->setDataCallback([this](const int8_t* data, size_t len) {
                this->dataReceived(data, len);
            });

            // Start device
            fprintf(stderr, "[7] Starting HackRF in RX mode...\n");
            fflush(stderr);

            if (hackRfDevice->start(rf_mode::RX) != RF_OK) {
                fprintf(stderr, "Failed to start HackRF\n");
                fflush(stderr);
                log("Could not open HackRF in RX. Please check the device.");
                delete hackRfDevice;
                hackRfDevice = nullptr;
                return false;
            }

            // Set parameters AFTER starting
            hackRfDevice->setMicEnabled(false);
            hackRfDevice->setSampleRate(s->samplerate);
            hackRfDevice->setFrequency(s->frequency);
            hackRfDevice->setAmpEnable(s->amp);

            fprintf(stderr, "[8]RX SUCCESS \n");
            fflush(stderr);
            log("HackTvLib started in RX mode with HackRF.");
            return true;
        }
        else if (strcmp(s->output_type, "rtlsdr") == 0) {
            fprintf(stderr, "[9] Setting up RTL-SDR...\n");
            fflush(stderr);

            // Clean up existing device
            if (rtlSdrDevice) {
                delete rtlSdrDevice;
                rtlSdrDevice = nullptr;
            }

            rtlSdrDevice = new RTLSDRDevice();
            rtlSdrDevice->setDataCallback([this](const int8_t* data, size_t len) {
                this->dataReceived(data, len);
            });

            if (rtlSdrDevice->initialize(s->samplerate, s->frequency)) {
                rtlSdrDevice->start();
                log("HackTvLib started in RX mode with RTL-SDR.");
                return true;
            } else {
                log("Could not open RTL-SDR. Please check the device.");
                delete rtlSdrDevice;
                rtlSdrDevice = nullptr;
                return false;
            }
        }

        fprintf(stderr, "Unknown output device: %s\n", s->output_type);
        fflush(stderr);
        return false;
    }

    // ========================================
    // TX MODE - FM TRANSMITTER (Microphone)
    // ========================================
    if (micEnabled && m_rxTxMode == TX_MODE) {
        fprintf(stderr, "[10] === FM TRANSMITTER MODE ===\n");
        fflush(stderr);

        if (strcmp(s->output_type, "hackrf") == 0) {
            // Clean up existing device
            if (hackRfDevice) {
                fprintf(stderr, "     Deleting existing HackRfDevice...\n");
                fflush(stderr);
                try {
                    // CRITICAL: Ensure mic is disabled before deleting
                    hackRfDevice->setMicEnabled(false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));

                    delete hackRfDevice;
                    fprintf(stderr, "    Deleted old device\n");
                    fflush(stderr);
                } catch (const std::exception& e) {
                    fprintf(stderr, "     Exception during delete: %s\n", e.what());
                    fflush(stderr);
                } catch (...) {
                    fprintf(stderr, "     Unknown exception during delete\n");
                    fflush(stderr);
                }
                hackRfDevice = nullptr;
            }

            // Create new device
            fprintf(stderr, "     Creating new HackRfDevice...\n");
            fflush(stderr);

            try {
                hackRfDevice = new HackRfDevice();
                fprintf(stderr, "    Created HackRfDevice: %p\n", (void*)hackRfDevice);
                fflush(stderr);
            } catch (const std::exception& e) {
                fprintf(stderr, "Exception creating HackRfDevice: %s\n", e.what());
                fflush(stderr);
                log("Failed to create HackRF device: %s", e.what());
                return false;
            } catch (...) {
                fprintf(stderr, "Unknown exception creating HackRfDevice\n");
                fflush(stderr);
                log("Failed to create HackRF device: unknown error");
                return false;
            }

            // CRITICAL: DON'T enable mic yet - device not started!
            fprintf(stderr, "[11] Starting HackRF in TX mode...\n");
            fprintf(stderr, "     About to call hackRfDevice->start(rf_mode::TX)...\n");
            fflush(stderr);

            int result = RF_ERROR;
            try {
                result = hackRfDevice->start(rf_mode::TX);
                fprintf(stderr, "     hackRfDevice->start() completed\n");
                fflush(stderr);
            } catch (const std::exception& e) {
                fprintf(stderr, "Exception in start(): %s\n", e.what());
                fflush(stderr);
                log("Failed to start HackRF: %s", e.what());
                delete hackRfDevice;
                hackRfDevice = nullptr;
                return false;
            } catch (...) {
                fprintf(stderr, "Unknown exception in start()\n");
                fflush(stderr);
                log("Failed to start HackRF: unknown exception");
                delete hackRfDevice;
                hackRfDevice = nullptr;
                return false;
            }

            fprintf(stderr, "[12] start() returned: %d\n", result);
            fflush(stderr);

            if (result != RF_OK) {
                fprintf(stderr, "Failed to start HackRF in TX mode (result=%d)\n", result);
                fflush(stderr);
                log("Could not open HackRF in TX. Please check the device. Error code: %d", result);

                // Clean up
                try {
                    delete hackRfDevice;
                } catch (...) {
                    fprintf(stderr, "     Exception during cleanup\n");
                    fflush(stderr);
                }
                hackRfDevice = nullptr;
                return false;
            }

            fprintf(stderr, "[13]Device started successfully\n");
            fflush(stderr);

            // Set basic parameters first
            fprintf(stderr, "[14] Setting parameters...\n");
            fflush(stderr);

            try {
                fprintf(stderr, "     Setting sample rate to %u...\n", s->samplerate);
                fflush(stderr);
                hackRfDevice->setSampleRate(s->samplerate);

                fprintf(stderr, "     Setting frequency to %llu...\n", (unsigned long long)s->frequency);
                fflush(stderr);
                hackRfDevice->setFrequency(s->frequency);

                fprintf(stderr, "     Setting amp enable to %d...\n", s->amp);
                fflush(stderr);
                hackRfDevice->setAmpEnable(s->amp);

                fprintf(stderr, "[15]Parameters set\n");
                fflush(stderr);

                // NOW enable microphone AFTER device is fully running
                fprintf(stderr, "[16] Enabling microphone...\n");
                fprintf(stderr, "     Waiting 500ms for device to stabilize...\n");
                fflush(stderr);

                // Give device time to stabilize
                std::this_thread::sleep_for(std::chrono::milliseconds(500));

                fprintf(stderr, "     About to call setMicEnabled(true)...\n");
                fflush(stderr);

                hackRfDevice->setMicEnabled(true);

                fprintf(stderr, "[17]Microphone enabled\n");
                fflush(stderr);

            } catch (const std::exception& e) {
                fprintf(stderr, "Exception setting parameters: %s\n", e.what());
                fflush(stderr);
                log("Failed to configure HackRF: %s", e.what());

                try {
                    hackRfDevice->stop();
                    delete hackRfDevice;
                } catch (...) {
                    fprintf(stderr, "     Exception during cleanup\n");
                    fflush(stderr);
                }
                hackRfDevice = nullptr;
                return false;
            } catch (...) {
                fprintf(stderr, "Unknown exception setting parameters\n");
                fflush(stderr);
                log("Failed to configure HackRF: unknown error");

                try {
                    hackRfDevice->stop();
                    delete hackRfDevice;
                } catch (...) {
                    fprintf(stderr, "     Exception during cleanup\n");
                    fflush(stderr);
                }
                hackRfDevice = nullptr;
                return false;
            }

            fprintf(stderr, "[18]FM TX SUCCESS \n");
            fflush(stderr);
            log("HackTvLib started in TX mode. Mic Enabled.");
            return true;
        }

        fprintf(stderr, "FM Transmitter only supports HackRF\n");
        fflush(stderr);
        log("FM Transmitter mode requires HackRF device");
        return false;
    }

    // ========================================
    // TX MODE - VIDEO TRANSMISSION
    // ========================================
    fprintf(stderr, "[19] === VIDEO TX MODE ===\n");
    fflush(stderr);

    // Validate input
    if (optind >= m_argv.size()) {
        fprintf(stderr, "No input specified\n");
        fflush(stderr);
        log("No input specified.");
        return false;
    }

    // Check if already running
    if (m_txThread.joinable()) {
        fprintf(stderr, "TX thread already running\n");
        fflush(stderr);
        return false;
    }

    // Setup video encoder
    fprintf(stderr, "[20] Setting up video encoder...\n");
    fflush(stderr);

    try {
        if (!setVideo()) {
            fprintf(stderr, "setVideo() failed\n");
            fflush(stderr);
            return false;
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "Exception in setVideo(): %s\n", e.what());
        fflush(stderr);
        return false;
    }

    fprintf(stderr, "[21]Video encoder ready\n");
    fflush(stderr);

    // Initialize AV
    fprintf(stderr, "[22] Initializing AV...\n");
    fflush(stderr);

    try {
        if (!initAv()) {
            fprintf(stderr, "initAv() failed\n");
            fflush(stderr);
            vid_free(&s->vid);
            return false;
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "Exception in initAv(): %s\n", e.what());
        fflush(stderr);
        vid_free(&s->vid);
        return false;
    }

    fprintf(stderr, "[23]AV initialized\n");
    fflush(stderr);

    // Open output device
    fprintf(stderr, "[24] Opening device...\n");
    fflush(stderr);

    try {
        if (!openDevice()) {
            fprintf(stderr, "openDevice() failed\n");
            fflush(stderr);
            vid_free(&s->vid);
            return false;
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "Exception in openDevice(): %s\n", e.what());
        fflush(stderr);
        vid_free(&s->vid);
        return false;
    }

    fprintf(stderr, "[25]Device opened\n");
    fflush(stderr);

    // Start TX thread
    fprintf(stderr, "[26] Starting TX thread...\n");
    fflush(stderr);

    m_abort.store(false);
    m_signal.store(0);

    try {
        m_txThread = std::thread([this]() {
            fprintf(stderr, "[TX THREAD] Started\n");
            fflush(stderr);

            try {
                rfTxLoop();
            } catch (const std::exception& e) {
                fprintf(stderr, "[TX THREAD] Exception: %s\n", e.what());
                fflush(stderr);
            } catch (...) {
                fprintf(stderr, "[TX THREAD] Unknown exception\n");
                fflush(stderr);
            }

            fprintf(stderr, "[TX THREAD] Exiting\n");
            fflush(stderr);
        });

        fprintf(stderr, "[27]VIDEO TX SUCCESS \n");
        fflush(stderr);
        log("HackTvLib started in TX mode.");
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to start TX thread: %s\n", e.what());
        fflush(stderr);
        rf_close(&s->rf);
        vid_free(&s->vid);
        return false;
    }
}
