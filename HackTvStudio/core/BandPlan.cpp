#include "BandPlan.h"
#include <vector>

namespace {

constexpr uint64_t kHz(double v) { return static_cast<uint64_t>(v * 1e3); }
constexpr uint64_t MHz(double v) { return static_cast<uint64_t>(v * 1e6); }

/*
 * ITU Region 1 (Europe / Turkey) oriented allocation table, 0 - 6 GHz.
 * Ordered by start frequency; first match wins, so put the narrower /
 * more specific band before any wider one that overlaps it.
 */
const std::vector<BandInfo>& table()
{
    static const std::vector<BandInfo> t = {
        { 0,             kHz(148.5),   "VLF / LF",                       Modulation::AM,      1000 },
        { kHz(148.5),    kHz(283.5),   "LW Broadcast",                   Modulation::AM,      9000 },
        { kHz(283.5),    kHz(526.5),   "NDB Beacons (LF/MF)",            Modulation::AM,      1000 },
        { kHz(526.5),    kHz(1606.5),  "MW Broadcast",                   Modulation::AM,      9000 },
        { MHz(1.8),      MHz(2.0),     "160 m Amateur",                  Modulation::LSB,     100 },
        { MHz(2.3),      MHz(2.495),   "120 m SW Broadcast",             Modulation::AM,      5000 },
        { MHz(3.2),      MHz(3.4),     "90 m SW Broadcast",              Modulation::AM,      5000 },
        { MHz(3.5),      MHz(3.8),     "80 m Amateur",                   Modulation::LSB,     100 },
        { MHz(3.9),      MHz(4.0),     "75 m SW Broadcast",              Modulation::AM,      5000 },
        { MHz(4.75),     MHz(5.06),    "60 m SW Broadcast",              Modulation::AM,      5000 },
        { MHz(5.9),      MHz(6.2),     "49 m SW Broadcast",              Modulation::AM,      5000 },
        { MHz(7.0),      MHz(7.2),     "40 m Amateur",                   Modulation::LSB,     100 },
        { MHz(7.2),      MHz(7.45),    "41 m SW Broadcast",              Modulation::AM,      5000 },
        { MHz(9.4),      MHz(9.9),     "31 m SW Broadcast",              Modulation::AM,      5000 },
        { MHz(10.1),     MHz(10.15),   "30 m Amateur",                   Modulation::USB,     100 },
        { MHz(11.6),     MHz(12.1),    "25 m SW Broadcast",              Modulation::AM,      5000 },
        { MHz(13.57),    MHz(13.87),   "22 m SW Broadcast",              Modulation::AM,      5000 },
        { MHz(14.0),     MHz(14.35),   "20 m Amateur",                   Modulation::USB,     100 },
        { MHz(15.1),     MHz(15.8),    "19 m SW Broadcast",              Modulation::AM,      5000 },
        { MHz(17.48),    MHz(17.9),    "16 m SW Broadcast",              Modulation::AM,      5000 },
        { MHz(18.068),   MHz(18.168),  "17 m Amateur",                   Modulation::USB,     100 },
        { MHz(21.0),     MHz(21.45),   "15 m Amateur",                   Modulation::USB,     100 },
        { MHz(21.45),    MHz(21.85),   "13 m SW Broadcast",              Modulation::AM,      5000 },
        { MHz(24.89),    MHz(24.99),   "12 m Amateur",                   Modulation::USB,     100 },
        { MHz(25.67),    MHz(26.1),    "11 m SW Broadcast",              Modulation::AM,      5000 },
        { MHz(26.965),   MHz(27.405),  "CB Radio (27 MHz)",              Modulation::AM,      10000 },
        { MHz(28.0),     MHz(29.7),    "10 m Amateur",                   Modulation::USB,     100 },
        { MHz(47.0),     MHz(68.0),    "VHF TV Band I",                  Modulation::TV,      7000000 },
        { MHz(50.0),     MHz(52.0),    "6 m Amateur",                    Modulation::USB,     1000 },
        { MHz(87.5),     MHz(108.0),   "FM Broadcast",                   Modulation::WFM,     100000 },
        { MHz(108.0),    MHz(118.0),   "VOR / ILS Navigation",           Modulation::AM,      25000 },
        { MHz(118.0),    MHz(137.0),   "Airband (Aviation)",             Modulation::AM,      25000 },
        { MHz(137.0),    MHz(138.0),   "Weather Satellite (APT)",        Modulation::NFM,     10000 },
        { MHz(144.0),    MHz(146.0),   "2 m Amateur",                    Modulation::NFM,     12500 },
        { MHz(146.0),    MHz(148.0),   "2 m Amateur (Region 2)",         Modulation::NFM,     12500 },
        { MHz(156.0),    MHz(162.025), "Marine VHF",                     Modulation::NFM,     25000 },
        { MHz(162.4),    MHz(162.55),  "NOAA Weather Radio",             Modulation::NFM,     25000 },
        { MHz(174.0),    MHz(230.0),   "VHF TV Band III",                Modulation::TV,      7000000 },
        { MHz(430.0),    MHz(440.0),   "70 cm Amateur",                  Modulation::NFM,     12500 },
        { MHz(446.0),    MHz(446.2),   "PMR446",                         Modulation::NFM,     12500 },
        { MHz(462.55),   MHz(467.725), "FRS / GMRS (US)",                Modulation::NFM,     12500 },
        { MHz(470.0),    MHz(790.0),   "UHF TV Band IV/V",               Modulation::TV,      8000000 },
        { MHz(790.0),    MHz(862.0),   "UHF TV Band V / LTE 800",        Modulation::TV,      8000000 },
        { MHz(863.0),    MHz(870.0),   "ISM 868 (SRD / LoRa)",           Modulation::Digital, 100000 },
        { MHz(880.0),    MHz(915.0),   "GSM 900 Uplink",                 Modulation::Digital, 200000 },
        { MHz(925.0),    MHz(960.0),   "GSM 900 Downlink",               Modulation::Digital, 200000 },
        { MHz(1025.0),   MHz(1035.0),  "SSR Interrogation (1030)",       Modulation::Digital, 1000000 },
        { MHz(1085.0),   MHz(1095.0),  "ADS-B / Mode S (1090)",          Modulation::Digital, 1000000 },
        { MHz(1164.0),   MHz(1215.0),  "GNSS L5 / E5",                   Modulation::Digital, 1000000 },
        { MHz(1215.0),   MHz(1240.0),  "GPS L2",                         Modulation::Digital, 1000000 },
        { MHz(1240.0),   MHz(1300.0),  "23 cm Amateur",                  Modulation::NFM,     25000 },
        { MHz(1559.0),   MHz(1610.0),  "GNSS L1 (GPS/Galileo/GLONASS)",  Modulation::Digital, 1000000 },
        { MHz(1710.0),   MHz(1785.0),  "GSM 1800 Uplink",                Modulation::Digital, 200000 },
        { MHz(1805.0),   MHz(1880.0),  "GSM 1800 Downlink",              Modulation::Digital, 200000 },
        { MHz(1920.0),   MHz(1980.0),  "UMTS / LTE 2100 Uplink",         Modulation::Digital, 1000000 },
        { MHz(2110.0),   MHz(2170.0),  "UMTS / LTE 2100 Downlink",       Modulation::Digital, 1000000 },
        { MHz(2300.0),   MHz(2400.0),  "LTE Band 40 / 13 cm Amateur",    Modulation::Digital, 1000000 },
        { MHz(2400.0),   MHz(2483.5),  "ISM 2.4 GHz (Wi-Fi / Bluetooth)", Modulation::Digital, 1000000 },
        { MHz(2500.0),   MHz(2690.0),  "LTE 2600",                       Modulation::Digital, 1000000 },
        { MHz(3400.0),   MHz(3800.0),  "5G NR n78",                      Modulation::Digital, 1000000 },
        { MHz(5150.0),   MHz(5350.0),  "Wi-Fi 5 GHz (U-NII-1/2A)",       Modulation::Digital, 1000000 },
        { MHz(5470.0),   MHz(5725.0),  "Wi-Fi 5 GHz (U-NII-2C)",         Modulation::Digital, 1000000 },
        { MHz(5650.0),   MHz(5850.0),  "6 cm Amateur",                   Modulation::NFM,     25000 },
        { MHz(5725.0),   MHz(5875.0),  "ISM 5.8 GHz / Wi-Fi",            Modulation::Digital, 1000000 },
    };
    return t;
}

} // namespace

