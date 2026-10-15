#include "RadioTxPanel.h"

#include <QAudioDevice>
#include <QButtonGroup>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMediaDevices>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <cmath>


RadioTxPanel::RadioTxPanel(QWidget* parent)
    : ModePanel(parent)
{
    buildUi();

    m_mod.setPullSource([this](float* stereo, size_t frames) { return pullAudio(stereo, frames); });

    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &RadioTxPanel::updateStatus);
    m_statusTimer->start(100);

    applyMode();
}

RadioTxPanel::~RadioTxPanel()
{
    stopMic();
}

void RadioTxPanel::buildUi()
{
    QHBoxLayout* root = new QHBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(8, 8, 8, 8);

    // ---- Modulation
    QGroupBox* modGroup = new QGroupBox("Modulation", this);
    QVBoxLayout* ml = new QVBoxLayout(modGroup);
    m_wfmRadio = new QRadioButton("WFM  —  broadcast FM, stereo pilot", modGroup);
    m_nfmRadio = new QRadioButton("NFM  —  narrow FM (amateur, PMR)", modGroup);
    m_amRadio  = new QRadioButton("AM   —  DSB amplitude modulation", modGroup);
    QButtonGroup* bg = new QButtonGroup(modGroup);
    bg->addButton(m_wfmRadio); bg->addButton(m_nfmRadio); bg->addButton(m_amRadio);
    m_wfmRadio->setChecked(true);
    ml->addWidget(m_wfmRadio); ml->addWidget(m_nfmRadio); ml->addWidget(m_amRadio);
    for (QRadioButton* r : { m_wfmRadio, m_nfmRadio, m_amRadio })
        connect(r, &QRadioButton::toggled, this, [this](bool on) { if (on) applyMode(); });

    m_followBandCheck = new QCheckBox("Follow band plan (auto modulation)", modGroup);
    m_followBandCheck->setChecked(true);
    m_followBandCheck->setToolTip("FM broadcast -> WFM, amateur / PMR / marine -> NFM, airband / CB / MW -> AM");
    ml->addWidget(m_followBandCheck);

    QHBoxLayout* devRow = new QHBoxLayout();
    devRow->addWidget(new QLabel("Deviation:", modGroup));
    m_devSpin = new QDoubleSpinBox(modGroup);
    m_devSpin->setRange(0.5, 150.0);
    m_devSpin->setDecimals(1);
    m_devSpin->setSuffix(" kHz");
    m_devSpin->setValue(10.0);
    m_devSpin->setToolTip("WFM default 10 kHz; broadcast receivers accept up to 75 kHz, handhelds about 5-7 kHz");
    connect(m_devSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { m_mod.setDeviationHz(static_cast<float>(v * 1000.0)); });
    devRow->addWidget(m_devSpin, 1);
    ml->addLayout(devRow);

    QHBoxLayout* depthRow = new QHBoxLayout();
    depthRow->addWidget(new QLabel("AM depth:", modGroup));
    m_depthSlider = new QSlider(Qt::Horizontal, modGroup);
    m_depthSlider->setRange(10, 120);
    m_depthSlider->setValue(80);
    m_depthLabel = new QLabel("80%", modGroup);
    m_depthLabel->setMinimumWidth(40);
    connect(m_depthSlider, &QSlider::valueChanged, this, [this](int v) {
        m_depthLabel->setText(QString("%1%").arg(v));
        m_mod.setAmDepth(v / 100.0f);
    });
    depthRow->addWidget(m_depthSlider, 1);
    depthRow->addWidget(m_depthLabel);
    ml->addLayout(depthRow);

    QHBoxLayout* preRow = new QHBoxLayout();
    preRow->addWidget(new QLabel("Pre-emphasis:", modGroup));
    m_preemphCombo = new QComboBox(modGroup);
    m_preemphCombo->addItem("Off", 0);
    m_preemphCombo->addItem("50 µs (EU)", 50);
    m_preemphCombo->addItem("75 µs (US)", 75);
    m_preemphCombo->setCurrentIndex(1);
    connect(m_preemphCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { m_mod.setPreemphasisUs(static_cast<float>(m_preemphCombo->currentData().toInt())); });
    preRow->addWidget(m_preemphCombo, 1);
    ml->addLayout(preRow);

    QHBoxLayout* ampRow = new QHBoxLayout();
    ampRow->addWidget(new QLabel("Baseband level:", modGroup));
    m_ampSlider = new QSlider(Qt::Horizontal, modGroup);
    m_ampSlider->setRange(5, 100);
    m_ampSlider->setValue(50);
    m_ampLabel = new QLabel("50%", modGroup);
    m_ampLabel->setMinimumWidth(40);
    connect(m_ampSlider, &QSlider::valueChanged, this, [this](int v) {
        m_ampLabel->setText(QString("%1%").arg(v));
        m_mod.setAmplitude(v / 100.0f);
    });
    ampRow->addWidget(m_ampSlider, 1);
    ampRow->addWidget(m_ampLabel);
    ml->addLayout(ampRow);

    QLabel* legal = new QLabel("<i>Transmitting needs a licence for the band you use. Test into a dummy load.</i>", modGroup);
    legal->setWordWrap(true);
    legal->setStyleSheet("QLabel { color: #ff8800; font-size: 8pt; }");
    ml->addWidget(legal);
    ml->addStretch();
    root->addWidget(modGroup, 1);

    // ---- Source
    QGroupBox* srcGroup = new QGroupBox("Audio source", this);
    QVBoxLayout* sl = new QVBoxLayout(srcGroup);

    m_micRadio  = new QRadioButton("Microphone", srcGroup);
    m_fileRadio = new QRadioButton("Audio file", srcGroup);
    m_toneRadio = new QRadioButton("Test tone", srcGroup);
    QButtonGroup* sg = new QButtonGroup(srcGroup);
    sg->addButton(m_micRadio); sg->addButton(m_fileRadio); sg->addButton(m_toneRadio);
    m_micRadio->setChecked(true);

    sl->addWidget(m_micRadio);
    QHBoxLayout* micRow = new QHBoxLayout();
    m_micCombo = new QComboBox(srcGroup);
    for (const QAudioDevice& d : QMediaDevices::audioInputs())
        m_micCombo->addItem(d.description(), d.id());
    micRow->addWidget(m_micCombo, 1);
    m_pttButton = new QPushButton("PUSH TO TALK", srcGroup);
    m_pttButton->setCheckable(true);
    m_pttButton->setMinimumHeight(36);
    m_pttButton->setToolTip("Toggle: microphone audio is sent while this is down, silence otherwise");
    connect(m_pttButton, &QPushButton::toggled, this, [this](bool on) {
        m_pttButton->setStyleSheet(on ? "QPushButton { background: #a03030; color: white; }" : QString());
        m_pttButton->setText(on ? "TALKING  (click to stop)" : "PUSH TO TALK");
    });
    micRow->addWidget(m_pttButton);
    sl->addLayout(micRow);

    sl->addWidget(m_fileRadio);
    QHBoxLayout* fileRow = new QHBoxLayout();
    m_fileEdit = new QLineEdit(srcGroup);
    m_fileEdit->setPlaceholderText("mp3 / wav / flac / ogg ...");
    m_browseButton = new QPushButton("Browse", srcGroup);
    connect(m_browseButton, &QPushButton::clicked, this, &RadioTxPanel::browseFile);
    connect(m_fileEdit, &QLineEdit::editingFinished, this, [this] { decodeFile(m_fileEdit->text()); });
    fileRow->addWidget(m_fileEdit, 1);
    fileRow->addWidget(m_browseButton);
    sl->addLayout(fileRow);
    QHBoxLayout* fileRow2 = new QHBoxLayout();
    m_loopCheck = new QCheckBox("Loop", srcGroup);
    m_loopCheck->setChecked(true);
    connect(m_loopCheck, &QCheckBox::toggled, this, [this](bool on) { m_pullLoop.store(on); });
    m_fileInfo = new QLabel("No file", srcGroup);
    m_fileInfo->setStyleSheet("QLabel { color: #708090; font-size: 8pt; }");
    fileRow2->addWidget(m_loopCheck);
    fileRow2->addWidget(m_fileInfo, 1);
    sl->addLayout(fileRow2);

    QHBoxLayout* toneRow = new QHBoxLayout();
    toneRow->addWidget(m_toneRadio);
    m_toneSpin = new QDoubleSpinBox(srcGroup);
    m_toneSpin->setRange(50.0, 15000.0);
    m_toneSpin->setValue(1000.0);
    m_toneSpin->setSuffix(" Hz");
    connect(m_toneSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { m_toneHz.store(static_cast<float>(v)); });
    toneRow->addWidget(m_toneSpin);
    toneRow->addStretch();
    sl->addLayout(toneRow);

    QHBoxLayout* gainRow = new QHBoxLayout();
    gainRow->addWidget(new QLabel("Audio gain:", srcGroup));
    m_gainSlider = new QSlider(Qt::Horizontal, srcGroup);
    m_gainSlider->setRange(1, 200);     // x0.1
    m_gainSlider->setValue(10);
    m_gainLabel = new QLabel("x1.0", srcGroup);
    m_gainLabel->setMinimumWidth(44);
    connect(m_gainSlider, &QSlider::valueChanged, this, [this](int v) {
        m_gainLabel->setText(QString("x%1").arg(v / 10.0, 0, 'f', 1));
        m_mod.setAudioGain(v / 10.0f);
    });
    gainRow->addWidget(m_gainSlider, 1);
    gainRow->addWidget(m_gainLabel);
    sl->addLayout(gainRow);

    m_agcCheck = new QCheckBox("Auto level (AGC)", srcGroup);
    m_agcCheck->setChecked(true);
    connect(m_agcCheck, &QCheckBox::toggled, this, [this](bool on) { m_mod.setAgc(on); });
    sl->addWidget(m_agcCheck);

    QHBoxLayout* vuRow = new QHBoxLayout();
    vuRow->addWidget(new QLabel("Input:", srcGroup));
    m_vu = new QProgressBar(srcGroup);
    m_vu->setRange(0, 100);
    m_vu->setTextVisible(false);
    m_vu->setFixedHeight(14);
    vuRow->addWidget(m_vu, 1);
    sl->addLayout(vuRow);

    m_statusLabel = new QLabel("", srcGroup);
    m_statusLabel->setStyleSheet("QLabel { color: #708090; font-size: 8pt; }");
    m_statusLabel->setWordWrap(true);
    sl->addWidget(m_statusLabel);
    sl->addStretch();
    root->addWidget(srcGroup, 2);
}

