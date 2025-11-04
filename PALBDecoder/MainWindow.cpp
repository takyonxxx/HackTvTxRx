#include "MainWindow.h"
#include "hacktvlib.h"  // Your HackRF library
#include <QDebug>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>
#include <QCheckBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_frameCount(0)
    , m_shuttingDown(false)
    , m_hackRfRunning(false)
    , m_currentFrequency(DEFAULT_FREQ)
{
    // Create circular buffer - 64MB buffer (holds ~244 HackRF callbacks)
    constexpr size_t BUFFER_SIZE = 64 * 1024 * 1024; // 64 MB
    m_circularBuffer = std::make_unique<CircularBuffer>(BUFFER_SIZE);

    // Create PAL decoder
    m_palDecoder = std::make_unique<PALDecoder>(this);

    // Connect frame ready signal
    connect(m_palDecoder.get(), &PALDecoder::frameReady,
            this, &MainWindow::onFrameReady, Qt::QueuedConnection);

    connect(m_palDecoder.get(), &PALDecoder::syncStatsUpdated,
            this, &MainWindow::onSyncStatsUpdated, Qt::QueuedConnection);

    // Create processor thread
    m_processorThread = std::make_unique<PALProcessorThread>(
        m_circularBuffer.get(),
        m_palDecoder.get(),
        this
        );

    // Connect buffer stats
    connect(m_processorThread.get(), &PALProcessorThread::bufferStats,
            this, &MainWindow::onBufferStats, Qt::QueuedConnection);

    // Connect frame ready signal
    connect(m_palDecoder.get(), &PALDecoder::frameReady,
            this, &MainWindow::onFrameReady, Qt::QueuedConnection);

    // Setup UI
    setupUI();
    setupControls();

    // Setup status timer
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    m_statusTimer->start(1000); // Update every second

    // Start FPS timer
    m_fpsTimer.start();

    setWindowTitle("PAL-B/G Decoder with HackRF - Qt 6.9.3");

    // Initialize HackRF automatically
    initHackRF();
}

MainWindow::~MainWindow()
{
    qDebug() << "MainWindow destructor started";
    m_shuttingDown = true;

    // Stop HackRF FIRST
    if (m_hackTvLib && m_hackRfRunning) {
        qDebug() << "Stopping HackRF...";
        m_hackTvLib->stop();
        QThread::msleep(100); // Wait for callbacks to finish
    }

    // Stop processor thread
    if (m_processorThread && m_processorThread->isRunning()) {
        qDebug() << "Stopping processor thread...";
        m_processorThread->stopProcessing();
        m_processorThread->quit();
        if (!m_processorThread->wait(2000)) {
            qWarning() << "Forcing thread termination";
            m_processorThread->terminate();
            m_processorThread->wait(1000);
        }
        qDebug() << "Processor thread stopped";
    }

    if (m_statusTimer) {
        m_statusTimer->stop();
    }

    qDebug() << "MainWindow destructor finished";
}

void MainWindow::setupUI()
{
    // Central widget
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // Video display
    QGroupBox* videoGroup = new QGroupBox("PAL Video Display (384x576)", this);
    QVBoxLayout* videoLayout = new QVBoxLayout(videoGroup);

    m_videoLabel = new QLabel(this);
    m_videoLabel->setMinimumSize(384 / 2, 576 / 2);
    m_videoLabel->setScaledContents(true);
    m_videoLabel->setStyleSheet("QLabel { background-color: black; border: 1px solid gray; }");
    m_videoLabel->setAlignment(Qt::AlignCenter);

    // Set initial placeholder
    QImage placeholder(384, 576, QImage::Format_Grayscale8);
    placeholder.fill(Qt::black);
    m_videoLabel->setPixmap(QPixmap::fromImage(placeholder));

    videoGroup->setMaximumWidth(576);
    m_videoLabel->setMaximumWidth(576);

    videoLayout->addWidget(m_videoLabel);
    mainLayout->addWidget(videoGroup);

    // Status display
    QHBoxLayout* statusLayout = new QHBoxLayout();
    m_statusLabel = new QLabel("Status: Initializing...", this);
    m_fpsLabel = new QLabel("FPS: 0.0", this);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    statusLayout->addWidget(m_fpsLabel);
    mainLayout->addLayout(statusLayout);
}

