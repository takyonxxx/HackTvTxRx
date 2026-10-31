#include "RadioRxPanel.h"
#include "Theme.h"

// Head unit shows short Turkish labels; the desktop keeps the original English ones.
#ifdef Q_OS_ANDROID
#define UI_TXT(android, desktop) QString::fromUtf8(android)
#else
#define UI_TXT(android, desktop) QString::fromUtf8(desktop)
#endif

#include <QButtonGroup>
#include <QPushButton>
#include <QDialog>
#include <algorithm>
#include <QDebug>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <cmath>

// ============================================================================
// RadioRxSink
// ============================================================================

RadioRxSink::RadioRxSink(QObject* parent)
    : QObject(parent)
    , m_audioOut(std::make_unique<AudioOutput>())
{
    // Both demodulators deliver interleaved stereo [L,R,L,R,...] at 48 kHz
    m_audioOut->setInputStereo(true);
    rebuildDemodulators();
}

RadioRxSink::~RadioRxSink()
{
    sinkStop();
}

void RadioRxSink::rebuildDemodulators()
{
    // Caller holds m_demodMutex (or no worker is running yet)
    const double rate = static_cast<double>(m_sampleRate);
    const double fmBw = (m_mode == Mode::WFM) ? std::max(m_bandwidth, 30000.0) : std::min(m_bandwidth, 25000.0);

    m_fm = std::make_unique<FMDemodulator>(rate, fmBw);
    m_fm->setOutputGain(m_audioGain);
    m_fm->setDeemphTau(m_deemphUs);
    m_fm->setRxModIndex(m_modIndex);
    m_fm->setFMNR(m_fmnr);
    m_fm->setForceMono(m_forceMono);
    connect(m_fm.get(), &FMDemodulator::stereoStatusChanged, this, [this](bool st) {
        m_stereo.store(st, std::memory_order_relaxed);
        emit stereoChanged(st);
    }, Qt::DirectConnection);

    m_am = std::make_unique<AMDemodulator>(rate, m_bandwidth);
}

void RadioRxSink::setMode(Mode m)
{
    std::lock_guard<std::mutex> lk(m_demodMutex);
    if (m_mode == m) return;
    m_mode = m;
    m_stereo.store(false);
    rebuildDemodulators();
}

void RadioRxSink::setBandwidth(double hz)
{
    std::lock_guard<std::mutex> lk(m_demodMutex);
    m_bandwidth = hz;
    // FMDemodulator decides NBFM/WBFM from the bandwidth: keep each mode on its side
    if (m_mode == Mode::WFM)      m_fm->setBandwidth(std::max(hz, 30000.0));
    else if (m_mode == Mode::NFM) m_fm->setBandwidth(std::min(hz, 25000.0));
    m_am->setBandwidth(hz);
}

void RadioRxSink::setAudioGain(float gain)
{
    std::lock_guard<std::mutex> lk(m_demodMutex);
    m_audioGain = gain;
    m_fm->setOutputGain(gain);
}

void RadioRxSink::setDeemphasisUs(float us)
{
    std::lock_guard<std::mutex> lk(m_demodMutex);
    m_deemphUs = us;
    m_fm->setDeemphTau(us);
}

void RadioRxSink::setModIndex(float idx)
{
    std::lock_guard<std::mutex> lk(m_demodMutex);
    m_modIndex = idx;
    m_fm->setRxModIndex(idx);
}

void RadioRxSink::setFmnr(bool on)
{
    std::lock_guard<std::mutex> lk(m_demodMutex);
    m_fmnr = on;
    m_fm->setFMNR(on);
}

void RadioRxSink::setForceMono(bool on)
{
    std::lock_guard<std::mutex> lk(m_demodMutex);
    m_forceMono = on;
    m_fm->setForceMono(on);
}

void RadioRxSink::setSquelchDb(float db)
{
    m_squelchDb.store(db, std::memory_order_relaxed);
}

void RadioRxSink::sinkStart(uint32_t sampleRate)
{
    m_worker.clear();
    m_worker.drain();
    std::lock_guard<std::mutex> lk(m_demodMutex);
    m_sampleRate = sampleRate;
    rebuildDemodulators();
    m_levelSmooth = -120.0f;
    m_dropped.store(0);
}