void RadioTxPanel::applyMode()
{
    RadioModulator::Mode m = RadioModulator::Mode::WFM;
    if (m_nfmRadio->isChecked()) m = RadioModulator::Mode::NFM;
    if (m_amRadio->isChecked())  m = RadioModulator::Mode::AM;
    m_mod.setMode(m);

    const bool fm = (m != RadioModulator::Mode::AM);
    m_devSpin->setEnabled(fm);
    m_depthSlider->setEnabled(!fm);
    m_preemphCombo->setEnabled(fm);
    // Keep the user's deviation; only pull an obviously wide value down when entering NFM
    if (m == RadioModulator::Mode::NFM && m_devSpin->value() > 10.0) m_devSpin->setValue(5.0);
    m_mod.setDeviationHz(static_cast<float>(m_devSpin->value() * 1000.0));
    m_mod.setAmDepth(m_depthSlider->value() / 100.0f);
    m_mod.setPreemphasisUs(static_cast<float>(m_preemphCombo->currentData().toInt()));
    m_mod.setAmplitude(m_ampSlider->value() / 100.0f);
    m_mod.setAudioGain(m_gainSlider->value() / 10.0f);
    m_mod.setAgc(m_agcCheck->isChecked());
}

// ---------------------------------------------------------------------------
// Audio sources