void MainWindow::setupControls()
{
    QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(centralWidget()->layout());
    if (!mainLayout) return;

    // ========== FREQUENCY CONTROL ==========
    QGroupBox* freqGroup = new QGroupBox("Frequency Control - UHF TV Band", this);
    QVBoxLayout* freqLayout = new QVBoxLayout(freqGroup);

    // Frequency spinbox
    QHBoxLayout* spinLayout = new QHBoxLayout();
    spinLayout->addWidget(new QLabel("<b>Frequency:</b>", this));
    m_frequencySpinBox = new QDoubleSpinBox(this);
    m_frequencySpinBox->setRange(470.0, 862.0);
    m_frequencySpinBox->setValue(478.0);
    m_frequencySpinBox->setSingleStep(0.1);  // 100 kHz steps
    m_frequencySpinBox->setDecimals(3);
    m_frequencySpinBox->setSuffix(" MHz");
    m_frequencySpinBox->setMinimumWidth(150);
    m_frequencySpinBox->setStyleSheet("QDoubleSpinBox { font-size: 14pt; font-weight: bold; }");
    connect(m_frequencySpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MainWindow::onFrequencySpinBoxChanged);
    spinLayout->addWidget(m_frequencySpinBox);

    // Channel label
    m_channelLabel = new QLabel(this);
    m_channelLabel->setStyleSheet(
        "QLabel {"
        "    font-size: 14pt;"
        "    font-weight: bold;"
        "    color: #3388ff;"
        "    padding: 8px;"
        "    background-color: #f0f0f0;"
        "    border-radius: 5px;"
        "    border: 2px solid #3388ff;"
        "}"
        );
    m_channelLabel->setAlignment(Qt::AlignCenter);
    m_channelLabel->setMinimumWidth(200);
    updateChannelLabel(m_currentFrequency);
    spinLayout->addWidget(m_channelLabel);
    spinLayout->addStretch();
    freqLayout->addLayout(spinLayout);

    // Horizontal slider with labels
    QHBoxLayout* sliderLayout = new QHBoxLayout();

    QLabel* minLabel = new QLabel("470 MHz", this);
    minLabel->setStyleSheet("font-weight: bold; color: #55ff55;");
    sliderLayout->addWidget(minLabel);

    m_frequencySlider = new QSlider(Qt::Horizontal, this);
    m_frequencySlider->setRange(470, 862);  // 470-862 MHz
    m_frequencySlider->setValue(478);       // Default: 478 MHz
    m_frequencySlider->setTickPosition(QSlider::TicksBelow);
    m_frequencySlider->setTickInterval(8);  // 8 MHz steps
    m_frequencySlider->setMinimumWidth(400);
    m_frequencySlider->setStyleSheet(
        "QSlider::groove:horizontal {"
        "    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #66cc66, stop:1 #ff6666);"
        "    height: 10px;"
        "    border-radius: 5px;"
        "}"
        "QSlider::handle:horizontal {"
        "    background: #3388ff;"
        "    border: 2px solid #ffffff;"
        "    width: 20px;"
        "    margin: -5px 0;"
        "    border-radius: 10px;"
        "}"
        "QSlider::handle:horizontal:hover {"
        "    background: #5599ff;"
        "}"
        );
    connect(m_frequencySlider, &QSlider::valueChanged,
            this, &MainWindow::onFrequencySliderChanged);
    sliderLayout->addWidget(m_frequencySlider, 1);

    QLabel* maxLabel = new QLabel("862 MHz", this);
    maxLabel->setStyleSheet("font-weight: bold; color: #ff5555;");
    sliderLayout->addWidget(maxLabel);

    freqLayout->addLayout(sliderLayout);

    mainLayout->addWidget(freqGroup);

    // ========== HackRF Controls ==========
    QGroupBox* hackRfGroup = new QGroupBox("HackRF Controls", this);
    QVBoxLayout* hackRfLayout = new QVBoxLayout(hackRfGroup);

    // Start/Stop button
    m_startStopButton = new QPushButton("Start HackRF", this);
    m_startStopButton->setStyleSheet("QPushButton { background-color: #55ff55; color: black; padding: 10px; font-weight: bold; }");
    connect(m_startStopButton, &QPushButton::clicked, this, &MainWindow::toggleHackRF);
    hackRfLayout->addWidget(m_startStopButton);

    // LNA Gain
    QHBoxLayout* lnaLayout = new QHBoxLayout();
    lnaLayout->addWidget(new QLabel("LNA Gain (0-40):", this));
    m_lnaGainSlider = new QSlider(Qt::Horizontal, this);
    m_lnaGainSlider->setRange(0, 40);
    m_lnaGainSlider->setValue(40);
    m_lnaGainSlider->setTickPosition(QSlider::TicksBelow);
    m_lnaGainSlider->setTickInterval(8);
    m_lnaGainSpinBox = new QSpinBox(this);
    m_lnaGainSpinBox->setRange(0, 40);
    m_lnaGainSpinBox->setValue(40);
    connect(m_lnaGainSlider, &QSlider::valueChanged, this, &MainWindow::onLnaGainChanged);
    connect(m_lnaGainSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int value) { m_lnaGainSlider->setValue(value); });
    lnaLayout->addWidget(m_lnaGainSlider, 1);
    lnaLayout->addWidget(m_lnaGainSpinBox);
    hackRfLayout->addLayout(lnaLayout);

    // VGA Gain
    QHBoxLayout* vgaLayout = new QHBoxLayout();
    vgaLayout->addWidget(new QLabel("VGA Gain (0-62):", this));
    m_vgaGainSlider = new QSlider(Qt::Horizontal, this);
    m_vgaGainSlider->setRange(0, 62);
    m_vgaGainSlider->setValue(20);
    m_vgaGainSlider->setTickPosition(QSlider::TicksBelow);
    m_vgaGainSlider->setTickInterval(8);
    m_vgaGainSpinBox = new QSpinBox(this);
    m_vgaGainSpinBox->setRange(0, 62);
    m_vgaGainSpinBox->setValue(20);
    connect(m_vgaGainSlider, &QSlider::valueChanged, this, &MainWindow::onVgaGainChanged);
    connect(m_vgaGainSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int value) { m_vgaGainSlider->setValue(value); });
    vgaLayout->addWidget(m_vgaGainSlider, 1);
    vgaLayout->addWidget(m_vgaGainSpinBox);
    hackRfLayout->addLayout(vgaLayout);

    // RX Amp Gain
    QHBoxLayout* rxAmpLayout = new QHBoxLayout();
    rxAmpLayout->addWidget(new QLabel("RX Amp (0-14):", this));
    m_rxAmpGainSlider = new QSlider(Qt::Horizontal, this);
    m_rxAmpGainSlider->setRange(0, 14);
    m_rxAmpGainSlider->setValue(14);
    m_rxAmpGainSlider->setTickPosition(QSlider::TicksBelow);
    m_rxAmpGainSlider->setTickInterval(2);
    m_rxAmpGainSpinBox = new QSpinBox(this);
    m_rxAmpGainSpinBox->setRange(0, 14);
    m_rxAmpGainSpinBox->setValue(14);
    connect(m_rxAmpGainSlider, &QSlider::valueChanged, this, &MainWindow::onRxAmpGainChanged);
    connect(m_rxAmpGainSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int value) { m_rxAmpGainSlider->setValue(value); });
    rxAmpLayout->addWidget(m_rxAmpGainSlider, 1);
    rxAmpLayout->addWidget(m_rxAmpGainSpinBox);
    hackRfLayout->addLayout(rxAmpLayout);

    mainLayout->addWidget(hackRfGroup);

    // ========== Video Processing Controls ==========
    QGroupBox* videoControlGroup = new QGroupBox("Video Processing Controls", this);
    QVBoxLayout* videoControlLayout = new QVBoxLayout(videoControlGroup);

    // Video Gain control
    QHBoxLayout* videoGainLayout = new QHBoxLayout();
    videoGainLayout->addWidget(new QLabel("Video Gain:", this));
    m_videoGainSlider = new QSlider(Qt::Horizontal, this);
    m_videoGainSlider->setRange(1, 100); // 0.1 to 10.0
    m_videoGainSlider->setValue(15); // Default 1.5
    m_videoGainSlider->setTickPosition(QSlider::TicksBelow);
    m_videoGainSlider->setTickInterval(10);
    m_videoGainSpinBox = new QDoubleSpinBox(this);
    m_videoGainSpinBox->setRange(0.1, 10.0);
    m_videoGainSpinBox->setSingleStep(0.1);
    m_videoGainSpinBox->setValue(1.5);
    m_videoGainSpinBox->setDecimals(1);
    connect(m_videoGainSlider, &QSlider::valueChanged, this, &MainWindow::onVideoGainChanged);
    connect(m_videoGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double value) { m_videoGainSlider->setValue(static_cast<int>(value * 10)); });
    videoGainLayout->addWidget(m_videoGainSlider, 1);
    videoGainLayout->addWidget(m_videoGainSpinBox);
    videoControlLayout->addLayout(videoGainLayout);

    // Video Offset control
    QHBoxLayout* videoOffsetLayout = new QHBoxLayout();
    videoOffsetLayout->addWidget(new QLabel("Video Offset:", this));
    m_videoOffsetSlider = new QSlider(Qt::Horizontal, this);
    m_videoOffsetSlider->setRange(-100, 100); // -1.0 to 1.0
    m_videoOffsetSlider->setValue(0); // Default 0.0
    m_videoOffsetSlider->setTickPosition(QSlider::TicksBelow);
    m_videoOffsetSlider->setTickInterval(20);
    m_videoOffsetSpinBox = new QDoubleSpinBox(this);
    m_videoOffsetSpinBox->setRange(-1.0, 1.0);
    m_videoOffsetSpinBox->setSingleStep(0.01);
    m_videoOffsetSpinBox->setValue(0.0);
    m_videoOffsetSpinBox->setDecimals(2);
    connect(m_videoOffsetSlider, &QSlider::valueChanged, this, &MainWindow::onVideoOffsetChanged);
    connect(m_videoOffsetSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double value) { m_videoOffsetSlider->setValue(static_cast<int>(value * 100)); });
    videoOffsetLayout->addWidget(m_videoOffsetSlider, 1);
    videoOffsetLayout->addWidget(m_videoOffsetSpinBox);
    videoControlLayout->addLayout(videoOffsetLayout);

    // Invert Video checkbox
    QHBoxLayout* invertLayout = new QHBoxLayout();
    m_invertVideoCheckBox = new QCheckBox("Invert Video (Negative)", this);
    m_invertVideoCheckBox->setStyleSheet("QCheckBox { font-weight: bold; }");
    connect(m_invertVideoCheckBox, &QCheckBox::stateChanged,
            this, &MainWindow::onInvertVideoChanged);
    invertLayout->addWidget(m_invertVideoCheckBox);
    invertLayout->addStretch();
    videoControlLayout->addLayout(invertLayout);
    mainLayout->addWidget(videoControlGroup);

    QGroupBox* syncControlGroup = new QGroupBox("Sync Detection Controls", this);
    QVBoxLayout* syncControlLayout = new QVBoxLayout(syncControlGroup);

    // Sync Rate Display (read-only)
    QHBoxLayout* syncRateLayout = new QHBoxLayout();
    syncRateLayout->addWidget(new QLabel("<b>Sync Rate:</b>", this));
    m_syncRateLabel = new QLabel("---%", this);
    m_syncRateLabel->setStyleSheet(
        "QLabel {"
        "    font-size: 16pt;"
        "    font-weight: bold;"
        "    color: #00aa00;"
        "    padding: 5px;"
        "    background-color: #f0f0f0;"
        "    border-radius: 3px;"
        "}"
        );
    m_syncRateLabel->setMinimumWidth(100);
    syncRateLayout->addWidget(m_syncRateLabel);
    syncRateLayout->addStretch();
    syncControlLayout->addLayout(syncRateLayout);

    // Sync Threshold control
    QHBoxLayout* syncThresholdLayout = new QHBoxLayout();
    syncThresholdLayout->addWidget(new QLabel("Sync Threshold:", this));

    m_syncThresholdSlider = new QSlider(Qt::Horizontal, this);
    m_syncThresholdSlider->setRange(-100, 0); // -1.0 to 0.0
    m_syncThresholdSlider->setValue(-20); // Default -0.2
    m_syncThresholdSlider->setTickPosition(QSlider::TicksBelow);
    m_syncThresholdSlider->setTickInterval(10);

    m_syncThresholdSpinBox = new QDoubleSpinBox(this);
    m_syncThresholdSpinBox->setRange(-1.0, 0.0);
    m_syncThresholdSpinBox->setSingleStep(0.01);
    m_syncThresholdSpinBox->setValue(-0.2);
    m_syncThresholdSpinBox->setDecimals(2);

    connect(m_syncThresholdSlider, &QSlider::valueChanged,
            this, &MainWindow::onSyncThresholdChanged);
    connect(m_syncThresholdSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double value) {
                m_syncThresholdSlider->setValue(static_cast<int>(value * 100));
            });

    syncThresholdLayout->addWidget(m_syncThresholdSlider, 1);
    syncThresholdLayout->addWidget(m_syncThresholdSpinBox);
    syncControlLayout->addLayout(syncThresholdLayout);

    // Help text
    QLabel* syncHelpLabel = new QLabel(
        "<i>Lower values (-0.5) = stricter sync detection<br>"
        "Higher values (-0.1) = more lenient sync detection<br>"
        "Recommended: -0.2 to -0.3 for stable image</i>",
        this);
    syncHelpLabel->setWordWrap(true);
    syncHelpLabel->setStyleSheet("QLabel { color: #666; font-size: 9pt; }");
    syncControlLayout->addWidget(syncHelpLabel);

    mainLayout->addWidget(syncControlGroup);

    // Info label
    QLabel* infoLabel = new QLabel(
        "<b>PAL-B/G Decoder with HackRF</b><br>"
        "• Sample Rate: 16 MHz | Video: 6 MHz<br>"
        "• Resolution: 288×576 pixels (Grayscale)<br>"
        "• 625 lines, 25 fps, AM demodulation<br><br>"
        "<i>Adjust gains for signal strength, sync threshold for stability.</i>",
        this);
    infoLabel->setWordWrap(true);
    mainLayout->addWidget(infoLabel);
}