void RadioRxSink::sinkProcess(const std::complex<float>* iq, size_t count)
{
    // In-channel power: boxcar average over ~1/bandwidth, power of the averaged
    // vector (= a crude low-pass around DC, i.e. the tuned channel).
    {
        const double bw = std::max(m_bandwidth, 5000.0);
        int w = static_cast<int>(m_sampleRate / bw);
        w = std::max(2, std::min(w, 4096));
        double acc = 0.0;
        int hops = 0;
        for (size_t i = 0; i + static_cast<size_t>(w) <= count; i += static_cast<size_t>(w)) {
            float si = 0.0f, sq = 0.0f;
            for (int k = 0; k < w; ++k) { si += iq[i + k].real(); sq += iq[i + k].imag(); }
            si /= w; sq /= w;
            acc += static_cast<double>(si) * si + static_cast<double>(sq) * sq;
            ++hops;
        }
        if (hops > 0) {
            const float db = static_cast<float>(10.0 * std::log10(std::max(acc / hops, 1e-12)));
            m_levelSmooth += 0.3f * (db - m_levelSmooth);
            m_levelDb.store(m_levelSmooth, std::memory_order_relaxed);
            const float sq = m_squelchDb.load(std::memory_order_relaxed);
            if (sq > -149.0f) {
                // 2 dB hysteresis
                const bool open = m_squelchOpen.load(std::memory_order_relaxed);
                if (open && m_levelSmooth < sq - 1.0f)       m_squelchOpen.store(false);
                else if (!open && m_levelSmooth > sq + 1.0f) m_squelchOpen.store(true);
            } else {
                m_squelchOpen.store(true);
            }
        }
    }

    auto chunk = std::make_shared<std::vector<std::complex<float>>>(iq, iq + count);
    const uint64_t before = m_worker.dropped();
    m_worker.post([this, chunk] { demodChunk(*chunk); });
    if (m_worker.dropped() != before)
        m_dropped.fetch_add(1, std::memory_order_relaxed);
}

void RadioRxSink::demodChunk(const std::vector<std::complex<float>>& iq)
{
    std::vector<float> audio;   // interleaved stereo, 48 kHz
    {
        std::lock_guard<std::mutex> lk(m_demodMutex);
        if (m_mode == Mode::AM) {
            const std::vector<float> mono = m_am->demodulate(iq);
            audio.resize(mono.size() * 2);
            for (size_t i = 0; i < mono.size(); ++i) {
                float s = mono[i] * m_audioGain;
                if (s > 0.6f)       s = 0.6f + 0.4f * std::tanh((s - 0.6f) * 2.0f);
                else if (s < -0.6f) s = -0.6f + 0.4f * std::tanh((s + 0.6f) * 2.0f);
                audio[i * 2] = s;
                audio[i * 2 + 1] = s;
            }
        } else {
            audio = m_fm->demodulate(iq);   // output gain + limiter applied inside
        }
    }
    if (audio.empty()) return;

    if (!m_squelchOpen.load(std::memory_order_relaxed))
        std::fill(audio.begin(), audio.end(), 0.0f);   // keep the stream running, silent

    m_audioOut->enqueueAudio(audio);
}

void RadioRxSink::sinkStop()
{
    m_worker.clear();
    m_worker.drain();
}

// ============================================================================
// RadioRxPanel
// ============================================================================

RadioRxPanel::RadioRxPanel(QWidget* parent)
    : ModePanel(parent)
    , m_sink(new RadioRxSink(this))
{
    buildUi();

    connect(m_sink, &RadioRxSink::stereoChanged, this, [this](bool st) {
        m_stereoLabel->setText(st ? "STEREO" : "MONO");
        m_stereoLabel->setStyleSheet(st ? Themes::accentLabelCss()
                                        : Themes::dimLabelCss());
    }, Qt::QueuedConnection);

    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &RadioRxPanel::updateStatus);
    m_statusTimer->start(200);

    applyMode(RadioRxSink::Mode::WFM, true);
}

