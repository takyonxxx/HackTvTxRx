#ifndef BANDPLAN_H
#define BANDPLAN_H

#include <QString>
#include <cstdint>

/* Demodulation / service type suggested for a band. */
enum class Modulation {
    None,       // unallocated or unknown
    AM,
    NFM,
    WFM,
    LSB,
    USB,
    TV,         // analogue television (PAL/SECAM/NTSC raster)
    Digital     // GSM/LTE/Wi-Fi/ADS-B/GNSS... (spectrum only)
};

struct BandInfo {
    uint64_t    startHz = 0;
    uint64_t    endHz   = 0;
    QString     name;              // "FM Broadcast", "UHF TV Band IV/V"...
    Modulation  mode    = Modulation::None;
    uint32_t    stepHz  = 1000;    // suggested tuning step
};

struct TvChannel {
    bool     valid = false;
    QString  name;                 // "E22"
    uint64_t videoCarrierHz = 0;   // 479.25 MHz for E22
    uint64_t channelStartHz = 0;
    uint64_t channelWidthHz = 0;   // 7 MHz VHF, 8 MHz UHF
};

namespace BandPlan {

/* Band covering the given frequency; an "Unallocated / Custom" entry if none. */
BandInfo lookup(uint64_t hz);

/* CCIR / European analogue TV channel (E2..E4, E5..E12, E21..E69). */
TvChannel tvChannel(uint64_t hz);

QString modulationName(Modulation m);     // "WFM", "PAL TV"...
QString modulationShort(Modulation m);    // "WFM", "TV"...

/* Human readable frequency: "479.250 MHz", "1.602 GHz", "162.400 kHz" */
QString formatFrequency(uint64_t hz);

/* Upper limit of the HackRF One tuning range. */
constexpr uint64_t HACKRF_MAX_HZ = 6000000000ULL;

} // namespace BandPlan

#endif // BANDPLAN_H