void RadioTxPanel::startMic()
{
    stopMic();
    QAudioDevice dev = QMediaDevices::defaultAudioInput();
    const QByteArray id = m_micCombo->currentData().toByteArray();
    for (const QAudioDevice& d : QMediaDevices::audioInputs())
        if (d.id() == id) dev = d;
    if (dev.isNull()) { m_statusLabel->setText("No microphone found"); return; }

    QAudioFormat fmt;
    fmt.setSampleRate(static_cast<int>(RadioModulator::AUDIO_RATE));
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Float);
    if (!dev.isFormatSupported(fmt)) {
        fmt = dev.preferredFormat();
        fmt.setChannelCount(1);
        fmt.setSampleFormat(QAudioFormat::Float);
    }
    m_micSource = std::make_unique<QAudioSource>(dev, fmt);
    m_micSource->setBufferSize(4096 * 4);
    m_micIo = m_micSource->start();
    if (!m_micIo) { m_micSource.reset(); m_statusLabel->setText("Microphone could not be opened"); return; }

    const int micRate = fmt.sampleRate();
    connect(m_micIo, &QIODevice::readyRead, this, [this, micRate] {
        const QByteArray data = m_micIo->readAll();
        if (!m_pttButton->isChecked() || data.isEmpty()) return;    // PTT released: drop
        const float* p = reinterpret_cast<const float*>(data.constData());
        const size_t n = data.size() / sizeof(float);
        std::vector<float> stereo;
        if (micRate == static_cast<int>(RadioModulator::AUDIO_RATE)) {
            stereo.resize(n * 2);
            for (size_t i = 0; i < n; ++i) stereo[2 * i] = stereo[2 * i + 1] = p[i];
        } else {
            // Device insisted on another rate: resample linearly to 48 kHz
            const double ratio = static_cast<double>(micRate) / RadioModulator::AUDIO_RATE;
            const size_t out = static_cast<size_t>(n / ratio);
            stereo.resize(out * 2);
            for (size_t i = 0; i < out; ++i) {
                const double pos = i * ratio;
                const size_t idx = std::min(static_cast<size_t>(pos), n - 1);
                const size_t nxt = std::min(idx + 1, n - 1);
                const float f = static_cast<float>(pos - idx);
                stereo[2 * i] = stereo[2 * i + 1] = p[idx] + (p[nxt] - p[idx]) * f;
            }
        }
        m_mod.pushAudio(stereo.data(), stereo.size());
    });
}