void MainWindow::onSyncStatsUpdated(float syncRate, float peakLevel, float minLevel)
{
    // Update sync rate label with color coding
    QString text = QString("%1%").arg(syncRate, 0, 'f', 1);
    m_syncRateLabel->setText(text);

    // Color code based on sync rate
    QString color;
    if (syncRate >= 95.0f) {
        color = "#00aa00"; // Green - excellent
    } else if (syncRate >= 85.0f) {
        color = "#aaaa00"; // Yellow - good
    } else if (syncRate >= 70.0f) {
        color = "#ff8800"; // Orange - acceptable
    } else {
        color = "#ff0000"; // Red - poor
    }

    m_syncRateLabel->setStyleSheet(QString(
                                       "QLabel {"
                                       "    font-size: 16pt;"
                                       "    font-weight: bold;"
                                       "    color: %1;"
                                       "    padding: 5px;"
                                       "    background-color: #f0f0f0;"
                                       "    border-radius: 3px;"
                                       "}"
                                       ).arg(color));
}

void MainWindow::onSyncThresholdChanged(int value)
{
    float threshold = value / 100.0f; // -100 to 0 -> -1.0 to 0.0

    m_syncThresholdSpinBox->blockSignals(true);
    m_syncThresholdSpinBox->setValue(threshold);
    m_syncThresholdSpinBox->blockSignals(false);

    if (m_palDecoder) {
        m_palDecoder->setSyncThreshold(threshold);
        qDebug() << "Sync threshold set to:" << threshold;
    }
}

