#ifndef RADIORXPANEL_H
#define RADIORXPANEL_H

#include "ModePanel.h"
#include "core/RxEngine.h"
#include "core/WorkerQueue.h"
#include "core/BandPlan.h"
#include "dsp/fmdemodulator.h"
#include "dsp/amdemodulator.h"
#include "audiooutput.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QRadioButton>
#include <QSlider>
#include <QTimer>
#include <atomic>
#include <memory>
#include <mutex>

/*
 * Radio receive pipeline: IQ -> (FM or AM demodulator, own worker thread)
 * -> gain / soft clip / squelch -> AudioOutput (48 kHz stereo).
 *
 * sinkProcess() runs on the RxEngine thread and only measures the in-channel
 * power (for the squelch and the level readout) before handing the chunk to
 * the demod worker. Parameter setters are called from the GUI thread and are
 * serialised against demodulate() with m_demodMutex.
 */
class RadioRxSink : public QObject, public IRxSink
{
    Q_OBJECT
public:
    enum class Mode { WFM, NFM, AM };

    explicit RadioRxSink(QObject* parent = nullptr);
    ~RadioRxSink() override;

    void setMode(Mode m);
    Mode mode() const { return m_mode; }
    void setBandwidth(double hz);
    void setAudioGain(float gain);
    void setDeemphasisUs(float us);
    void setModIndex(float idx);
    void setFmnr(bool on);
    void setForceMono(bool on);
    void setSquelchDb(float db);          // <= -150 disables the squelch

    AudioOutput* audioOut() { return m_audioOut.get(); }

    float channelLevelDb() const { return m_levelDb.load(std::memory_order_relaxed); }
    bool  squelchOpen()    const { return m_squelchOpen.load(std::memory_order_relaxed); }
    bool  stereo()         const { return m_stereo.load(std::memory_order_relaxed); }
    uint64_t droppedChunks() const { return m_dropped.load(std::memory_order_relaxed); }

    // IRxSink (RxEngine thread)
    void sinkStart(uint32_t sampleRate) override;
    void sinkProcess(const std::complex<float>* iq, size_t count) override;
    void sinkStop() override;

signals:
    void stereoChanged(bool stereo);

private:
    void rebuildDemodulators();
    void demodChunk(const std::vector<std::complex<float>>& iq);

    std::mutex m_demodMutex;
    std::unique_ptr<FMDemodulator> m_fm;
    std::unique_ptr<AMDemodulator> m_am;
    std::unique_ptr<AudioOutput> m_audioOut;
    WorkerQueue m_worker{"radio-demod", 8};

    Mode     m_mode = Mode::WFM;
    uint32_t m_sampleRate = 2000000;
    double   m_bandwidth = 150000.0;
    float    m_audioGain = 3.0f;
    float    m_deemphUs = 50.0f;
    float    m_modIndex = 1.5f;
    bool     m_fmnr = false;
    bool     m_forceMono = false;
    std::atomic<float> m_squelchDb{-150.0f};
    std::atomic<float> m_levelDb{-120.0f};
    std::atomic<bool>  m_squelchOpen{true};
    std::atomic<bool>  m_stereo{false};
    std::atomic<uint64_t> m_dropped{0};
    float m_levelSmooth = -120.0f;
};

class RadioRxPanel : public ModePanel
{
    Q_OBJECT
public:
    explicit RadioRxPanel(QWidget* parent = nullptr);

    QString  title() const override { return "RADIO RX"; }
    IRxSink* rxSink() override { return m_sink; }
    uint32_t preferredSampleRate() const override { return 2000000; }
    bool     wantsFilterBox() const override { return true; }
    bool     meterRange(uint64_t, int& lowHz, int& highHz) const override
    {
        const int bw = static_cast<int>(m_bwSpin->value() * 1000.0);
        lowHz = -bw; highHz = bw;
        return true;
    }

    void onActivated() override;
    void onBandChanged(const BandInfo& band) override;
    void onFilterRangeChanged(int lowHz, int highHz) override;
    void onDeviceStarted() override;
    void onDeviceStopped() override;

    void saveSettings(QSettings& s) override;
    void loadSettings(QSettings& s) override;

private slots:
    void updateStatus();

private:
    void buildUi();
    void applyMode(RadioRxSink::Mode m, bool applyPreset);
    void applyModeDefaults(RadioRxSink::Mode m);   // bandwidth, de-emphasis, mod index, gain (HackTvGui presets)
    void setBandwidthHz(int hz, bool pushFilterBox);
    RadioRxSink::Mode currentMode() const;

    RadioRxSink* m_sink;

    QRadioButton* m_wfmRadio = nullptr;
    QRadioButton* m_nfmRadio = nullptr;
    QRadioButton* m_amRadio = nullptr;
    QCheckBox* m_followBandCheck = nullptr;

    QSlider* m_bwSlider = nullptr;
    QDoubleSpinBox* m_bwSpin = nullptr;
    QSlider* m_squelchSlider = nullptr;
    QLabel* m_squelchLabel = nullptr;
    QLabel* m_levelLabel = nullptr;

    QSlider* m_gainSlider = nullptr;
    QDoubleSpinBox* m_gainSpin = nullptr;
    QSlider* m_volumeSlider = nullptr;
    QLabel* m_volumeLabel = nullptr;

    QGroupBox* m_fmGroup = nullptr;
    QComboBox* m_deemphCombo = nullptr;
    QDoubleSpinBox* m_modIndexSpin = nullptr;
    QCheckBox* m_fmnrCheck = nullptr;
    QCheckBox* m_forceMonoCheck = nullptr;
    QLabel* m_stereoLabel = nullptr;

    QTimer* m_statusTimer = nullptr;
    bool m_running = false;
    bool m_updatingBw = false;
    int m_bwWfm = 150000, m_bwNfm = 12500, m_bwAm = 10500;
};

#endif // RADIORXPANEL_H
