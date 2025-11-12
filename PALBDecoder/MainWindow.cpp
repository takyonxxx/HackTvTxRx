#include "MainWindow.h"
#include "hacktvlib.h"  // Your HackRF library
#include <QDebug>
#include <QFuture>
#include <QtConcurrent/QtConcurrent>
#include <memory>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>
#include <QCheckBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_threadPool(nullptr)
    , m_frameCount(0)
    , m_shuttingDown(false)
    , m_hackRfRunning(false)
    , m_currentFrequency(DEFAULT_FREQ)
    , m_currentSampleRate(SAMP_RATE)
{
    m_threadPool = new QThreadPool(this);
    if (m_threadPool) {
        m_threadPool->setMaxThreadCount(QThread::idealThreadCount() / 2);
    }

    palFrameBuffer = new FrameBuffer(m_currentSampleRate, 0.04);

    // Create PAL decoder
    m_palDecoder = std::make_shared<PALDecoder>(this);
    m_audioDemodulator = std::make_unique<AudioDemodulator>(this);
    m_audioOutput = std::make_unique<AudioOutput>();

    m_audioDemodulator->setSampleRate(m_currentSampleRate);

    // Connect frame ready signal
    connect(m_palDecoder.get(), &PALDecoder::frameReady,
            this, &MainWindow::onFrameReady, Qt::QueuedConnection);

    connect(m_palDecoder.get(), &PALDecoder::syncStatsUpdated,
            this, &MainWindow::onSyncStatsUpdated, Qt::QueuedConnection);

    connect(
        m_audioDemodulator.get(), &AudioDemodulator::audioReady,
        this, &MainWindow::onAudioReady, Qt::QueuedConnection);

    // Connect frame ready signal
    connect(m_palDecoder.get(), &PALDecoder::frameReady,
            this, &MainWindow::onFrameReady, Qt::QueuedConnection);

    // Setup UI
    setupUI();

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

    // ========== MAIN HORIZONTAL LAYOUT (2 COLUMNS) ==========
    QHBoxLayout* columnsLayout = new QHBoxLayout();
    columnsLayout->setSpacing(15);

    // ========== LEFT COLUMN ==========
    QVBoxLayout* leftColumn = new QVBoxLayout();
    leftColumn->setSpacing(10);

    // Video display - 576x384 landscape
    QGroupBox* videoGroup = new QGroupBox("PAL Video Display (576×384)", this);
        QVBoxLayout* videoLayout = new QVBoxLayout(videoGroup);

    m_videoLabel = new QLabel(this);
    m_videoLabel->setFixedSize(576, 384);  // Landscape gösterim
    m_videoLabel->setScaledContents(false);
    m_videoLabel->setStyleSheet("QLabel { background-color: black; border: 2px solid #333; }");
    m_videoLabel->setAlignment(Qt::AlignCenter);

    // Set initial placeholder (rotated)
    QImage placeholder(384, 576, QImage::Format_Grayscale8);
    placeholder.fill(Qt::black);
    QTransform transform;
    transform.rotate(90); // Saat yönünde 90°
    QImage rotated = placeholder.transformed(transform);
    m_videoLabel->setPixmap(QPixmap::fromImage(rotated));

    videoLayout->addWidget(m_videoLabel);
    videoGroup->setFixedWidth(592);
    leftColumn->addWidget(videoGroup);

    // Video Processing Controls (BELOW VIDEO)
    QGroupBox* videoControlGroup = new QGroupBox("Video Processing", this);
    videoControlGroup->setMaximumWidth(592); // Video ile aynı genişlik
    QVBoxLayout* videoControlLayout = new QVBoxLayout(videoControlGroup);

    // Video Gain
    QHBoxLayout* videoGainLayout = new QHBoxLayout();
    videoGainLayout->addWidget(new QLabel("Gain:", this));
    m_videoGainSlider = new QSlider(Qt::Horizontal, this);
    m_videoGainSlider->setRange(1, 100);
    m_videoGainSlider->setValue(15);
    m_videoGainSlider->setTickPosition(QSlider::TicksBelow);
    m_videoGainSlider->setTickInterval(10);
    m_videoGainSpinBox = new QDoubleSpinBox(this);
    m_videoGainSpinBox->setRange(0.1, 10.0);
    m_videoGainSpinBox->setSingleStep(0.1);
    m_videoGainSpinBox->setValue(1.5);
    m_videoGainSpinBox->setDecimals(1);
    m_videoGainSpinBox->setMaximumWidth(80);
    connect(m_videoGainSlider, &QSlider::valueChanged, this, &MainWindow::onVideoGainChanged);
    connect(m_videoGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double value) { m_videoGainSlider->setValue(static_cast<int>(value * 10)); });
    videoGainLayout->addWidget(m_videoGainSlider, 1);
    videoGainLayout->addWidget(m_videoGainSpinBox);
    videoControlLayout->addLayout(videoGainLayout);

    // Video Offset
    QHBoxLayout* videoOffsetLayout = new QHBoxLayout();
    videoOffsetLayout->addWidget(new QLabel("Offset:", this));
    m_videoOffsetSlider = new QSlider(Qt::Horizontal, this);
    m_videoOffsetSlider->setRange(-100, 100);
    m_videoOffsetSlider->setValue(0);
    m_videoOffsetSlider->setTickPosition(QSlider::TicksBelow);
    m_videoOffsetSlider->setTickInterval(20);
    m_videoOffsetSpinBox = new QDoubleSpinBox(this);
    m_videoOffsetSpinBox->setRange(-1.0, 1.0);
    m_videoOffsetSpinBox->setSingleStep(0.01);
    m_videoOffsetSpinBox->setValue(0.0);
    m_videoOffsetSpinBox->setDecimals(2);
    m_videoOffsetSpinBox->setMaximumWidth(80);
    connect(m_videoOffsetSlider, &QSlider::valueChanged, this, &MainWindow::onVideoOffsetChanged);
    connect(m_videoOffsetSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double value) { m_videoOffsetSlider->setValue(static_cast<int>(value * 100)); });
    videoOffsetLayout->addWidget(m_videoOffsetSlider, 1);
    videoOffsetLayout->addWidget(m_videoOffsetSpinBox);
    videoControlLayout->addLayout(videoOffsetLayout);

    // Invert checkbox
    m_invertVideoCheckBox = new QCheckBox("Invert Video (Negative)", this);
    m_invertVideoCheckBox->setStyleSheet("QCheckBox { font-weight: bold; }");
    connect(m_invertVideoCheckBox, &QCheckBox::stateChanged,
            this, &MainWindow::onInvertVideoChanged);
    videoControlLayout->addWidget(m_invertVideoCheckBox);

    leftColumn->addWidget(videoControlGroup);

    // Audio Controls (NEW - BELOW VIDEO CONTROLS)
    QGroupBox* audioControlGroup = new QGroupBox("Audio Controls", this);
    audioControlGroup->setMaximumWidth(592);
    QVBoxLayout* audioControlLayout = new QVBoxLayout(audioControlGroup);

    // Audio Enable Checkbox
    m_audioEnabledCheckBox = new QCheckBox("Enable Audio", this);
    m_audioEnabledCheckBox->setChecked(true);
    m_audioEnabledCheckBox->setStyleSheet("QCheckBox { font-weight: bold; }");
    connect(m_audioEnabledCheckBox, &QCheckBox::stateChanged,
            this, &MainWindow::onAudioEnabledChanged);
    audioControlLayout->addWidget(m_audioEnabledCheckBox);

    // Audio Gain
    QHBoxLayout* audioGainLayout = new QHBoxLayout();
    audioGainLayout->addWidget(new QLabel("Audio Gain:", this));

    m_audioGainSlider = new QSlider(Qt::Horizontal, this);
    m_audioGainSlider->setRange(0, 100);
    m_audioGainSlider->setValue(10);  // 1.0x gain
    m_audioGainSlider->setTickPosition(QSlider::TicksBelow);
    m_audioGainSlider->setTickInterval(10);

    m_audioGainSpinBox = new QDoubleSpinBox(this);
    m_audioGainSpinBox->setRange(0.0, 10.0);
    m_audioGainSpinBox->setSingleStep(0.1);
    m_audioGainSpinBox->setValue(1.0);
    m_audioGainSpinBox->setDecimals(1);
    m_audioGainSpinBox->setMaximumWidth(80);

    connect(m_audioGainSlider, &QSlider::valueChanged,
            this, &MainWindow::onAudioGainChanged);
    connect(m_audioGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double value) {
                m_audioGainSlider->setValue(static_cast<int>(value * 10));                
            });

    audioGainLayout->addWidget(m_audioGainSlider, 1);
    audioGainLayout->addWidget(m_audioGainSpinBox);
    audioControlLayout->addLayout(audioGainLayout);

    // System Volume Control
    QHBoxLayout* volumeLayout = new QHBoxLayout();
    volumeLayout->addWidget(new QLabel("Volume:", this));

    QSlider* volumeSlider = new QSlider(Qt::Horizontal, this);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(10);
    volumeSlider->setTickPosition(QSlider::TicksBelow);
    volumeSlider->setTickInterval(10);
    if(m_audioOutput)
        m_audioOutput->setVolume(10);

    QLabel* volumeLabel = new QLabel("10%", this);
    volumeLabel->setMinimumWidth(50);
    volumeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(volumeSlider,  &QSlider::valueChanged,
            [this, volumeLabel](int value) {
                m_audioOutput->setVolume(value);
                volumeLabel->setText(QString("%1%").arg(value));
            });

    volumeLayout->addWidget(volumeSlider, 1);
    volumeLayout->addWidget(volumeLabel);
    audioControlLayout->addLayout(volumeLayout);

    leftColumn->addWidget(audioControlGroup);

    leftColumn->addStretch();

    columnsLayout->addLayout(leftColumn);

    // ========== RIGHT COLUMN ==========
    QVBoxLayout* rightColumn = new QVBoxLayout();
    rightColumn->setSpacing(10);

    // ========== SAMPLE RATE SELECTION (NEW - TOP OF RIGHT COLUMN) ==========
    QGroupBox* sampleRateGroup = new QGroupBox("Sample Rate", this);
    QHBoxLayout* sampleRateLayout = new QHBoxLayout(sampleRateGroup);

    sampleRateLayout->addWidget(new QLabel("Rate:", this));

    m_sampleRateComboBox = new QComboBox(this);
    m_sampleRateComboBox->setStyleSheet("QComboBox { font-weight: bold; }");

    // Add sample rates
    std::map<int, QString> sortedSampleRates {
        {2000000, "2"},
        {4000000, "4"},
        {8000000, "8"},
        {10000000, "10"},
        {12500000, "12.5"},
        {16000000, "16"},
        {20000000, "20"}
    };

    int defaultIndex = 0;
    int currentIndex = 0;
    for (const auto& [rate, displayText] : sortedSampleRates) {
        m_sampleRateComboBox->addItem(displayText + " MHz", rate);
        if (rate == 16000000) {
            defaultIndex = currentIndex;
        }
        currentIndex++;
    }

    m_sampleRateComboBox->setCurrentIndex(defaultIndex);

    connect(m_sampleRateComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSampleRateChanged);

    sampleRateLayout->addWidget(m_sampleRateComboBox);
    sampleRateLayout->addStretch();

    rightColumn->addWidget(sampleRateGroup);

    // Frequency Control (TOP RIGHT)
    QGroupBox* freqGroup = new QGroupBox("Frequency Control - UHF TV Band", this);
    QVBoxLayout* freqLayout = new QVBoxLayout(freqGroup);

    QHBoxLayout* freqTopLayout = new QHBoxLayout();
    m_frequencySpinBox = new QDoubleSpinBox(this);
    m_frequencySpinBox->setRange(470.0, 862.0);
    m_frequencySpinBox->setValue(478.0);
    m_frequencySpinBox->setSingleStep(0.1);
    m_frequencySpinBox->setDecimals(3);
    m_frequencySpinBox->setSuffix(" MHz");
    m_frequencySpinBox->setMinimumWidth(150);
    m_frequencySpinBox->setStyleSheet("QDoubleSpinBox { font-size: 12pt; font-weight: bold; }");
    connect(m_frequencySpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MainWindow::onFrequencySpinBoxChanged);
    freqTopLayout->addWidget(m_frequencySpinBox);

    m_channelLabel = new QLabel(this);
    m_channelLabel->setStyleSheet(
        "QLabel {"
        "    font-size: 11pt;"
        "    font-weight: bold;"
        "    color: #3388ff;"
        "    padding: 5px;"
        "    background-color: #f0f0f0;"
        "    border-radius: 3px;"
        "    border: 2px solid #3388ff;"
        "}"
        );
    m_channelLabel->setAlignment(Qt::AlignCenter);
    updateChannelLabel(m_currentFrequency);
    freqTopLayout->addWidget(m_channelLabel, 1);
    freqLayout->addLayout(freqTopLayout);

    QHBoxLayout* sliderLayout = new QHBoxLayout();
    QLabel* minLabel = new QLabel("470", this);
    sliderLayout->addWidget(minLabel);

    m_frequencySlider = new QSlider(Qt::Horizontal, this);
    m_frequencySlider->setRange(470, 862);
    m_frequencySlider->setValue(478);
    m_frequencySlider->setTickPosition(QSlider::TicksBelow);
    m_frequencySlider->setTickInterval(8);
    connect(m_frequencySlider, &QSlider::valueChanged,
            this, &MainWindow::onFrequencySliderChanged);
    sliderLayout->addWidget(m_frequencySlider, 1);

    QLabel* maxLabel = new QLabel("862", this);
    sliderLayout->addWidget(maxLabel);
    freqLayout->addLayout(sliderLayout);

    rightColumn->addWidget(freqGroup);

    // HackRF Controls
    QGroupBox* hackRfGroup = new QGroupBox("HackRF Controls", this);
    QVBoxLayout* hackRfLayout = new QVBoxLayout(hackRfGroup);

    m_startStopButton = new QPushButton("Start HackRF", this);
    m_startStopButton->setStyleSheet("QPushButton { background-color: #55ff55; color: black; padding: 10px; font-weight: bold; }");
    connect(m_startStopButton, &QPushButton::clicked, this, &MainWindow::toggleHackRF);
    hackRfLayout->addWidget(m_startStopButton);

    // LNA Gain (compact)
    QHBoxLayout* lnaLayout = new QHBoxLayout();
    lnaLayout->addWidget(new QLabel("LNA:", this));
    m_lnaGainSlider = new QSlider(Qt::Horizontal, this);
    m_lnaGainSlider->setRange(0, 40);
    m_lnaGainSlider->setValue(40);
    m_lnaGainSlider->setTickPosition(QSlider::TicksBelow);
    m_lnaGainSlider->setTickInterval(8);
    m_lnaGainSpinBox = new QSpinBox(this);
    m_lnaGainSpinBox->setRange(0, 40);
    m_lnaGainSpinBox->setValue(40);
    m_lnaGainSpinBox->setMaximumWidth(100);
    connect(m_lnaGainSlider, &QSlider::valueChanged, this, &MainWindow::onLnaGainChanged);
    connect(m_lnaGainSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int value) { m_lnaGainSlider->setValue(value); });
    lnaLayout->addWidget(m_lnaGainSlider, 1);
    lnaLayout->addWidget(m_lnaGainSpinBox);
    hackRfLayout->addLayout(lnaLayout);

    // VGA Gain (compact)
    QHBoxLayout* vgaLayout = new QHBoxLayout();
    vgaLayout->addWidget(new QLabel("VGA:", this));
    m_vgaGainSlider = new QSlider(Qt::Horizontal, this);
    m_vgaGainSlider->setRange(0, 62);
    m_vgaGainSlider->setValue(20);
    m_vgaGainSlider->setTickPosition(QSlider::TicksBelow);
    m_vgaGainSlider->setTickInterval(8);
    m_vgaGainSpinBox = new QSpinBox(this);
    m_vgaGainSpinBox->setRange(0, 62);
    m_vgaGainSpinBox->setValue(20);
    m_vgaGainSpinBox->setMaximumWidth(100);
    connect(m_vgaGainSlider, &QSlider::valueChanged, this, &MainWindow::onVgaGainChanged);
    connect(m_vgaGainSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int value) { m_vgaGainSlider->setValue(value); });
    vgaLayout->addWidget(m_vgaGainSlider, 1);
    vgaLayout->addWidget(m_vgaGainSpinBox);
    hackRfLayout->addLayout(vgaLayout);

    // RX Amp (compact)
    QHBoxLayout* rxAmpLayout = new QHBoxLayout();
    rxAmpLayout->addWidget(new QLabel("RX Amp:", this));
    m_rxAmpGainSlider = new QSlider(Qt::Horizontal, this);
    m_rxAmpGainSlider->setRange(0, 14);
    m_rxAmpGainSlider->setValue(14);
    m_rxAmpGainSlider->setTickPosition(QSlider::TicksBelow);
    m_rxAmpGainSlider->setTickInterval(2);
    m_rxAmpGainSpinBox = new QSpinBox(this);
    m_rxAmpGainSpinBox->setRange(0, 14);
    m_rxAmpGainSpinBox->setValue(14);
    m_rxAmpGainSpinBox->setMaximumWidth(100);
    connect(m_rxAmpGainSlider, &QSlider::valueChanged, this, &MainWindow::onRxAmpGainChanged);
    connect(m_rxAmpGainSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int value) { m_rxAmpGainSlider->setValue(value); });
    rxAmpLayout->addWidget(m_rxAmpGainSlider, 1);
    rxAmpLayout->addWidget(m_rxAmpGainSpinBox);
    hackRfLayout->addLayout(rxAmpLayout);

    rightColumn->addWidget(hackRfGroup);

    // Sync Detection Controls
    QGroupBox* syncControlGroup = new QGroupBox("Sync Detection", this);
    QVBoxLayout* syncControlLayout = new QVBoxLayout(syncControlGroup);

    // Sync Rate Display
    QHBoxLayout* syncRateLayout = new QHBoxLayout();
    syncRateLayout->addWidget(new QLabel("<b>Sync:</b>", this));
    m_syncRateLabel = new QLabel("---%", this);
    m_syncRateLabel->setStyleSheet(
        "QLabel {"
        "    font-size: 14pt;"
        "    font-weight: bold;"
        "    color: #00aa00;"
        "    padding: 5px;"
        "    background-color: #f0f0f0;"
        "    border-radius: 3px;"
        "}"
        );
    m_syncRateLabel->setMinimumWidth(80);
    syncRateLayout->addWidget(m_syncRateLabel);
    syncRateLayout->addStretch();
    syncControlLayout->addLayout(syncRateLayout);

    // Sync Threshold
    QHBoxLayout* syncThresholdLayout = new QHBoxLayout();
    syncThresholdLayout->addWidget(new QLabel("Threshold:", this));

    m_syncThresholdSlider = new QSlider(Qt::Horizontal, this);
    m_syncThresholdSlider->setRange(-100, 0);
    m_syncThresholdSlider->setValue(-20);
    m_syncThresholdSlider->setTickPosition(QSlider::TicksBelow);
    m_syncThresholdSlider->setTickInterval(10);

    m_syncThresholdSpinBox = new QDoubleSpinBox(this);
    m_syncThresholdSpinBox->setRange(-1.0, 0.0);
    m_syncThresholdSpinBox->setSingleStep(0.01);
    m_syncThresholdSpinBox->setValue(-0.2);
    m_syncThresholdSpinBox->setDecimals(2);
    m_syncThresholdSpinBox->setMaximumWidth(100);

    connect(m_syncThresholdSlider, &QSlider::valueChanged,
            this, &MainWindow::onSyncThresholdChanged);
    connect(m_syncThresholdSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double value) {
                m_syncThresholdSlider->setValue(static_cast<int>(value * 100));
            });

    syncThresholdLayout->addWidget(m_syncThresholdSlider, 1);
    syncThresholdLayout->addWidget(m_syncThresholdSpinBox);
    syncControlLayout->addLayout(syncThresholdLayout);

    QLabel* syncHelpLabel = new QLabel(
        "<i>Adjust for stable sync (try -0.2 to -0.3)</i>", this);
    syncHelpLabel->setWordWrap(true);
    syncHelpLabel->setStyleSheet("QLabel { color: #666; font-size: 8pt; }");
    syncControlLayout->addWidget(syncHelpLabel);

    rightColumn->addWidget(syncControlGroup);
    rightColumn->addStretch();

    columnsLayout->addLayout(rightColumn, 1); // Give right column more stretch

    mainLayout->addLayout(columnsLayout);

    // ========== BOTTOM STATUS BAR ==========
    QHBoxLayout* statusLayout = new QHBoxLayout();
    m_statusLabel = new QLabel("Status: Initializing...", this);
    m_statusLabel->setStyleSheet("QLabel { font-size: 9pt; }");
    m_fpsLabel = new QLabel("FPS: 0.0", this);
    m_fpsLabel->setStyleSheet("QLabel { font-size: 9pt; font-weight: bold; }");
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    statusLayout->addWidget(m_fpsLabel);
    mainLayout->addLayout(statusLayout);
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