void MainWindow::onFrequencySliderChanged(int value)
{
    // Slider MHz cinsinden (470-862)
    double freqMHz = static_cast<double>(value);

    // Spinbox'ı güncelle (sonsuz döngü önleme)
    m_frequencySpinBox->blockSignals(true);
    m_frequencySpinBox->setValue(freqMHz);
    m_frequencySpinBox->blockSignals(false);

    // Frequency'yi Hz cinsine çevir
    m_currentFrequency = static_cast<uint64_t>(freqMHz * 1000000.0);

    // Channel label'ı güncelle
    updateChannelLabel(m_currentFrequency);

    // HackRF çalışıyorsa frekansı güncelle
    if (m_hackRfRunning && m_hackTvLib) {
        applyFrequencyChange();
    }
}

void MainWindow::onFrequencySpinBoxChanged(double value)
{
    // SpinBox MHz cinsinden
    int sliderValue = static_cast<int>(value);

    // Slider'ı güncelle (sonsuz döngü önleme)
    m_frequencySlider->blockSignals(true);
    m_frequencySlider->setValue(sliderValue);
    m_frequencySlider->blockSignals(false);

    // Frequency'yi Hz cinsine çevir
    m_currentFrequency = static_cast<uint64_t>(value * 1000000.0);

    // Channel label'ı güncelle
    updateChannelLabel(m_currentFrequency);

    // HackRF çalışıyorsa frekansı güncelle
    if (m_hackRfRunning && m_hackTvLib) {
        applyFrequencyChange();
    }
}

