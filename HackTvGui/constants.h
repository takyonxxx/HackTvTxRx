#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <QDebug>
#include <complex>
#include <cmath>
#include <algorithm>

#define _GHZ(x) ((uint64_t)(x) * 1000000000)
#define _MHZ(x) ((x) * 1000000)
#define _KHZ(x) ((x) * 1000)
#define _HZ(x) ((x) * 1)

#define DEFAULT_FREQUENCY              _MHZ(100)
#define DEFAULT_RF_SAMPLE_RATE         _MHZ(20)
#define DEFAULT_AUDIO_SAMPLE_RATE      _KHZ(48)
#define DEFAULT_CUT_OFF                _KHZ(75)

#define M_PI 3.14159265358979323846
#define F_PI ((float)(M_PI))


inline QDebug operator<<(QDebug debug, const std::complex<float>& c)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << '(' << c.real() << ", " << c.imag() << ')';
    return debug;
}

#endif // CONSTANTS_H
