#include "TvRxPanel.h"
#include "VideoView.h"
#include "TvRecorder.h"
#include <algorithm>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>
#include <QPushButton>
#include <QStandardPaths>
#include "core/BandPlan.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScreen>

// ============================================================================
// TvRxPipeline
// ============================================================================

TvRxPipeline::TvRxPipeline(QObject* parent)
    : QObject(parent)
    , m_decoder(new PALDecoder(this))
    , m_audioDemod(new AudioDemodulator(this))
    , m_audioOut(std::make_unique<AudioOutput>())
    , m_frameBuffer(std::make_unique<FrameBuffer>(16e6, 0.04))
{
    m_decoder->setSampleRate(16000000);
    m_audioDemod->setSampleRate(16000000);
}

TvRxPipeline::~TvRxPipeline()
{
    sinkStop();
}

void TvRxPipeline::sinkStart(uint32_t sampleRate)
{
    // Make sure no worker is inside the decoder while we reconfigure it
    m_videoWorker.clear();
    m_videoWorker.drain();
    m_audioWorker.drain();

    m_frameBuffer = std::make_unique<FrameBuffer>(static_cast<double>(sampleRate), 0.04);
    m_decoder->setSampleRate(static_cast<int>(sampleRate));
    m_audioDemod->setSampleRate(static_cast<double>(sampleRate));
    m_droppedFrames.store(0);
}

void TvRxPipeline::sinkProcess(const std::complex<float>* iq, size_t count)
{
    m_chunk.assign(iq, iq + count);
    m_frameBuffer->addBuffer(m_chunk);

    // One full PAL frame (40 ms) at a time, shared between the two stages
    while (m_frameBuffer->isFrameReady()) {
        auto frame = std::make_shared<std::vector<std::complex<float>>>(m_frameBuffer->getFrame());
        if (frame->empty()) break;

        // Video: short queue so a USB burst or one slow frame does not cost a frame;
        // if the decoder is really behind, the oldest waiting frame is dropped.
        const uint64_t before = m_videoWorker.dropped();
        m_videoWorker.post([this, frame] {
            QElapsedTimer t; t.start();
            m_decoder->processSamples(*frame);
            m_videoMs.store(0.9f * m_videoMs.load() + 0.1f * t.nsecsElapsed() / 1e6f);
        });
        if (m_videoWorker.dropped() != before)
            m_droppedFrames.fetch_add(1, std::memory_order_relaxed);

        // Audio: always queued (bounded, oldest dropped on overload)
        m_audioWorker.post([this, frame] {
            QElapsedTimer t; t.start();
            m_audioDemod->processSamples(*frame);
            m_audioMs.store(0.9f * m_audioMs.load() + 0.1f * t.nsecsElapsed() / 1e6f);
        });
    }
}

void TvRxPipeline::sinkStop()
{
    m_videoWorker.clear();
    m_audioWorker.clear();
    m_videoWorker.drain();
    m_audioWorker.drain();
}

// ============================================================================
// TvRxPanel
// ============================================================================

TvRxPanel::TvRxPanel(QWidget* parent)
    : ModePanel(parent)
    , m_pipe(new TvRxPipeline(this))
    , m_recorder(new TvRecorder(this))
{
    buildUi();

    connect(m_recorder, &TvRecorder::recordingChanged, this, [this](bool on) {
        m_recButton->setText(on ? "STOP RECORDING" : "RECORD MP4");
        m_recButton->setStyleSheet(on ? "QPushButton { background: #a03030; color: white; }" : QString());
        if (!on && !m_recorder->filePath().isEmpty())
            setRecText("Saved: " + QFileInfo(m_recorder->filePath()).fileName(),
                       QDir::toNativeSeparators(m_recorder->filePath()));
    });
    connect(m_recorder, &TvRecorder::errorOccurred, this, [this](const QString& msg) {
        setRecText("Recording error: " + msg, msg);
    });

    connect(m_pipe->decoder(), &PALDecoder::frameReady,
            this, &TvRxPanel::onFrameReady, Qt::QueuedConnection);
    connect(m_pipe->decoder(), &PALDecoder::syncStatsUpdated,
            this, &TvRxPanel::onSyncStatsUpdated, Qt::QueuedConnection);
    connect(m_pipe->audioDemod(), &AudioDemodulator::audioReady,
            this, &TvRxPanel::onAudioReady, Qt::QueuedConnection);
    connect(m_pipe->audioDemod(), &AudioDemodulator::audioCapabilityChanged, this,
            [this](bool capable, double rate, double carrier) {
                if (!capable) {
                    m_audioEnabledCheck->setChecked(false);
                    m_audioEnabledCheck->setEnabled(false);
                    m_audioEnabledCheck->setText(QString("Audio N/A (carrier %1 MHz > Nyquist %2 MHz)")
                                                     .arg(carrier / 1e6, 0, 'f', 1)
                                                     .arg(rate / 2e6, 0, 'f', 1));
                } else {
                    m_audioEnabledCheck->setEnabled(true);
                    m_audioEnabledCheck->setText("Enable Audio");
                    m_audioEnabledCheck->setChecked(true);
                }
            }, Qt::QueuedConnection);

    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &TvRxPanel::updateStatus);
    m_statusTimer->start(1000);
    m_fpsTimer.start();
}

