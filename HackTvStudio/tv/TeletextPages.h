#ifndef TELETEXTPAGES_H
#define TELETEXTPAGES_H

#include <QString>
#include <QStringList>
#include <cstdint>

/*
 * Writes a small teletext magazine (MRG .tti files) describing the current
 * transmission: index (100), transmission parameters (101), source (102),
 * options (103). hacktv loads the folder at START, so the pages are rewritten
 * every time the transmitter starts; the clock in the page header row is
 * generated live by hacktv.
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

/* Writes P100..P103 into `dir`; returns false (with `error`) when a file cannot be written. */
bool write(const QString& dir, const TeletextInfo& info, QString* error = nullptr);

}

#endif // TELETEXTPAGES_H