void RadioRxPanel::buildUi()
{
    // ---- Demodulation
    QGroupBox* modeGroup = new QGroupBox(UI_TXT("Demodülasyon", "Demodulation"), this);
    QVBoxLayout* ml = new QVBoxLayout(modeGroup);
#ifdef Q_OS_ANDROID
    ml->setSpacing(2);
    ml->setContentsMargins(8, 2, 8, 2);
    m_wfmRadio = new QRadioButton(QString::fromUtf8("WFM  (FM yayın)"), modeGroup);
    m_nfmRadio = new QRadioButton(QString::fromUtf8("NFM  (amatör / PMR)"), modeGroup);
    m_amRadio  = new QRadioButton(QString::fromUtf8("AM   (MW / havacılık)"), modeGroup);
#else
    m_wfmRadio = new QRadioButton("WFM  —  broadcast FM, stereo", modeGroup);
    m_nfmRadio = new QRadioButton("NFM  —  narrow FM (amateur, PMR, marine)", modeGroup);
    m_amRadio  = new QRadioButton("AM   —  MW/SW broadcast, airband, CB", modeGroup);
#endif
    QButtonGroup* bg = new QButtonGroup(modeGroup);
    bg->addButton(m_wfmRadio); bg->addButton(m_nfmRadio); bg->addButton(m_amRadio);
    m_wfmRadio->setChecked(true);
    ml->addWidget(m_wfmRadio);
    ml->addWidget(m_nfmRadio);
    ml->addWidget(m_amRadio);
    connect(m_wfmRadio, &QRadioButton::toggled, this, [this](bool on) { if (on) applyMode(RadioRxSink::Mode::WFM, true); });
    connect(m_nfmRadio, &QRadioButton::toggled, this, [this](bool on) { if (on) applyMode(RadioRxSink::Mode::NFM, true); });
    connect(m_amRadio,  &QRadioButton::toggled, this, [this](bool on) { if (on) applyMode(RadioRxSink::Mode::AM,  true); });

    m_followBandCheck = new QCheckBox(UI_TXT("Banda göre otomatik", "Follow band plan (auto demod)"), modeGroup);
    m_followBandCheck->setChecked(true);
    m_followBandCheck->setToolTip("Pick WFM / NFM / AM automatically from the tuned band");
    ml->addWidget(m_followBandCheck);

    QPushButton* defaultsBtn = new QPushButton(UI_TXT("Mod varsayılanlarına dön", "Reset mode defaults"), modeGroup);
    defaultsBtn->setToolTip("Bandwidth, de-emphasis, mod index and gain back to the preset for this mode");
    connect(defaultsBtn, &QPushButton::clicked, this, [this] { applyModeDefaults(currentMode()); });

    QHBoxLayout* stRow = new QHBoxLayout();
    stRow->addWidget(new QLabel(UI_TXT("Sinyal:", "Signal:"), modeGroup));
    m_stereoLabel = new QLabel("MONO", modeGroup);
    m_stereoLabel->setStyleSheet(Themes::dimLabelCss());
    stRow->addWidget(m_stereoLabel);
    stRow->addStretch();
#ifndef Q_OS_ANDROID
    ml->addWidget(defaultsBtn);
    ml->addLayout(stRow);
    ml->addStretch();
#endif

    // ---- Channel
    QGroupBox* chGroup = new QGroupBox(UI_TXT("Kanal", "Channel"), this);
    QVBoxLayout* cl = new QVBoxLayout(chGroup);
#ifdef Q_OS_ANDROID
    cl->setSpacing(4);
    cl->setContentsMargins(8, 2, 8, 2);
#endif

    QHBoxLayout* bwRow = new QHBoxLayout();
    bwRow->addWidget(new QLabel("Bandwidth:", chGroup));
    m_bwSlider = new QSlider(Qt::Horizontal, chGroup);
    m_bwSlider->setRange(2, 500);          // kHz
    m_bwSpin = new QDoubleSpinBox(chGroup);
    m_bwSpin->setRange(2.0, 500.0);
    m_bwSpin->setDecimals(1);
    m_bwSpin->setSingleStep(0.5);
    m_bwSpin->setSuffix(" kHz");
    m_bwSpin->setMaximumWidth(110);
    connect(m_bwSlider, &QSlider::valueChanged, this, [this](int kHz) {
        if (m_updatingBw) return;
        setBandwidthHz(kHz * 1000, true);
    });
    connect(m_bwSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double kHz) {
        if (m_updatingBw) return;
        setBandwidthHz(static_cast<int>(std::lround(kHz * 1000.0)), true);
    });
    bwRow->addWidget(m_bwSlider, 1);
    bwRow->addWidget(m_bwSpin);
    QLabel* bwHint = new QLabel("<i>Drag the filter edges on the spectrum, or set it here.</i>", chGroup);
    bwHint->setStyleSheet("QLabel { color: #9fb3c8; font-size: 8pt; }");

    QHBoxLayout* sqRow = new QHBoxLayout();
    sqRow->addWidget(new QLabel("Squelch:", chGroup));
    m_squelchSlider = new QSlider(Qt::Horizontal, chGroup);
    m_squelchSlider->setRange(-150, 0);    // -150 = off
    m_squelchSlider->setValue(-150);
    m_squelchLabel = new QLabel("OFF", chGroup);
    m_squelchLabel->setMinimumWidth(64);
    m_squelchLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_squelchSlider, &QSlider::valueChanged, this, [this](int v) {
        m_sink->setSquelchDb(static_cast<float>(v));
        m_squelchLabel->setText(v <= -150 ? "OFF" : QString("%1 dB").arg(v));
    });
    sqRow->addWidget(m_squelchSlider, 1);
    sqRow->addWidget(m_squelchLabel);

    QHBoxLayout* lvRow = new QHBoxLayout();
    lvRow->addWidget(new QLabel(UI_TXT("Seviye:", "Channel level:"), chGroup));
    m_levelLabel = new QLabel("--- dBFS", chGroup);
    m_levelLabel->setStyleSheet(Themes::accentLabelCss());
    lvRow->addWidget(m_levelLabel);
    lvRow->addStretch();

    // ---- Audio
    QGroupBox* auGroup = new QGroupBox(UI_TXT("Ses", "Audio"), this);
    QVBoxLayout* al = new QVBoxLayout(auGroup);