void RadioTxPanel::stopMic()
{
    if (m_micSource) {
        m_micSource->stop();
        m_micSource.reset();
        m_micIo = nullptr;
    }
}

void RadioTxPanel::browseFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    const QString f = QFileDialog::getOpenFileName(this, "Audio file", dir,
                                                   "Audio (*.mp3 *.wav *.flac *.ogg *.m4a *.aac *.wma);;All files (*)");
    if (f.isEmpty()) return;
    m_fileEdit->setText(f);
    decodeFile(f);
}

void RadioTxPanel::decodeFile(const QString& path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) return;
    if (m_decoder) { m_decoder->stop(); m_decoder->deleteLater(); m_decoder = nullptr; }
    m_fileData.clear();
    m_filePos = 0;
    m_fileReady = false;
    m_fileInfo->setText("Decoding...");

    m_decoder = new QAudioDecoder(this);
    QAudioFormat df;
    df.setSampleRate(static_cast<int>(RadioModulator::AUDIO_RATE));
    df.setChannelCount(2);
    df.setSampleFormat(QAudioFormat::Int16);
    m_decoder->setAudioFormat(df);
    m_decoder->setSource(QUrl::fromLocalFile(path));
    connect(m_decoder, &QAudioDecoder::bufferReady, this, [this] {
        const QAudioBuffer buf = m_decoder->read();
        if (!buf.isValid()) return;
        const int16_t* d = buf.constData<int16_t>();
        m_fileData.insert(m_fileData.end(), d, d + buf.sampleCount());
    });
    connect(m_decoder, &QAudioDecoder::finished, this, [this] {
        m_fileReady = !m_fileData.empty();
        const double sec = m_fileData.size() / 2.0 / RadioModulator::AUDIO_RATE;
        m_fileInfo->setText(m_fileReady ? QString("%1  (%2:%3)").arg(QFileInfo(m_fileEdit->text()).fileName())
                                              .arg(static_cast<int>(sec) / 60).arg(static_cast<int>(sec) % 60, 2, 10, QChar('0'))
                                        : "Decode failed");
        m_decoder->deleteLater();
        m_decoder = nullptr;
    });
    connect(m_decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [this](QAudioDecoder::Error) {
        m_fileInfo->setText("Decode error: " + m_decoder->errorString());
    });
    m_decoder->start();
}

