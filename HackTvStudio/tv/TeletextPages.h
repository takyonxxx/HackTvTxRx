#ifndef TELETEXTPAGES_H
#define TELETEXTPAGES_H

#include <QString>
#include <QStringList>
#include <cstdint>

/*
 * Writes one teletext page (MRG .tti file, page 100) describing the current
 * transmission: frequency, standard, picture format, sound, source and the
 * start time. hacktv loads the folder at START, so the page is rewritten every
 * time the transmitter starts; the clock in the page header row is generated
 * live by hacktv. (Teletext page numbers are 100-899: the first digit is the
 * magazine, so there is no page 1.)
 */
struct TeletextInfo {
    uint64_t frequencyHz = 0;
    QString  channel;           // "E22  UHF TV Band IV/V" (may be empty)
    QString  modeId;            // "b"
    QString  modeDescription;   // "PAL colour, 25 fps, 625 lines, AM (complex), 5.5 MHz FM audio"
    uint32_t sampleRate = 0;
    int      width = 0, height = 0;    // active picture handed to the encoder
    QString  fps;               // "25" / "29.97"
    QString  sound;             // "FM 5.5 MHz + NICAM"
    QString  wss;
    double   level = 1.0;
    QString  sourceKind;        // "Test pattern" / "Video file" / "USB camera"
    QString  sourceName;        // file name / camera name
    QString  sourceSound;       // microphone name (camera) or ""
    QStringList options;        // enabled option names
};

namespace TeletextPages {

/* Folder the pages are written to (created on demand). */
QString defaultDir();

/* Writes P100.tti into `dir`; returns false (with `error`) when the file cannot be written. */
bool write(const QString& dir, const TeletextInfo& info, QString* error = nullptr);

}

#endif // TELETEXTPAGES_H
