#ifndef MODEPANEL_H
#define MODEPANEL_H

#include <QWidget>
#include <QLayout>
#include <QString>
#include <QSettings>
#include <cstdint>
#include "core/BandPlan.h"

class IRxSink;
class ITxSource;

/*
 * One tab of HackTvStudio (Radio RX, Radio TX, TV RX, TV TX).
 *
 * StudioWindow owns the device, the RX engine, the spectrum display and the
 * frequency control; a ModePanel only owns its own controls and its DSP
 * pipeline. All virtuals are called on the GUI thread.
 */
class ModePanel : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;

    virtual QString title() const = 0;

    /* TX panels drive the device themselves (phase 3); RX panels supply a sink. */
    virtual bool     isTransmit() const { return false; }
    virtual IRxSink* rxSink() { return nullptr; }
    virtual ITxSource* txSource() { return nullptr; }

    /* 0 = keep whatever sample rate is selected. */
    virtual uint32_t preferredSampleRate() const { return 0; }

    /* Panels that demodulate a channel show the draggable filter box on the spectrum. */
    virtual bool wantsFilterBox() const { return false; }

    /* A layout inside the panel where the main window docks the spectrum /
     * waterfall while this panel is active (nullptr = keep the global spot). */
    virtual QLayout* spectrumSlot() { return nullptr; }

    /* Frequency range (Hz, relative to the tuned centre) whose power feeds the
     * S-meter. Return false to meter the whole span. */
    virtual bool meterRange(uint64_t tuneHz, int& lowHz, int& highHz) const
    {
        (void)tuneHz; (void)lowHz; (void)highHz;
        return false;
    }

    virtual void onActivated() {}
    virtual void onDeactivated() {}
    virtual void onFrequencyChanged(uint64_t hz) { (void)hz; }
    virtual void onBandChanged(const BandInfo& band) { (void)band; }
    virtual void onSampleRateChanged(uint32_t hz) { (void)hz; }
    /* The user dragged the spectrum filter box (Hz relative to the centre). */
    virtual void onFilterRangeChanged(int lowHz, int highHz) { (void)lowHz; (void)highHz; }
    virtual void onDeviceStarted() {}
    virtual void onDeviceStopped() {}

    virtual void saveSettings(QSettings& s) { (void)s; }
    virtual void loadSettings(QSettings& s) { (void)s; }

signals:
    /* Free-form text shown in the main status bar. */
    void statusText(const QString& text);
    /* Ask the main window to draw the spectrum filter box at [low, high] Hz around the centre. */
    void filterRangeRequested(int lowHz, int highHz);
};

#endif // MODEPANEL_H