/* TX-thread pull source: file samples or the test tone, straight into the modulator.
 * m_fileData is not touched by the GUI while transmitting (source widgets are disabled). */
size_t RadioTxPanel::pullAudio(float* stereo, size_t frames)
{
    const int kind = m_pullKind.load(std::memory_order_acquire);
    if (kind == 1) {
        if (m_fileData.size() < 2) return 0;
        size_t pos = m_filePos.load(std::memory_order_relaxed);
        size_t done = 0;
        while (done < frames) {
            if (pos + 1 >= m_fileData.size()) {
                if (!m_pullLoop.load()) break;
                pos = 0;
            }
            stereo[2 * done]     = m_fileData[pos]     / 32768.0f;
            stereo[2 * done + 1] = m_fileData[pos + 1] / 32768.0f;
            pos += 2;
            ++done;
        }
        m_filePos.store(pos, std::memory_order_relaxed);
        return done;
    }
    if (kind == 2) {
        const double step = 2.0 * M_PI * m_toneHz.load() / RadioModulator::AUDIO_RATE;
        for (size_t i = 0; i < frames; ++i) {
            const float s = 0.5f * static_cast<float>(std::sin(m_tonePhase));
            stereo[2 * i] = stereo[2 * i + 1] = s;
            m_tonePhase += step;
            if (m_tonePhase > 2.0 * M_PI) m_tonePhase -= 2.0 * M_PI;
        }
        return frames;
    }
    return 0;   // microphone: ring only
}

void RadioTxPanel::setSourceWidgetsEnabled(bool on)
{
    m_micRadio->setEnabled(on);
    m_fileRadio->setEnabled(on);
    m_toneRadio->setEnabled(on);
    m_fileEdit->setEnabled(on);
    m_browseButton->setEnabled(on);
    m_micCombo->setEnabled(on);
}

// ---------------------------------------------------------------------------

void RadioTxPanel::onActivated()
{
}

void RadioTxPanel::onBandChanged(const BandInfo& band)
{
    if (!m_followBandCheck->isChecked() || m_running) return;   // never switch while on the air
    switch (band.mode) {
    case Modulation::WFM: if (!m_wfmRadio->isChecked()) m_wfmRadio->setChecked(true); break;
    case Modulation::NFM: if (!m_nfmRadio->isChecked()) m_nfmRadio->setChecked(true); break;
    case Modulation::AM:
    case Modulation::LSB:
    case Modulation::USB:
        if (!m_amRadio->isChecked()) m_amRadio->setChecked(true);
        break;
    default:
        break;
    }
}

ITxSource* RadioTxPanel::txSource()
{
    // Called just before the TX engine starts. File / tone are pulled by the TX
    // thread itself; only the microphone pushes through the ring.
    m_running = true;
    m_mod.clearAudio();
    m_filePos.store(0);
    m_tonePhase = 0.0;
    m_pullLoop.store(m_loopCheck->isChecked());
    m_toneHz.store(static_cast<float>(m_toneSpin->value()));
    m_pullKind.store(m_fileRadio->isChecked() ? 1 : (m_toneRadio->isChecked() ? 2 : 0), std::memory_order_release);
    setSourceWidgetsEnabled(false);      // m_fileData must stay frozen while the TX thread reads it
    if (m_micRadio->isChecked()) startMic();
    return &m_mod;
}

void RadioTxPanel::onDeviceStarted()
{
    m_running = true;
}

void RadioTxPanel::onDeviceStopped()
{
    m_running = false;
    m_pullKind.store(0);
    stopMic();
    setSourceWidgetsEnabled(true);
    m_pttButton->setChecked(false);
    m_vu->setValue(0);
}