#ifdef Q_OS_ANDROID
    al->setSpacing(4);
    al->setContentsMargins(8, 2, 8, 2);
#endif

    QHBoxLayout* gRow = new QHBoxLayout();
    gRow->addWidget(new QLabel("Gain:", auGroup));
    m_gainSlider = new QSlider(Qt::Horizontal, auGroup);
    m_gainSlider->setRange(1, 100);        // x0.1
    m_gainSlider->setValue(30);
    m_gainSpin = new QDoubleSpinBox(auGroup);
    m_gainSpin->setRange(0.1, 10.0);
    m_gainSpin->setDecimals(1);
    m_gainSpin->setSingleStep(0.1);
    m_gainSpin->setValue(3.0);
    m_gainSpin->setMaximumWidth(80);
    connect(m_gainSlider, &QSlider::valueChanged, this, [this](int v) {
        m_gainSpin->blockSignals(true); m_gainSpin->setValue(v / 10.0); m_gainSpin->blockSignals(false);
        m_sink->setAudioGain(v / 10.0f);
    });
    connect(m_gainSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { m_gainSlider->setValue(static_cast<int>(std::lround(v * 10))); });
    gRow->addWidget(m_gainSlider, 1);
    gRow->addWidget(m_gainSpin);

    QHBoxLayout* vRow = new QHBoxLayout();
    vRow->addWidget(new QLabel(UI_TXT("Ses:", "Volume:"), auGroup));
    m_volumeSlider = new QSlider(Qt::Horizontal, auGroup);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(50);
    m_volumeLabel = new QLabel("50%", auGroup);
    m_volumeLabel->setMinimumWidth(40);
    m_volumeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_sink->audioOut()->setVolume(50);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int v) {
        m_sink->audioOut()->setVolume(v);
        m_volumeLabel->setText(QString("%1%").arg(v));
    });
    vRow->addWidget(m_volumeSlider, 1);
    vRow->addWidget(m_volumeLabel);

    // ---- FM options
    m_fmGroup = new QGroupBox("FM", this);
    QVBoxLayout* fl = new QVBoxLayout(m_fmGroup);

    QHBoxLayout* deRow = new QHBoxLayout();
    deRow->addWidget(new QLabel("De-emphasis:", m_fmGroup));
    m_deemphCombo = new QComboBox(m_fmGroup);
    m_deemphCombo->addItem("Off", 0);
    m_deemphCombo->addItem("50 µs (EU)", 50);
    m_deemphCombo->addItem("75 µs (US)", 75);
    m_deemphCombo->setCurrentIndex(1);
    connect(m_deemphCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { m_sink->setDeemphasisUs(static_cast<float>(m_deemphCombo->currentData().toInt())); });
    deRow->addWidget(m_deemphCombo, 1);
    fl->addLayout(deRow);

    QHBoxLayout* miRow = new QHBoxLayout();
    miRow->addWidget(new QLabel("Mod. index:", m_fmGroup));
    m_modIndexSpin = new QDoubleSpinBox(m_fmGroup);
    m_modIndexSpin->setRange(0.1, 5.0);
    m_modIndexSpin->setDecimals(2);
    m_modIndexSpin->setSingleStep(0.1);
    m_modIndexSpin->setValue(1.5);
    m_modIndexSpin->setToolTip("Scales the expected FM deviation (lower = louder)");
    connect(m_modIndexSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { m_sink->setModIndex(static_cast<float>(v)); });
    miRow->addWidget(m_modIndexSpin, 1);
    fl->addLayout(miRow);

    m_fmnrCheck = new QCheckBox("IF noise reduction (NFM)", m_fmGroup);
    connect(m_fmnrCheck, &QCheckBox::toggled, this, [this](bool on) { m_sink->setFmnr(on); });
    fl->addWidget(m_fmnrCheck);

    m_forceMonoCheck = new QCheckBox("Force mono (WFM)", m_fmGroup);
    connect(m_forceMonoCheck, &QCheckBox::toggled, this, [this](bool on) { m_sink->setForceMono(on); });
    fl->addWidget(m_forceMonoCheck);
    fl->addStretch();