namespace BandPlan {

BandInfo lookup(uint64_t hz)
{
    for (const BandInfo& b : table()) {
        if (hz >= b.startHz && hz < b.endHz)
            return b;
    }
    BandInfo custom;
    custom.startHz = hz;
    custom.endHz   = hz;
    custom.name    = "Unallocated / Custom";
    custom.mode    = Modulation::None;
    custom.stepHz  = 1000;
    return custom;
}

TvChannel tvChannel(uint64_t hz)
{
    TvChannel ch;

    auto fill = [&](int number, uint64_t start, uint64_t width) {
        ch.valid = true;
        ch.name = QString("E%1").arg(number);
        ch.channelStartHz = start;
        ch.channelWidthHz = width;
        ch.videoCarrierHz = start + 1250000ULL;
    };

    // VHF Band I: E2 47-54, E3 54-61, E4 61-68 (7 MHz)
    if (hz >= MHz(47) && hz < MHz(68)) {
        const int idx = static_cast<int>((hz - MHz(47)) / MHz(7));
        fill(2 + idx, MHz(47) + idx * MHz(7), MHz(7));
    }
    // VHF Band III: E5..E12, 174-230 (7 MHz)
    else if (hz >= MHz(174) && hz < MHz(230)) {
        const int idx = static_cast<int>((hz - MHz(174)) / MHz(7));
        fill(5 + idx, MHz(174) + idx * MHz(7), MHz(7));
    }
    // UHF Band IV/V: E21..E69, 470-862 (8 MHz)
    else if (hz >= MHz(470) && hz < MHz(862)) {
        const int idx = static_cast<int>((hz - MHz(470)) / MHz(8));
        fill(21 + idx, MHz(470) + idx * MHz(8), MHz(8));
    }

    return ch;
}

QString modulationName(Modulation m)
{
    switch (m) {
    case Modulation::AM:      return "AM";
    case Modulation::NFM:     return "Narrow FM";
    case Modulation::WFM:     return "Wide FM (Stereo)";
    case Modulation::LSB:     return "LSB";
    case Modulation::USB:     return "USB";
    case Modulation::TV:      return "Analogue TV (PAL)";
    case Modulation::Digital: return "Digital (spectrum only)";
    case Modulation::None:    break;
    }
    return "Unknown";
}

QString modulationShort(Modulation m)
{
    switch (m) {
    case Modulation::AM:      return "AM";
    case Modulation::NFM:     return "NFM";
    case Modulation::WFM:     return "WFM";
    case Modulation::LSB:     return "LSB";
    case Modulation::USB:     return "USB";
    case Modulation::TV:      return "TV";
    case Modulation::Digital: return "DIG";
    case Modulation::None:    break;
    }
    return "---";
}

QString formatFrequency(uint64_t hz)
{
    if (hz >= 1000000000ULL)
        return QString("%1 GHz").arg(hz / 1e9, 0, 'f', 6);
    if (hz >= 1000000ULL)
        return QString("%1 MHz").arg(hz / 1e6, 0, 'f', 3);
    if (hz >= 1000ULL)
        return QString("%1 kHz").arg(hz / 1e3, 0, 'f', 3);
    return QString("%1 Hz").arg(hz);
}

} // namespace BandPlan
