#include "MainWindow.h"
#include "hacktvlib.h"  // Your HackRF library
#include <QDebug>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_frameCount(0)
    , m_shuttingDown(false)
    , m_hackRfRunning(false)
{
    // Create PAL decoder
    m_palDecoder = std::make_unique<PALDecoder>(this);

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
    resize(900, 850);

    // Initialize HackRF automatically
    initHackRF();
}

MainWindow::~MainWindow()
{
    m_shuttingDown = true;

    if (m_statusTimer) {
        m_statusTimer->stop();
    }

    // Stop HackRF
    if (m_hackTvLib && m_hackRfRunning) {
        m_hackTvLib->stop();
    }
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
    m_vgaGainSlider->setValue(40);
    m_vgaGainSlider->setTickPosition(QSlider::TicksBelow);
    m_vgaGainSlider->setTickInterval(8);
    m_vgaGainSpinBox = new QSpinBox(this);
    m_vgaGainSpinBox->setRange(0, 62);
    m_vgaGainSpinBox->setValue(40);
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
    m_videoGainSlider->setValue(20); // Default 2.0
    m_videoGainSlider->setTickPosition(QSlider::TicksBelow);
    m_videoGainSlider->setTickInterval(10);
    m_videoGainSpinBox = new QDoubleSpinBox(this);
    m_videoGainSpinBox->setRange(0.1, 10.0);
    m_videoGainSpinBox->setSingleStep(0.1);
    m_videoGainSpinBox->setValue(2.0);
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

    mainLayout->addWidget(videoControlGroup);

    // Info label
    QLabel* infoLabel = new QLabel(
        "<b>PAL-B/G Decoder with HackRF</b><br>"
        "• Frequency: 478 MHz<br>"
        "• Sample Rate: 16 MHz<br>"
        "• Line Frequency: 15625 Hz<br>"
        "• Image: 384×576 pixels (Grayscale)<br><br>"
        "<i>Adjust HackRF gains for signal strength, Video Gain/Offset for picture quality.</i>",
        this);
    infoLabel->setWordWrap(true);
    //infoLabel->setStyleSheet("QLabel { padding: 10px; background-color: #f0f0f0; border-radius: 5px; }");
    mainLayout->addWidget(infoLabel);
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
        if (m_hackTvLib) {
            m_hackTvLib->stop();
            m_hackRfRunning = false;
            m_startStopButton->setText("Start HackRF");
            m_startStopButton->setStyleSheet("QPushButton { background-color: #55ff55; color: black; padding: 10px; font-weight: bold; }");
            qDebug() << "HackRF stopped";
        }
    } else {
        // Start HackRF
        if (m_hackTvLib) {
            if (m_hackTvLib->start()) {
                m_hackRfRunning = true;
                m_startStopButton->setText("Stop HackRF");
                m_startStopButton->setStyleSheet("QPushButton { background-color: #ff5555; color: white; padding: 10px; font-weight: bold; }");
                qDebug() << "HackRF started";
            } else {
                QMessageBox::critical(this, "Error", "Failed to start HackRF. Check device connection.");
            }
        }
    }
}

void MainWindow::onFrameReady(const QImage& frame)
{
    if (m_shuttingDown) return;

    QMutexLocker locker(&m_frameMutex);

    // Update frame count for FPS calculation
    m_frameCount++;

    // Store current frame
    m_currentFrame = frame;

    // Scale to fit display while maintaining aspect ratio
    QPixmap pixmap = QPixmap::fromImage(frame);

    // Scale to label size
    if (m_videoLabel) {
        int labelWidth = m_videoLabel->width();
        int labelHeight = m_videoLabel->height();

        // Calculate scaled size maintaining aspect ratio
        QSize scaledSize = frame.size().scaled(labelWidth, labelHeight, Qt::KeepAspectRatio);
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

    // Setup arguments for RX mode
    QStringList args;
    args << "--rx-tx-mode" << "rx";
    args << "-a";  // Enable amp
    args << "--filter";
    args << "-f" << QString::number(FREQ);
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
    m_hackTvLib->setVgaGain(40);
    m_hackTvLib->setRxAmpGain(14);

    // Set mic disabled
    m_hackTvLib->setMicEnabled(false);

    // Setup log callback
    m_hackTvLib->setLogCallback([this](const std::string& msg) {
        if (!m_shuttingDown.load() && this && m_hackTvLib) {
            QMetaObject::invokeMethod(this, [this, msg]() {
                if (this && !m_shuttingDown.load()) {
                    qDebug() << "[HackRF]" << QString::fromStdString(msg);
                }
            }, Qt::QueuedConnection);
        }
    });

    // Setup data callback - THIS IS THE KEY INTEGRATION
    m_hackTvLib->setReceivedDataCallback([this](const int8_t* data, size_t len) {
        if (!m_shuttingDown.load() && this && m_hackTvLib && data && len == 262144) {
            // Copy data to avoid dangling pointer
            QByteArray dataCopy(reinterpret_cast<const char*>(data), len);
            QMetaObject::invokeMethod(this, [this, dataCopy]() {
                if (this && !m_shuttingDown.load()) {
                    handleReceivedData(
                        reinterpret_cast<const int8_t*>(dataCopy.data()),
                        dataCopy.size()
                        );
                }
            }, Qt::QueuedConnection);
        }
    });

    qDebug() << "HackRF initialized:";
    qDebug() << "  Sample rate:" << SAMP_RATE << "Hz";
    qDebug() << "  Frequency:" << FREQ << "Hz";
    qDebug() << "  LNA Gain: 40 dB";
    qDebug() << "  VGA Gain: 40 dB";
    qDebug() << "  RX Amp: 14 dB";

    // *** DO NOT AUTO-START - Wait for user to click Start button ***
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
    if (m_shuttingDown || !m_palDecoder || !data || len == 0) return;

    // Process int8_t IQ data through PAL decoder
    m_palDecoder->processSamples(data, len);
}
