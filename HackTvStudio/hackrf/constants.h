#ifndef CONSTANTS_H
#define CONSTANTS_H

#define _GHZ(x) ((uint64_t)(x) * 1000000000)
#define _MHZ(x) ((x) * 1000000)
#define _KHZ(x) ((x) * 1000)
#define _HZ(x) ((x) * 1)

#define DEFAULT_FREQUENCY              _MHZ(100)
#define DEFAULT_SAMPLE_RATE            _MHZ(2)
#define DEFAULT_AUDIO_SAMPLE_RATE      _KHZ(48)
#define DEFAULT_CUT_OFF                _KHZ(75)

#define HACKRF_RX_VGA_MAX_DB            62.0
#define HACKRF_RX_LNA_MAX_DB            40.0
#define HACKRF_RX_AMP_MAX_DB            11.0
#define HACKRF_TX_AMP_MAX_DB            47.0

#define DEFAULT_FFT_SIZE                1024
#define MIN_DB -60.0f
#define MAX_DB +20.0f
#define DEFAULT_FFT_SIZE                1024

#define M_PI 3.14159265358979323846
#define F_PI ((float)(M_PI))
#endif // CONSTANTS_H