void RadioTxPanel::updateStatus()
{
    const float db = m_mod.inputLevelDb();
    m_vu->setValue(std::clamp(static_cast<int>((db + 60.0f) * (100.0f / 60.0f)), 0, 100));

    if (!m_running) {
        emit statusText("Radio TX idle");
        m_statusLabel->setText(m_fileRadio->isChecked() && !m_fileReady ? "Choose an audio file" : "Press START to transmit");
        return;
    }
    QString mode = m_wfmRadio->isChecked() ? "WFM" : (m_nfmRadio->isChecked() ? "NFM" : "AM");
    QString src = m_micRadio->isChecked() ? (m_pttButton->isChecked() ? "mic (talking)" : "mic (PTT released)")
                : m_fileRadio->isChecked() ? "file" : "tone";
    QString s = QString("Radio TX | %1 | %2 | input %3 dBFS").arg(mode, src).arg(db, 0, 'f', 0);
    if (m_fileRadio->isChecked() && m_fileReady)
        s += QString(" | %1%").arg(100.0 * m_filePos.load() / std::max<size_t>(1, m_fileData.size()), 0, 'f', 0);
    if (m_mod.underrunFrames())
        s += QString(" | audio underrun %1 ms").arg(m_mod.underrunFrames() * 1000 / RadioModulator::AUDIO_RATE);
    emit statusText(s);
    m_statusLabel->setText("Transmitting " + mode + " from " + src);
}

void RadioTxPanel::saveSettings(QSettings& s)
{
    s.beginGroup("radioTx");
    s.setValue("mode", m_nfmRadio->isChecked() ? 1 : (m_amRadio->isChecked() ? 2 : 0));
    s.setValue("followBand", m_followBandCheck->isChecked());
    s.setValue("source", m_fileRadio->isChecked() ? 1 : (m_toneRadio->isChecked() ? 2 : 0));
    s.setValue("deviation", m_devSpin->value());
    s.setValue("depth", m_depthSlider->value());
    s.setValue("preemph", m_preemphCombo->currentIndex());
    s.setValue("amplitude", m_ampSlider->value());
    s.setValue("gain", m_gainSlider->value());
    s.setValue("agc", m_agcCheck->isChecked());
    s.setValue("file", m_fileEdit->text());
    s.setValue("loop", m_loopCheck->isChecked());
    s.setValue("tone", m_toneSpin->value());
    s.setValue("mic", m_micCombo->currentIndex());
    s.endGroup();
}

void RadioTxPanel::loadSettings(QSettings& s)
{
    s.beginGroup("radioTx");
    const int mode = s.value("mode", 0).toInt();
    const int src = s.value("source", 0).toInt();
    m_followBandCheck->setChecked(s.value("followBand", true).toBool());
    m_devSpin->setValue(s.value("deviation", 10.0).toDouble());
    m_depthSlider->setValue(s.value("depth", 80).toInt());
    m_preemphCombo->setCurrentIndex(s.value("preemph", 1).toInt());
    m_ampSlider->setValue(s.value("amplitude", 50).toInt());
    m_gainSlider->setValue(s.value("gain", 10).toInt());
    m_agcCheck->setChecked(s.value("agc", true).toBool());
    m_fileEdit->setText(s.value("file").toString());
    m_loopCheck->setChecked(s.value("loop", true).toBool());
    m_toneSpin->setValue(s.value("tone", 1000.0).toDouble());
    const int mic = s.value("mic", 0).toInt();
    if (mic >= 0 && mic < m_micCombo->count()) m_micCombo->setCurrentIndex(mic);
    s.endGroup();

    (mode == 1 ? m_nfmRadio : mode == 2 ? m_amRadio : m_wfmRadio)->setChecked(true);
    (src == 1 ? m_fileRadio : src == 2 ? m_toneRadio : m_micRadio)->setChecked(true);
    applyMode();
    if (!m_fileEdit->text().isEmpty()) decodeFile(m_fileEdit->text());
}