TvRxPanel::~TvRxPanel()
{
    delete m_fullscreen;   // parentless top-level window
    m_fullscreen = nullptr;
}

void TvRxPanel::buildUi()
{
    //   [ PAL video            ] [ spectrum + waterfall (docked by StudioWindow) ]
    //   [ video processing     ] [ audio | sync detection | recording            ]
    QGridLayout* grid = new QGridLayout(this);
    grid->setSpacing(8);
    grid->setContentsMargins(8, 8, 8, 8);

    m_videoGroup = new QGroupBox("PAL Video (16:9)", this);
    QVBoxLayout* vl = new QVBoxLayout(m_videoGroup);
    vl->setContentsMargins(6, 6, 6, 6);
    m_video = new VideoView(m_videoGroup);
    m_video->setToolTip("Double-click: fullscreen");
    connect(m_video, &VideoView::doubleClicked, this, &TvRxPanel::toggleFullscreen);
    vl->addWidget(m_video);
    grid->addWidget(m_videoGroup, 0, 0);

    QGroupBox* specGroup = new QGroupBox("Spectrum", this);
    m_specSlot = new QVBoxLayout(specGroup);
    m_specSlot->setContentsMargins(6, 6, 6, 6);
    grid->addWidget(specGroup, 0, 1);

    grid->addWidget(buildVideoProcessingGroup(), 1, 0);

    QWidget* bottomRight = new QWidget(this);
    QHBoxLayout* br = new QHBoxLayout(bottomRight);
    br->setContentsMargins(0, 0, 0, 0);
    br->setSpacing(8);
    br->addWidget(buildAudioGroup(), 1);
    br->addWidget(buildSyncGroup(), 1);
    br->addWidget(buildRecordGroup(), 1);
    grid->addWidget(bottomRight, 1, 1);

    grid->setRowStretch(0, 1);          // the picture and the spectrum take the height
    grid->setRowStretch(1, 0);
    grid->setColumnStretch(0, 3);
    grid->setColumnStretch(1, 2);
}