#ifdef Q_OS_ANDROID
    // Head unit: one compact row under the spectrum = mode | squelch + level | volume | settings.
    // Bandwidth, audio gain, FM details and the preset reset live in the ⚙ dialog.
    QHBoxLayout* root = new QHBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(6, 4, 6, 4);
    root->addWidget(modeGroup, 2);

    cl->addLayout(sqRow);
    QHBoxLayout* lvRow2 = new QHBoxLayout();
    lvRow2->addLayout(lvRow, 1);
    lvRow2->addLayout(stRow);
    cl->addLayout(lvRow2);
    cl->addStretch();
    root->addWidget(chGroup, 3);

    m_volumeSlider->setMinimumHeight(34);
    m_volumeLabel->setStyleSheet("QLabel { font-size: 13pt; font-weight: bold; }");
    al->addLayout(vRow);
    al->addStretch();
    root->addWidget(auGroup, 2);

    // Dialog: channel width + audio gain + FM
    QGroupBox* dlgCh = new QGroupBox(QString::fromUtf8("Kanal genişliği"), this);
    QVBoxLayout* dcl = new QVBoxLayout(dlgCh);
    dcl->addLayout(bwRow);
    dcl->addWidget(bwHint);
    QGroupBox* dlgAu = new QGroupBox(QString::fromUtf8("Ses kazancı"), this);
    QVBoxLayout* dal = new QVBoxLayout(dlgAu);
    dal->addLayout(gRow);

    m_tabSettings = new QDialog(this);
    m_tabSettings->setWindowTitle(QString::fromUtf8("Radyo RX ayarları"));
    QVBoxLayout* dl = new QVBoxLayout(m_tabSettings);
    dl->addWidget(dlgCh);
    dl->addWidget(dlgAu);
    dl->addWidget(m_fmGroup);
    dl->addWidget(defaultsBtn);
    QPushButton* closeBtn = new QPushButton(QString::fromUtf8("Kapat"), m_tabSettings);
    connect(closeBtn, &QPushButton::clicked, m_tabSettings, &QDialog::accept);
    dl->addWidget(closeBtn);

    m_tabSettingsBtn = new QPushButton(QString::fromUtf8("⚙\nRX\nayarları"), this);
    m_tabSettingsBtn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_tabSettingsBtn->setMinimumWidth(80);
    connect(m_tabSettingsBtn, &QPushButton::clicked, this, &RadioRxPanel::openTabSettings);
    root->addWidget(m_tabSettingsBtn, 1);
#else
    // Desktop: the four groups stack 2x2 in the left half of the window.
    QGridLayout* root = new QGridLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(8, 8, 8, 8);
    root->addWidget(modeGroup, 0, 0);
    cl->addLayout(bwRow);
    cl->addWidget(bwHint);
    cl->addLayout(sqRow);
    cl->addLayout(lvRow);
    cl->addStretch();
    root->addWidget(chGroup, 0, 1);
    al->addLayout(gRow);
    al->addLayout(vRow);
    al->addStretch();
    root->addWidget(auGroup, 1, 0);
    root->addWidget(m_fmGroup, 1, 1);
    root->setRowStretch(2, 1);
    root->setColumnStretch(0, 1);
    root->setColumnStretch(1, 1);