void MainWindow::onInvertVideoChanged(int state)
{
    bool invert = (state == Qt::Checked);

    if (m_palDecoder) {
        m_palDecoder->setVideoInvert(invert);
    }

    qDebug() << "Video invert:" << (invert ? "ON" : "OFF");
}

void MainWindow::updateChannelLabel(uint64_t frequency)
{
    // UHF TV kanalı hesapla
    // UHF kanal formülü: Kanal = (Frekans - 306 MHz) / 8 MHz
    // Örnek: 474 MHz = Kanal 21

    int64_t freqMHz = frequency / 1000000;
    int channel = -1;

    if (freqMHz >= 470 && freqMHz <= 862) {
        channel = (freqMHz - 306) / 8;
    }

    QString text;
    if (channel >= 21 && channel <= 69) {
        text = QString("<b>UHF Channel %1</b><br>%2 MHz")
                   .arg(channel)
                   .arg(freqMHz);
    } else {
        text = QString("<b>Custom Frequency</b><br>%1 MHz")
                   .arg(freqMHz);
    }

    m_channelLabel->setText(text);
}

void MainWindow::applyFrequencyChange()
{
    if (!m_hackTvLib) return;
    m_hackTvLib->setFrequency(m_currentFrequency);
}

void MainWindow::onVideoGainChanged(int value)
{
    float gain = value / 10.0f;
    m_videoGainSpinBox->blockSignals(true);
    m_videoGainSpinBox->setValue(gain);
    m_videoGainSpinBox->blockSignals(false);

    if (m_palDecoder) {
        m_palDecoder->setVideoGain(gain);
    }
}