QWidget* TvRxPanel::buildVideoProcessingGroup()
{
    QGroupBox* g = new QGroupBox("Video Processing", this);
    QVBoxLayout* l = new QVBoxLayout(g);

    auto sliderRow = [&](const QString& label, QSlider*& slider, QDoubleSpinBox*& spin,
                         int smin, int smax, int sval, double dmin, double dmax, double dstep,
                         double dval, int decimals, double scale) {
        QHBoxLayout* row = new QHBoxLayout();
        row->addWidget(new QLabel(label, g));
        slider = new QSlider(Qt::Horizontal, g);
        slider->setRange(smin, smax);
        slider->setValue(sval);
        spin = new QDoubleSpinBox(g);
        spin->setRange(dmin, dmax);
        spin->setSingleStep(dstep);
        spin->setValue(dval);
        spin->setDecimals(decimals);
        spin->setMaximumWidth(80);
        QSlider* s = slider; QDoubleSpinBox* sp = spin;
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), g,
                [s, scale](double v) { s->setValue(static_cast<int>(std::lround(v * scale))); });
        connect(slider, &QSlider::valueChanged, g, [sp, scale](int v) {
            sp->blockSignals(true);
            sp->setValue(v / scale);
            sp->blockSignals(false);
        });
        row->addWidget(slider, 1);
        row->addWidget(spin);
        l->addLayout(row);
    };

    sliderRow("Gain:", m_videoGainSlider, m_videoGainSpin, 1, 100, 15, 0.1, 10.0, 0.1, 1.5, 1, 10.0);
    connect(m_videoGainSlider, &QSlider::valueChanged, this,
            [this](int v) { m_pipe->decoder()->setVideoGain(v / 10.0f); });

    sliderRow("Offset:", m_videoOffsetSlider, m_videoOffsetSpin, -100, 100, 0, -1.0, 1.0, 0.01, 0.0, 2, 100.0);
    connect(m_videoOffsetSlider, &QSlider::valueChanged, this,
            [this](int v) { m_pipe->decoder()->setVideoOffset(v / 100.0f); });

    m_invertCheck = new QCheckBox("Invert Video (Negative)", g);
    m_invertCheck->setChecked(true);
    connect(m_invertCheck, &QCheckBox::toggled, this,
            [this](bool on) { m_pipe->decoder()->setVideoInvert(on); });
    l->addWidget(m_invertCheck);

    m_colorCheck = new QCheckBox("Color Mode", g);
    m_colorCheck->setChecked(false);
    connect(m_colorCheck, &QCheckBox::toggled, this,
            [this](bool on) { m_pipe->decoder()->setColorMode(on); });
    l->addWidget(m_colorCheck);

    sliderRow("Chroma:", m_chromaSlider, m_chromaSpin, 0, 500, 75, 0.0, 5.0, 0.01, 0.75, 2, 100.0);
    connect(m_chromaSlider, &QSlider::valueChanged, this,
            [this](int v) { m_pipe->decoder()->setChromaGain(v / 100.0f); });

    m_wideCheck = new QCheckBox("16:9 Display", g);
    m_wideCheck->setToolTip("Stretch the picture to 16:9 (anamorphic broadcasts)");
    m_wideCheck->setChecked(true);
    connect(m_wideCheck, &QCheckBox::toggled, this, [this](bool wide) {
        m_video->setWideAspect(wide);
        if (m_fullscreen) m_fullscreen->setWideAspect(wide);
        m_videoGroup->setTitle(wide ? "PAL Video (16:9)" : "PAL Video (4:3)");
    });
    l->addWidget(m_wideCheck);

    return g;
}

QWidget* TvRxPanel::buildAudioGroup()
{
    QGroupBox* g = new QGroupBox("Audio", this);
    QVBoxLayout* l = new QVBoxLayout(g);

    m_audioEnabledCheck = new QCheckBox("Enable Audio", g);
    m_audioEnabledCheck->setChecked(true);
    connect(m_audioEnabledCheck, &QCheckBox::toggled, this,
            [this](bool on) { m_pipe->audioDemod()->setAudioEnabled(on); });
    l->addWidget(m_audioEnabledCheck);

    QHBoxLayout* gainRow = new QHBoxLayout();
    gainRow->addWidget(new QLabel("Audio Gain:", g));
    m_audioGainSlider = new QSlider(Qt::Horizontal, g);
    m_audioGainSlider->setRange(0, 50);
    m_audioGainSlider->setValue(10);
    m_audioGainSpin = new QDoubleSpinBox(g);
    m_audioGainSpin->setRange(0.0, 5.0);
    m_audioGainSpin->setSingleStep(0.1);
    m_audioGainSpin->setValue(1.0);
    m_audioGainSpin->setDecimals(1);
    m_audioGainSpin->setMaximumWidth(80);
    connect(m_audioGainSlider, &QSlider::valueChanged, this, [this](int v) {
        m_audioGainSpin->blockSignals(true);
        m_audioGainSpin->setValue(v / 10.0);
        m_audioGainSpin->blockSignals(false);
        m_pipe->audioDemod()->setAudioGain(v / 10.0f);
    });
    connect(m_audioGainSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { m_audioGainSlider->setValue(static_cast<int>(std::lround(v * 10))); });
    gainRow->addWidget(m_audioGainSlider, 1);
    gainRow->addWidget(m_audioGainSpin);
    l->addLayout(gainRow);

    QHBoxLayout* volRow = new QHBoxLayout();
    volRow->addWidget(new QLabel("Volume:", g));
    m_volumeSlider = new QSlider(Qt::Horizontal, g);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(10);
    m_volumeLabel = new QLabel("10%", g);
    m_volumeLabel->setMinimumWidth(40);
    m_volumeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_pipe->audioOut()->setVolume(10);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int v) {
        m_pipe->audioOut()->setVolume(v);
        m_volumeLabel->setText(QString("%1%").arg(v));
    });
    volRow->addWidget(m_volumeSlider, 1);
    volRow->addWidget(m_volumeLabel);
    l->addLayout(volRow);

    return g;
}