#endif
}

void RadioRxPanel::openTabSettings()
{
    if (!m_tabSettings) return;
    m_tabSettings->resize(std::min(560, width() - 20), m_tabSettings->sizeHint().height());
    m_tabSettings->exec();
}

RadioRxSink::Mode RadioRxPanel::currentMode() const
{
    if (m_nfmRadio->isChecked()) return RadioRxSink::Mode::NFM;
    if (m_amRadio->isChecked())  return RadioRxSink::Mode::AM;
    return RadioRxSink::Mode::WFM;
}

void RadioRxPanel::applyMode(RadioRxSink::Mode m, bool applyPreset)
{
    m_sink->setMode(m);
    m_fmGroup->setEnabled(m != RadioRxSink::Mode::AM);
    m_fmnrCheck->setEnabled(m == RadioRxSink::Mode::NFM);
    m_forceMonoCheck->setEnabled(m == RadioRxSink::Mode::WFM);
    m_stereoLabel->setText(m == RadioRxSink::Mode::WFM ? "MONO" : "—");

    if (applyPreset) {
        // Each mode keeps its own last bandwidth; de-emphasis / mod index follow
        // the mode preset (same values HackTvGui applied on a mode change).
        int bw = m_bwWfm;
        if (m == RadioRxSink::Mode::NFM) bw = m_bwNfm;
        if (m == RadioRxSink::Mode::AM)  bw = m_bwAm;
        setBandwidthHz(bw, true);
        switch (m) {
        case RadioRxSink::Mode::WFM:
            m_deemphCombo->setCurrentIndex(1);   // 50 us
            m_modIndexSpin->setValue(1.5);
            m_gainSlider->setValue(30);          // 3.0
            m_fmnrCheck->setChecked(false);
            break;
        case RadioRxSink::Mode::NFM:
            // Tuned on air against a 2 m handheld: flat audio, low index, NR on
            m_deemphCombo->setCurrentIndex(0);   // off
            m_modIndexSpin->setValue(0.4);
            m_gainSlider->setValue(20);          // 2.0
            m_fmnrCheck->setChecked(true);
            break;
        case RadioRxSink::Mode::AM:
            m_deemphCombo->setCurrentIndex(0);
            m_modIndexSpin->setValue(1.0);
            m_gainSlider->setValue(30);
            m_fmnrCheck->setChecked(false);
            break;
        }
    }
}

void RadioRxPanel::applyModeDefaults(RadioRxSink::Mode m)
{
    switch (m) {
    case RadioRxSink::Mode::WFM: m_bwWfm = 150000; break;
    case RadioRxSink::Mode::NFM: m_bwNfm = 12500;  break;
    case RadioRxSink::Mode::AM:  m_bwAm  = 10500;  break;
    }
    m_forceMonoCheck->setChecked(false);
    applyMode(m, true);                  // gain, de-emphasis, mod index, NR come from the preset
}

void RadioRxPanel::setBandwidthHz(int hz, bool pushFilterBox)
{
    hz = std::max(2000, std::min(hz, 500000));
    switch (currentMode()) {
    case RadioRxSink::Mode::WFM: m_bwWfm = hz; break;
    case RadioRxSink::Mode::NFM: m_bwNfm = hz; break;
    case RadioRxSink::Mode::AM:  m_bwAm  = hz; break;
    }

    m_updatingBw = true;
    m_bwSlider->setValue(hz / 1000);
    m_bwSpin->setValue(hz / 1000.0);
    m_updatingBw = false;

    m_sink->setBandwidth(static_cast<double>(hz));
    if (pushFilterBox)
        emit filterRangeRequested(-hz, hz);
}

void RadioRxPanel::onActivated()
{
    // Re-arm the spectrum filter box with our current channel width
    const int bw = static_cast<int>(std::lround(m_bwSpin->value() * 1000.0));
    emit filterRangeRequested(-bw, bw);
}