void MainWindow::onAudioGainChanged(int value)
{
    float gain = value / 10.0f;  // 0-100 -> 0.0-10.0

    m_audioGainSpinBox->blockSignals(true);
    m_audioGainSpinBox->setValue(gain);
    m_audioGainSpinBox->blockSignals(false);

    if (m_audioDemodulator) {  // PALDecoder → AudioDemodulator
        m_audioDemodulator->setAudioGain(gain);
        qDebug() << "Audio gain set to:" << gain;
    }
}

void MainWindow::onAudioEnabledChanged(int state)
{
    bool enabled = (state == Qt::Checked);

    if (m_audioDemodulator) {  // PALDecoder → AudioDemodulator
        m_audioDemodulator->setAudioEnabled(enabled);
    }

    qDebug() << "Audio:" << (enabled ? "ENABLED" : "DISABLED");
}

void MainWindow::onAudioReady(const std::vector<float>& audioSamples)
{
    if (m_audioOutput) {
        m_audioOutput->enqueueAudio(audioSamples);
    } else {
        qCritical() << "AudioOutput is NULL!";
    }
}

void MainWindow::onSampleRateChanged(int index)
{
    if (!m_sampleRateComboBox) return;

    int newSampleRate = m_sampleRateComboBox->itemData(index).toInt();

    if (newSampleRate == m_currentSampleRate) {
        return; // No change
    }

    qDebug() << "Sample rate changed from" << m_currentSampleRate
             << "to" << newSampleRate;

    m_currentSampleRate = newSampleRate;

    // If HackRF is running, restart with new sample rate
    if (m_hackTvLib) {
        m_hackTvLib->setSampleRate(m_currentSampleRate);
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
    m_hackTvLib->setSampleRate(m_currentSampleRate);
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
        // ========== STOP HackRF ==========
        qDebug() << "=== STOPPING HackRF ===";

        if (m_hackTvLib) {
            m_hackTvLib->stop();
        }

        m_hackRfRunning = false;

        // Reset sample rate to 16 MHz
        m_currentSampleRate = 16000000;

        // Update combobox
        if (m_sampleRateComboBox) {
            for (int i = 0; i < m_sampleRateComboBox->count(); i++) {
                if (m_sampleRateComboBox->itemData(i).toInt() == 16000000) {
                    m_sampleRateComboBox->blockSignals(true);
                    m_sampleRateComboBox->setCurrentIndex(i);
                    m_sampleRateComboBox->blockSignals(false);
                    break;
                }
            }
        }

        m_startStopButton->setText("Start HackRF");
        m_startStopButton->setStyleSheet(
            "QPushButton { background-color: #55ff55; color: black; "
            "padding: 10px; font-weight: bold; }");
        qDebug() << "✓ HackRF stopped successfully";

    } else {
        // ========== START HackRF ==========
        qDebug() << "=== STARTING HackRF ===";

        // ===== VALIDATION CHECKS =====

        // 1. Check if library exists
        if (!m_hackTvLib) {
            qCritical() << "❌ m_hackTvLib is NULL!";
            QMessageBox::critical(this, "Error",
                                  "HackTvLib not initialized!\n\n"
                                  "This is a critical error. Please restart the application.");
            return;
        }

        // Force sample rate to 16 MHz
        m_currentSampleRate = 16000000;

        // Update combobox to 16 MHz
        if (m_sampleRateComboBox) {
            for (int i = 0; i < m_sampleRateComboBox->count(); i++) {
                if (m_sampleRateComboBox->itemData(i).toInt() == 16000000) {
                    m_sampleRateComboBox->blockSignals(true);
                    m_sampleRateComboBox->setCurrentIndex(i);
                    m_sampleRateComboBox->blockSignals(false);
                    break;
                }
            }
        }

        // Configure arguments
        QStringList args;
        args << "--rx-tx-mode" << "rx";
        args << "-a";
        args << "--filter";
        args << "-f" << QString::number(m_currentFrequency);
        args << "-s" << QString::number(m_currentSampleRate);  // Always 16 MHz

        std::vector<std::string> stdArgs;
        stdArgs.reserve(args.size());
        for (const QString& arg : args) {
            stdArgs.push_back(arg.toStdString());
        }

        qDebug() << "Setting arguments...";
        m_hackTvLib->setArguments(stdArgs);

        // ===== START DEVICE =====

        qDebug() << "Calling start()...";
        qDebug() << "  Frequency:" << (m_currentFrequency / 1000000) << "MHz";
        qDebug() << "  Sample rate:" << m_currentSampleRate << "Hz";

        bool startSuccess = false;
        try {
            startSuccess = m_hackTvLib->start();
        } catch (const std::exception& e) {
            qCritical() << "❌ Exception calling start():" << e.what();
            QMessageBox::critical(this, "Error",
                                  QString("Failed to start HackRF!\n\nException: %1")
                                      .arg(e.what()));
            return;
        } catch (...) {
            qCritical() << "❌ Unknown exception calling start()";
            QMessageBox::critical(this, "Error",
                                  "Failed to start HackRF!\n\nUnknown exception occurred.");
            return;
        }

        if (startSuccess) {
            // SUCCESS!
            m_hackRfRunning = true;

            // Set gains
            m_hackTvLib->setLnaGain(40);
            m_hackTvLib->setVgaGain(20);
            m_hackTvLib->setRxAmpGain(14);

            m_startStopButton->setText("Stop HackRF");
            m_startStopButton->setStyleSheet(
                "QPushButton { background-color: #ff5555; color: white; "
                "padding: 10px; font-weight: bold; }");

            qDebug() << "✓✓✓ HackRF started successfully ✓✓✓";

        } else {
            // FAILED TO START
            qCritical() << "❌ start() returned false";

            QMessageBox::critical(this, "Error",
                                  "Failed to start HackRF!\n\n"
                                  "Possible causes:\n"
                                  "• HackRF device not connected\n"
                                  "• Device already in use\n"
                                  "• USB permissions issue\n"
                                  "• Driver problem\n\n"
                                  "Check the console output for details.");
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

    m_frameCount = 0;
    m_fpsTimer.restart();

    // Update status
    QString status = QString("Status: %1 | Freq: %2 MHz | Rate: %3 MHz")
                         .arg(m_hackRfRunning ? "Running" : "Stopped")
                         .arg(m_currentFrequency / 1000000)
                         .arg(m_currentSampleRate / 1000000.0, 0, 'f', 1);

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

    m_hackTvLib = std::make_unique<HackTvLib>(this);

    palDemodulationInProgress.storeRelease(0);

    QStringList args;
    args << "--rx-tx-mode" << "rx";
    args << "-a";
    args << "--filter";
    args << "-f" << QString::number(m_currentFrequency);
    args << "-s" << QString::number(m_currentSampleRate);  // <<<< Dinamik sample rate

    std::vector<std::string> stdArgs;
    stdArgs.reserve(args.size());
    for (const QString& arg : args) {
        stdArgs.push_back(arg.toStdString());
    }

    m_hackTvLib->setArguments(stdArgs);
    m_hackTvLib->setLnaGain(40);
    m_hackTvLib->setVgaGain(20);
    m_hackTvLib->setRxAmpGain(14);
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

    m_hackTvLib->setReceivedDataCallback([this](const int8_t* data, size_t len) {
        if (!m_shuttingDown.load() && this && m_hackTvLib && data && len == 262144) {
            // Copy data to avoid dangling pointer
            QByteArray dataCopy(reinterpret_cast<const char*>(data), len);
            QMetaObject::invokeMethod(this, [this, dataCopy]() {
                    if (this && !m_shuttingDown.load()) {
                        handleReceivedData(reinterpret_cast<const int8_t*>(dataCopy.data()), dataCopy.size());
                    }
                }, Qt::QueuedConnection);
        }
    });
    m_hackRfRunning = false;
}

void MainWindow::handleReceivedData(const int8_t* data, size_t len)
{
    // Validate pointers and state
    if (m_shuttingDown.load() || !m_hackTvLib || !palFrameBuffer) {
        return;
    }

    // Validate data
    if (!data || len == 0) {
        return;
    }

    const int samples_count = len / 2;
    auto samplesPtr = std::make_shared<std::vector<std::complex<float>>>(samples_count);

#pragma omp parallel for
    for (int i = 0; i < samples_count; i++) {
        (*samplesPtr)[i] = std::complex<float>(
            static_cast<int8_t>(data[i * 2]) / 128.0f,
            static_cast<int8_t>(data[i * 2 + 1]) / 128.0f
            );
    }

    QFuture<void> demodSamples = QtConcurrent::run(m_threadPool, [this, samplesPtr]() {
        this->processDemod(*samplesPtr);
    });
}

void MainWindow::processDemod(const std::vector<std::complex<float>>& samples)
{
    if (!m_palDecoder || !m_audioDemodulator || !palFrameBuffer || !m_audioOutput) {
        return;
    }

    palFrameBuffer->addBuffer(samples);

    // VIDEO PROCESSING - tam frame (40ms = 1 PAL frame)
    if (palFrameBuffer->isFrameReady()) {
        int expectedVideo = 0;
        if (palDemodulationInProgress.testAndSetAcquire(expectedVideo, 1)) {
            auto fullFrame = palFrameBuffer->getFrame();
            if (!fullFrame.empty()) {
                auto framePtr = std::make_shared<std::vector<std::complex<float>>>(
                    std::move(fullFrame)
                    );
                startPalVideoProcessing(framePtr);
            } else {
                palDemodulationInProgress.storeRelease(0);
            }
        }
    }

    if (m_audioDemodulator && m_audioDemodulator->getAudioEnabled()) {
        // AUDIO PROCESSING - her 1/4 frame
        qsizetype quarterFrameSize = palFrameBuffer->targetSize() / 4;
        if (palFrameBuffer->size() >= quarterFrameSize) {
            int expectedAudio = 0;
            if (audioDemodulationInProgress.testAndSetAcquire(expectedAudio, 1)) {
                auto audioSamples = palFrameBuffer->getSamples(quarterFrameSize);
                if (!audioSamples.empty()) {
                    auto audioPtr = std::make_shared<std::vector<std::complex<float>>>(
                        std::move(audioSamples)
                        );
                    startPalAudioProcessing(audioPtr);
                } else {
                    audioDemodulationInProgress.storeRelease(0);
                }
            }
        }
    }
}

void MainWindow::startPalAudioProcessing(std::shared_ptr<std::vector<std::complex<float>>> audioPtr)
{
    QtConcurrent::run(QThreadPool::globalInstance(), [this, audioPtr]() {
        AtomicGuard guard(audioDemodulationInProgress);
        try {          
            if (m_audioDemodulator) {
                m_audioDemodulator->processSamples(*audioPtr);
                // auto audio = m_audioDemodulator->demodulateAudio(*audioPtr);
                // if (!audio.empty()) {
                //     m_audioOutput->enqueueAudio(std::move(audio));
                // }
            }
        }
        catch (const std::exception& e) {
            qCritical() << "PAL audio demodulation error:" << e.what();
        }
        catch (...) {
            qCritical() << "Unknown exception in PAL audio demodulation";
        }
    });
}

// mainwindow.cpp - startPalVideoProcessing metodunu debug ile güncelle

void MainWindow::startPalVideoProcessing(std::shared_ptr<std::vector<std::complex<float>>> framePtr)
{
    QtConcurrent::run(m_threadPool, [this, framePtr]() {
        AtomicGuard guard(palDemodulationInProgress);
        try {          
            if(m_palDecoder)
            {
                m_palDecoder->processSamples(*framePtr);
            }
        }
        catch (const std::exception& e) {
            qCritical() << "PAL video demodulation error:" << e.what();
        }
        catch (...) {
            qCritical() << "Unknown exception in PAL video demodulation";
        }
    });
}