QWidget* TvRxPanel::buildSyncGroup()
{
    QGroupBox* g = new QGroupBox("Sync Detection", this);
    QVBoxLayout* l = new QVBoxLayout(g);

    QHBoxLayout* rateRow = new QHBoxLayout();
    rateRow->addWidget(new QLabel("<b>Sync:</b>", g));
    m_syncRateLabel = new QLabel("---%", g);
    m_syncRateLabel->setMinimumWidth(80);
    m_syncRateLabel->setStyleSheet("QLabel { font-size: 14pt; font-weight: bold; color: #708090; "
                                   "padding: 4px; background-color: #0a0c12; border-radius: 3px; }");
    rateRow->addWidget(m_syncRateLabel);
    rateRow->addStretch();
    l->addLayout(rateRow);

    QHBoxLayout* thrRow = new QHBoxLayout();
    thrRow->addWidget(new QLabel("Threshold:", g));
    m_syncThresholdSlider = new QSlider(Qt::Horizontal, g);
    m_syncThresholdSlider->setRange(0, 50);
    m_syncThresholdSlider->setValue(0);
    m_syncThresholdSpin = new QDoubleSpinBox(g);
    m_syncThresholdSpin->setRange(0.0, 0.5);
    m_syncThresholdSpin->setSingleStep(0.01);
    m_syncThresholdSpin->setDecimals(2);
    m_syncThresholdSpin->setMaximumWidth(80);
    connect(m_syncThresholdSlider, &QSlider::valueChanged, this, [this](int v) {
        m_syncThresholdSpin->blockSignals(true);
        m_syncThresholdSpin->setValue(v / 100.0);
        m_syncThresholdSpin->blockSignals(false);
        m_pipe->decoder()->setSyncThreshold(v / 100.0f);
    });
    connect(m_syncThresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { m_syncThresholdSlider->setValue(static_cast<int>(std::lround(v * 100))); });
    thrRow->addWidget(m_syncThresholdSlider, 1);
    thrRow->addWidget(m_syncThresholdSpin);
    l->addLayout(thrRow);

    QGridLayout* cg = new QGridLayout();
    cg->setHorizontalSpacing(12);
    cg->setVerticalSpacing(2);

    m_autoSyncCheck = new QCheckBox("Auto threshold", g);
    m_autoSyncCheck->setToolTip("Slice halfway between sync tip and black level while locked");
    connect(m_autoSyncCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_syncThresholdSlider->setEnabled(!on);
        m_syncThresholdSpin->setEnabled(!on);
        m_pipe->decoder()->setAutoSyncThreshold(on);
        if (!on) m_pipe->decoder()->setSyncThreshold(m_syncThresholdSlider->value() / 100.0f);
    });
    cg->addWidget(m_autoSyncCheck, 0, 0);

    m_afcCheck = new QCheckBox("AFC (carrier tracking)", g);
    m_afcCheck->setChecked(true);
    connect(m_afcCheck, &QCheckBox::toggled, this, [this](bool on) { m_pipe->decoder()->setAfcEnabled(on); });
    cg->addWidget(m_afcCheck, 0, 1);

    m_syncDemodCheck = new QCheckBox("Synchronous demodulation", g);
    m_syncDemodCheck->setChecked(true);
    connect(m_syncDemodCheck, &QCheckBox::toggled, this, [this](bool on) { m_pipe->decoder()->setSyncDemod(on); });
    cg->addWidget(m_syncDemodCheck, 1, 0);

    m_vsbCheck = new QCheckBox("VSB compensation", g);
    connect(m_vsbCheck, &QCheckBox::toggled, this, [this](bool on) { m_pipe->decoder()->setVsbCompensation(on); });
    cg->addWidget(m_vsbCheck, 1, 1);

    m_denoiseCheck = new QCheckBox("Noise reduction (temporal)", g);
    m_denoiseCheck->setChecked(true);
    connect(m_denoiseCheck, &QCheckBox::toggled, this, [this](bool on) { m_pipe->decoder()->setTemporalDenoise(on); });
    cg->addWidget(m_denoiseCheck, 2, 0);
    l->addLayout(cg);
    l->addStretch();

    return g;
}