void RadioRxPanel::onBandChanged(const BandInfo& band)
{
    if (!m_followBandCheck->isChecked()) return;
    switch (band.mode) {
    case Modulation::WFM: if (!m_wfmRadio->isChecked()) m_wfmRadio->setChecked(true); break;
    case Modulation::NFM: if (!m_nfmRadio->isChecked()) m_nfmRadio->setChecked(true); break;
    case Modulation::AM:
    case Modulation::LSB:   // no SSB demodulator yet: AM is the closest listen-mode
    case Modulation::USB:
        if (!m_amRadio->isChecked()) m_amRadio->setChecked(true);
        break;
    default:
        break;
    }
}

void RadioRxPanel::onFilterRangeChanged(int lowHz, int highHz)
{
    // Symmetric filter box: the channel bandwidth is the half-width
    const int bw = std::max(std::abs(lowHz), std::abs(highHz));
    setBandwidthHz(bw, false);
}

void RadioRxPanel::onDeviceStarted()
{
    m_running = true;
}

void RadioRxPanel::onDeviceStopped()
{
    m_running = false;
    m_levelLabel->setText("--- dBFS");
}

void RadioRxPanel::updateStatus()
{
    if (!m_running) {
        emit statusText("Radio RX idle");
        return;
    }
    const float lvl = m_sink->channelLevelDb();
    const bool open = m_sink->squelchOpen();
    m_levelLabel->setText(QString("%1 dBFS%2").arg(lvl, 0, 'f', 1).arg(open ? "" : "  (squelched)"));
    m_levelLabel->setStyleSheet(open ? Themes::accentLabelCss()
                                     : Themes::dimLabelCss());

    QString modeName = "WFM";
    if (m_nfmRadio->isChecked()) modeName = "NFM";
    if (m_amRadio->isChecked())  modeName = "AM";
    QString s = QString("Radio RX | %1 | BW %2 kHz | level %3 dBFS%4")
                    .arg(modeName)
                    .arg(m_bwSpin->value(), 0, 'f', 1)
                    .arg(lvl, 0, 'f', 1)
                    .arg(open ? "" : " | squelch closed");
    if (m_wfmRadio->isChecked()) s += m_sink->stereo() ? " | stereo" : " | mono";
    if (m_sink->droppedChunks()) s += QString(" | dropped %1").arg(m_sink->droppedChunks());
    emit statusText(s);
}

// ---------------------------------------------------------------------------

void RadioRxPanel::saveSettings(QSettings& s)
{
    s.beginGroup("radioRx");
    s.setValue("mode", m_nfmRadio->isChecked() ? 1 : (m_amRadio->isChecked() ? 2 : 0));
    s.setValue("bwWfm", m_bwWfm);
    s.setValue("bwNfm", m_bwNfm);
    s.setValue("bwAm", m_bwAm);
    s.setValue("followBand", m_followBandCheck->isChecked());
    s.setValue("squelch", m_squelchSlider->value());
    s.setValue("volume", m_volumeSlider->value());
    // gain / de-emphasis / mod index / NR are mode presets (re-applied on every mode change), not persisted
    s.setValue("forceMono", m_forceMonoCheck->isChecked());
    s.endGroup();
}

void RadioRxPanel::loadSettings(QSettings& s)
{
    s.beginGroup("radioRx");
    m_bwWfm = s.value("bwWfm", 150000).toInt();
    m_bwNfm = s.value("bwNfm", 12500).toInt();
    m_bwAm  = s.value("bwAm", 10500).toInt();
    m_followBandCheck->setChecked(s.value("followBand", true).toBool());
    m_squelchSlider->setValue(s.value("squelch", -150).toInt());
    m_volumeSlider->setValue(s.value("volume", 50).toInt());
    m_forceMonoCheck->setChecked(s.value("forceMono", false).toBool());
    const int mode = s.value("mode", 0).toInt();
    s.endGroup();

    // Push values whose widgets may not have emitted a change
    m_sink->setAudioGain(m_gainSlider->value() / 10.0f);
    m_sink->setSquelchDb(static_cast<float>(m_squelchSlider->value()));
    m_sink->audioOut()->setVolume(m_volumeSlider->value());

    QRadioButton* btn = (mode == 1) ? m_nfmRadio : (mode == 2) ? m_amRadio : m_wfmRadio;
    if (btn->isChecked()) applyMode(currentMode(), true);   // toggled() will not fire
    else btn->setChecked(true);
}