void MainWindow::onVideoOffsetChanged(int value)
{
    float offset = value / 100.0f;
    m_videoOffsetSpinBox->blockSignals(true);
    m_videoOffsetSpinBox->setValue(offset);
    m_videoOffsetSpinBox->blockSignals(false);

    if (m_palDecoder) {
        m_palDecoder->setVideoOffset(offset);
    }
}

void MainWindow::onLnaGainChanged(int value)
{
    m_lnaGainSpinBox->blockSignals(true);
    m_lnaGainSpinBox->setValue(value);
    m_lnaGainSpinBox->blockSignals(false);

    if (m_hackTvLib && m_hackRfRunning) {
        m_hackTvLib->setLnaGain(static_cast<unsigned int>(value));
        qDebug() << "LNA Gain set to:" << value;
    }
}

void MainWindow::onVgaGainChanged(int value)
{
    m_vgaGainSpinBox->blockSignals(true);
    m_vgaGainSpinBox->setValue(value);
    m_vgaGainSpinBox->blockSignals(false);

    if (m_hackTvLib && m_hackRfRunning) {
        m_hackTvLib->setVgaGain(static_cast<unsigned int>(value));
        qDebug() << "VGA Gain set to:" << value;
    }
}

void MainWindow::onRxAmpGainChanged(int value)
{
    m_rxAmpGainSpinBox->blockSignals(true);
    m_rxAmpGainSpinBox->setValue(value);
    m_rxAmpGainSpinBox->blockSignals(false);

    if (m_hackTvLib && m_hackRfRunning) {
        m_hackTvLib->setRxAmpGain(static_cast<unsigned int>(value));
        qDebug() << "RX Amp Gain set to:" << value;
    }
}