QWidget* TvRxPanel::buildRecordGroup()
{
    QGroupBox* g = new QGroupBox("Recording", this);
    QVBoxLayout* l = new QVBoxLayout(g);
    m_recButton = new QPushButton("RECORD MP4", g);
    m_recButton->setToolTip("Record the decoded picture and sound to an .mp4 in your Videos folder");
    m_recButton->setEnabled(false);
    connect(m_recButton, &QPushButton::clicked, this, &TvRxPanel::toggleRecording);
    l->addWidget(m_recButton);
    m_recFolderButton = new QPushButton("OPEN FOLDER", g);
    m_recFolderButton->setToolTip("Show the recordings in Explorer (selects the last file)");
    connect(m_recFolderButton, &QPushButton::clicked, this, &TvRxPanel::openRecordFolder);
    l->addWidget(m_recFolderButton);
    m_recLabel = new QLabel(g);
    m_recLabel->setStyleSheet("QLabel { color: #708090; font-size: 8pt; }");
    // Never let a long file name widen the column: the label wraps inside whatever width is left
    m_recLabel->setWordWrap(true);
    m_recLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_recLabel->setMinimumWidth(1);
    l->addWidget(m_recLabel);
    setRecText("Start the receiver to record");
    l->addStretch();
    return g;
}

QString TvRxPanel::recordDir() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (dir.isEmpty()) dir = QDir::homePath();
    return dir;
}

/* Short text in the label (file name only, wrapped), the full path in the tooltip. */
void TvRxPanel::setRecText(const QString& text, const QString& tip)
{
    m_recLabel->setToolTip(tip.isEmpty() ? text : tip);
    m_recLabel->setText(text);
}

void TvRxPanel::openRecordFolder()
{
    const QString last = m_recorder->filePath();
#ifdef Q_OS_WIN
    if (!last.isEmpty() && QFileInfo::exists(last)) {
        QProcess::startDetached("explorer.exe", { "/select,", QDir::toNativeSeparators(last) });
        return;
    }
#endif
    const QString dir = recordDir();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void TvRxPanel::toggleRecording()
{
    if (m_recorder->isRecording()) {
        m_recorder->stop();
        return;
    }
    const QString dir = recordDir();
    QDir().mkpath(dir);
    const QString name = QString("HackTvStudio_%1MHz_%2.mp4")
                             .arg(m_frequency / 1e6, 0, 'f', 3)
                             .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QSize size = m_wideCheck->isChecked() ? QSize(1024, 576) : QSize(720, 576);
    if (m_recorder->start(QDir(dir).filePath(name), size, 25, 48000))
        setRecText("Recording " + name + "  (" + m_recorder->codecInfo() + ")", QDir::toNativeSeparators(QDir(dir).filePath(name)));
}

// ---------------------------------------------------------------------------

void TvRxPanel::onActivated()
{
    m_video->setFocus();
}

void TvRxPanel::onFrequencyChanged(uint64_t hz)
{
    m_frequency = hz;
    m_pipe->decoder()->setTuneFrequency(hz);

    // Audio carrier in baseband = (video carrier + 5.5 MHz) - tune frequency.
    // Inside a known TV channel use that channel's video carrier (CCIR: start + 1.25 MHz),
    // otherwise assume we are tuned directly to the video carrier.
    const TvChannel ch = BandPlan::tvChannel(hz);
    const double videoCarrier = ch.valid ? static_cast<double>(ch.videoCarrierHz) : static_cast<double>(hz);
    const double audioBaseband = videoCarrier + 5.5e6 - static_cast<double>(hz);
    m_pipe->audioDemod()->setAudioCarrierFreq(audioBaseband);
}

void TvRxPanel::onSampleRateChanged(uint32_t hz)
{
    m_sampleRate = hz;
    // The pipeline is re-armed with the new rate by RxEngine (sinkStart) when running;
    // when idle apply it now so the status text is right.
    if (!m_running) {
        m_pipe->decoder()->setSampleRate(static_cast<int>(hz));
        m_pipe->audioDemod()->setSampleRate(static_cast<double>(hz));
    }
}

void TvRxPanel::onDeviceStarted()
{
    m_running = true;
    m_recButton->setEnabled(true);
    setRecText("Ready");
    m_frameCount = 0;
    m_fpsTimer.restart();
    onFrequencyChanged(m_frequency);
}

void TvRxPanel::onDeviceStopped()
{
    m_running = false;
    if (m_recorder->isRecording()) m_recorder->stop();
    m_recButton->setEnabled(false);
    m_syncRateLabel->setText("---%");
}

void TvRxPanel::onFrameReady(const QImage& frame)
{
    ++m_frameCount;
    const QImage copy = frame.copy();
    if (m_recorder->isRecording()) m_recorder->pushFrame(copy);
    if (m_fullscreen && m_fullscreen->isVisible())
        m_fullscreen->setFrame(copy);
    else
        m_video->setFrame(copy);
}

void TvRxPanel::onSyncStatsUpdated(float syncRate, float, float)
{
    QString color;
    if (syncRate >= 95.0f)      color = "#00ffcc";
    else if (syncRate >= 85.0f) color = "#ffd700";
    else if (syncRate >= 70.0f) color = "#ff8800";
    else                        color = "#ff4040";

    m_syncRateLabel->setText(QString("%1%").arg(syncRate, 0, 'f', 1));
    m_syncRateLabel->setStyleSheet(QString("QLabel { font-size: 14pt; font-weight: bold; color: %1; "
                                           "padding: 4px; background-color: #0a0c12; border-radius: 3px; }")
                                       .arg(color));
}

void TvRxPanel::onAudioReady(const std::vector<float>& samples)
{
    m_pipe->audioOut()->enqueueAudio(samples);
    if (m_recorder->isRecording()) m_recorder->pushAudio(samples);
}

void TvRxPanel::updateStatus()
{
    const qint64 elapsed = m_fpsTimer.elapsed();
    const float fps = elapsed > 0 ? (m_frameCount * 1000.0f) / elapsed : 0.0f;
    m_frameCount = 0;
    m_fpsTimer.restart();

    if (!m_running) {
        emit statusText("TV RX idle");
        return;
    }

    QString s = QString("TV RX | %1 fps | decode %4 ms/frame, audio %5 ms | V.Gain %2 | V.Offset %3")
                    .arg(fps, 0, 'f', 1)
                    .arg(m_pipe->decoder()->getVideoGain(), 0, 'f', 1)
                    .arg(m_pipe->decoder()->getVideoOffset(), 0, 'f', 2)
                    .arg(m_pipe->videoMsPerFrame(), 0, 'f', 1)
                    .arg(m_pipe->audioMsPerFrame(), 0, 'f', 1);
    if (m_afcCheck->isChecked())
        s += QString(" | AFC %1 kHz").arg(m_pipe->decoder()->getAfcTrimHz() / 1000.0f, 0, 'f', 1);
    const uint64_t dropped = m_pipe->droppedFrames();
    if (dropped)
        s += QString(" | dropped %1").arg(dropped);
    if (m_recorder->isRecording()) {
        const qint64 ms = m_recorder->durationMs();
        s += QString(" | REC %1:%2").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QChar('0'));
        setRecText(QString("Recording  %1:%2  (%3)")
                       .arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QChar('0'))
                       .arg(m_recorder->codecInfo()),
                   QDir::toNativeSeparators(m_recorder->filePath()));
    }
    emit statusText(s);
}

void TvRxPanel::toggleFullscreen()
{
    if (!m_fullscreen) {
        m_fullscreen = new VideoView(nullptr);
        m_fullscreen->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        m_fullscreen->setWideAspect(m_wideCheck->isChecked());
        connect(m_fullscreen, &VideoView::doubleClicked, this, &TvRxPanel::toggleFullscreen);
        connect(m_fullscreen, &VideoView::escapePressed, this, &TvRxPanel::toggleFullscreen);
    }
    if (m_fullscreen->isVisible()) {
        m_fullscreen->hide();
        m_video->setFrame(m_fullscreen->frame());
    } else {
        m_fullscreen->setScreen(screen());
        m_fullscreen->setFrame(m_video->frame());
        m_fullscreen->showFullScreen();
        m_fullscreen->setFocus();
    }
}