void MainWindow::toggleHackRF()
{
    if (m_hackRfRunning) {
        // Stop HackRF
        qDebug() << "Stopping HackRF...";

        if (m_hackTvLib) {
            m_hackTvLib->stop();
        }

        m_hackRfRunning = false;

        // Stop processor thread
        if (m_processorThread && m_processorThread->isRunning()) {
            qDebug() << "Stopping processor thread...";
            m_processorThread->stopProcessing();
            m_processorThread->quit();
            if (!m_processorThread->wait(2000)) {
                qWarning() << "Processor thread did not stop gracefully";
                m_processorThread->terminate();
                m_processorThread->wait(1000);
            }
            qDebug() << "Processor thread stopped";
        }

        // Clear buffer
        if (m_circularBuffer) {
            m_circularBuffer->clear();
            qDebug() << "Buffer cleared";
        }

        m_startStopButton->setText("Start HackRF");
        m_startStopButton->setStyleSheet("QPushButton { background-color: #55ff55; color: black; padding: 10px; font-weight: bold; }");
        qDebug() << "HackRF stopped";

    } else {
        // Start HackRF
        qDebug() << "Starting HackRF...";

        if (!m_hackTvLib) {
            QMessageBox::critical(this, "Error", "HackTvLib not initialized!");
            return;
        }

        // Clear buffer before starting
        if (m_circularBuffer) {
            m_circularBuffer->clear();
            qDebug() << "Buffer cleared";
        }

        // Make sure processor thread exists
        if (!m_processorThread) {
            qWarning() << "Creating processor thread...";
            m_processorThread = std::make_unique<PALProcessorThread>(
                m_circularBuffer.get(),
                m_palDecoder.get(),
                this
                );
            connect(m_processorThread.get(), &PALProcessorThread::bufferStats,
                    this, &MainWindow::onBufferStats, Qt::QueuedConnection);
        }

        // Start processor thread FIRST
        if (!m_processorThread->isRunning()) {
            qDebug() << "Starting processor thread...";
            m_processorThread->start(QThread::HighPriority);

            // Give thread time to start
            QThread::msleep(100);

            if (m_processorThread->isRunning()) {
                qDebug() << "Processor thread started successfully";
            } else {
                QMessageBox::critical(this, "Error", "Failed to start processor thread!");
                return;
            }
        }

        // Now start HackRF
        qDebug() << "Starting HackRF device...";
        if (m_hackTvLib->start()) {
            m_hackRfRunning = true;

            m_hackTvLib->setLnaGain(40);
            m_hackTvLib->setVgaGain(20);
            m_hackTvLib->setRxAmpGain(14);

            m_startStopButton->setText("Stop HackRF");
            m_startStopButton->setStyleSheet("QPushButton { background-color: #ff5555; color: white; padding: 10px; font-weight: bold; }");
            qDebug() << "HackRF started successfully";
            qDebug() << "Frequency:" << (m_currentFrequency / 1000000) << "MHz";
            qDebug() << "Sample rate:" << SAMP_RATE << "Hz";
        } else {
            // Failed to start - stop processor thread
            qWarning() << "Failed to start HackRF!";
            if (m_processorThread) {
                m_processorThread->stopProcessing();
                m_processorThread->quit();
                m_processorThread->wait(1000);
            }
            QMessageBox::critical(this, "Error", "Failed to start HackRF. Check device connection.");
        }
    }
}

void MainWindow::onFrameReady(const QImage& frame)
{
    if (m_shuttingDown) return;

    // Create a deep copy immediately
    QImage frameCopy = frame.copy();

    QMutexLocker locker(&m_frameMutex);
    m_frameCount++;
    m_currentFrame = frameCopy;

    if (m_videoLabel) {
        QPixmap pixmap = QPixmap::fromImage(frameCopy);
        QSize labelSize(m_videoLabel->width(), m_videoLabel->height());
        QSize scaledSize = frameCopy.size().scaled(labelSize, Qt::KeepAspectRatio);
        pixmap = pixmap.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        m_videoLabel->setPixmap(pixmap);
    }
}

void MainWindow::updateStatus()
{
    // Calculate FPS
    qint64 elapsed = m_fpsTimer.elapsed();
    if (elapsed > 0) {
        float fps = (m_frameCount * 1000.0f) / elapsed;
        m_fpsLabel->setText(QString("FPS: %1").arg(fps, 0, 'f', 1));
    }

    // Reset counters
    m_frameCount = 0;
    m_fpsTimer.restart();

    // Update status
    QString status = QString("Status: %1 | Freq: 478 MHz | Rate: 16 MHz")
                         .arg(m_hackRfRunning ? "Running" : "Stopped");

    if (m_hackRfRunning && m_palDecoder) {
        status += QString(" | V.Gain: %1 | V.Offset: %2")
                      .arg(m_palDecoder->getVideoGain(), 0, 'f', 1)
                      .arg(m_palDecoder->getVideoOffset(), 0, 'f', 2);

        // Add buffer stats
        if (m_circularBuffer) {
            size_t bufferUsage = m_circularBuffer->availableData();
            uint64_t dropped = m_circularBuffer->droppedFrames();
            status += QString(" | Buf: %1 KB | Dropped: %2")
                          .arg(bufferUsage / 1024)
                          .arg(dropped);
        }
    }

    m_statusLabel->setText(status);
}