// ---------------------------------------------------------------------------
// Settings (same keys as the original PALBDecoder so the migrated values apply)

void TvRxPanel::saveSettings(QSettings& s)
{
    s.setValue("videoGain",     m_videoGainSlider->value());
    s.setValue("videoOffset",   m_videoOffsetSlider->value());
    s.setValue("invertVideo",   m_invertCheck->isChecked());
    s.setValue("colorMode",     m_colorCheck->isChecked());
    s.setValue("chromaGain",    m_chromaSlider->value());
    s.setValue("syncThreshold", m_syncThresholdSlider->value());
    s.setValue("autoSync",      m_autoSyncCheck->isChecked());
    s.setValue("denoise",       m_denoiseCheck->isChecked());
    s.setValue("afc",           m_afcCheck->isChecked());
    s.setValue("wideDisplay",   m_wideCheck->isChecked());
    s.setValue("syncDemod",     m_syncDemodCheck->isChecked());
    s.setValue("vsbComp",       m_vsbCheck->isChecked());
    s.setValue("audioEnabled",  m_audioEnabledCheck->isChecked());
    s.setValue("audioGain",     m_audioGainSlider->value());
    s.setValue("volume",        m_volumeSlider->value());
}

void TvRxPanel::loadSettings(QSettings& s)
{
    m_videoGainSlider->setValue(s.value("videoGain", 15).toInt());
    m_videoOffsetSlider->setValue(s.value("videoOffset", 0).toInt());
    m_invertCheck->setChecked(s.value("invertVideo", true).toBool());
    m_colorCheck->setChecked(s.value("colorMode", false).toBool());
    m_chromaSlider->setValue(s.value("chromaGain", 75).toInt());
    m_syncThresholdSlider->setValue(s.value("syncThreshold", 0).toInt());
    m_autoSyncCheck->setChecked(s.value("autoSync", false).toBool());
    m_denoiseCheck->setChecked(s.value("denoise", true).toBool());
    m_afcCheck->setChecked(s.value("afc", true).toBool());
    m_wideCheck->setChecked(s.value("wideDisplay", true).toBool());
    m_syncDemodCheck->setChecked(s.value("syncDemod", true).toBool());
    m_vsbCheck->setChecked(s.value("vsbComp", false).toBool());
    m_audioEnabledCheck->setChecked(s.value("audioEnabled", true).toBool());
    m_audioGainSlider->setValue(s.value("audioGain", 10).toInt());
    m_volumeSlider->setValue(s.value("volume", 10).toInt());

    // Sliders that did not change value never emitted valueChanged: push explicitly
    m_pipe->decoder()->setVideoGain(m_videoGainSlider->value() / 10.0f);
    m_pipe->decoder()->setVideoOffset(m_videoOffsetSlider->value() / 100.0f);
    m_pipe->decoder()->setVideoInvert(m_invertCheck->isChecked());
    m_pipe->decoder()->setColorMode(m_colorCheck->isChecked());
    m_pipe->decoder()->setChromaGain(m_chromaSlider->value() / 100.0f);
    m_pipe->decoder()->setSyncThreshold(m_syncThresholdSlider->value() / 100.0f);
    m_pipe->decoder()->setAutoSyncThreshold(m_autoSyncCheck->isChecked());
    m_pipe->decoder()->setTemporalDenoise(m_denoiseCheck->isChecked());
    m_pipe->decoder()->setAfcEnabled(m_afcCheck->isChecked());
    m_pipe->decoder()->setSyncDemod(m_syncDemodCheck->isChecked());
    m_pipe->decoder()->setVsbCompensation(m_vsbCheck->isChecked());
    m_pipe->audioDemod()->setAudioEnabled(m_audioEnabledCheck->isChecked());
    m_pipe->audioDemod()->setAudioGain(m_audioGainSlider->value() / 10.0f);
    m_pipe->audioOut()->setVolume(m_volumeSlider->value());
    m_video->setWideAspect(m_wideCheck->isChecked());
}