// ============================================================================
// HackRF Integration
// ============================================================================

void MainWindow::initHackRF()
{
    qDebug() << "Initializing HackRF...";

    // Create HackTvLib instance
    m_hackTvLib = std::make_unique<HackTvLib>(this);

    // Setup arguments for RX mode with current frequency
    QStringList args;
    args << "--rx-tx-mode" << "rx";
    args << "-a";  // Enable amp
    args << "--filter";
    args << "-f" << QString::number(m_currentFrequency);  // Use current frequency
    args << "-s" << QString::number(SAMP_RATE);

    // Convert to std::vector<std::string>
    std::vector<std::string> stdArgs;
    stdArgs.reserve(args.size());
    for (const QString& arg : args) {
        stdArgs.push_back(arg.toStdString());
    }

    // Set arguments
    m_hackTvLib->setArguments(stdArgs);

    // Set initial gains
    m_hackTvLib->setLnaGain(40);
    m_hackTvLib->setVgaGain(20);
    m_hackTvLib->setRxAmpGain(14);

    // Set mic disabled
    m_hackTvLib->setMicEnabled(false);

    // Setup log callback
    m_hackTvLib->setLogCallback([this](const std::string& msg) {
        if (!m_shuttingDown.load() && this && m_hackTvLib) {
            // Copy message to avoid dangling reference
            std::string msgCopy = msg;
            QMetaObject::invokeMethod(this, [this, msgCopy]() {
                    if (this && !m_shuttingDown.load()) {
                        qDebug() << "[HackRF]" << QString::fromStdString(msgCopy);
                    }
                }, Qt::QueuedConnection);
        }
    });

    // Setup data callback - THIS IS THE KEY INTEGRATION
    m_hackTvLib->setReceivedDataCallback([this](const int8_t* data, size_t len) {
        // Validate pointers and state
        if (!this || m_shuttingDown.load() || !m_hackTvLib || !m_circularBuffer) {
            return;
        }

        // Validate data
        if (!data || len == 0) {
            return;
        }

        // Expected callback size from HackRF
        if (len != 262144) {
            static bool warned = false;
            if (!warned) {
                qWarning() << "Unexpected callback size:" << len << "expected 262144";
                warned = true;
            }
        }

        // Write directly to circular buffer (this is FAST)
        // No need to copy or use QMetaObject - just write to buffer
        if (m_circularBuffer) {
            bool success = m_circularBuffer->write(data, len);
            if (!success) {
                // Buffer full - this is logged by the buffer stats
            }
        }
    });

    qDebug() << "HackRF initialized:";
    qDebug() << "  Sample rate:" << SAMP_RATE << "Hz";
    qDebug() << "  Frequency:" << m_currentFrequency << "Hz ("
             << (m_currentFrequency / 1000000) << "MHz)";  
    qDebug() << "  Circular buffer size:" << (64 * 1024 * 1024) << "bytes";

    m_hackRfRunning = false;
    qDebug() << "HackRF ready. Click 'Start HackRF' button to begin.";
}
void MainWindow::handleSamples(const std::vector<std::complex<float>>& samples)
{
    if (m_shuttingDown || !m_palDecoder) return;

    // Process samples through PAL decoder
    m_palDecoder->processSamples(samples);
}

void MainWindow::handleReceivedData(const int8_t* data, size_t len)
{
    if (m_shuttingDown || !m_circularBuffer || !data || len == 0) return;
    m_circularBuffer->write(data, len);
}

void MainWindow::onBufferStats(size_t available, uint64_t dropped)
{
    // Log dropped frames
    if (dropped > m_lastDroppedFrames) {
        m_lastDroppedFrames = dropped;
    }
}
