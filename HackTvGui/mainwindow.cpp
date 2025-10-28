#include "mainwindow.h"
#include <QApplication>
#include <QFuture>
#include <QLabel>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <memory>
#include "constants.h"
#include "palbdemodulator.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    m_hackTvLib(nullptr),
    palbDemodulator(nullptr),
    palFrameBuffer(nullptr),
    tvDisplay(nullptr),
    m_threadPool(nullptr),
    m_frequency(DEFAULT_FREQUENCY),
    m_sampleRate(DEFAULT_SAMPLE_RATE),
    m_volumeLevel(10),
    m_LowCutFreq(-1*int(DEFAULT_CUT_OFF)),
    m_HiCutFreq(DEFAULT_CUT_OFF),
    m_shuttingDown(false),
    m_isProcessing(false),   
    palDemodulationInProgress(0),
    audioDemodulationInProgress(0)
{
    QString homePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    m_sSettingsFile = homePath + "/hacktv_settings.ini";

    // Settings handling
    try {
        if (QFile::exists(m_sSettingsFile)) {
            qDebug() << "Settings file exists, loading settings";
            loadSettings();
        } else {
            qDebug() << "Settings file doesn't exist, saving default settings";
            saveSettings();
        }
    } catch (const std::exception& e) {
        qDebug() << "Exception caught:" << e.what();
    }

    tvDisplay = new TVDisplay(this);
    audioOutput = std::make_unique<AudioOutput>();

    // PAL-B/G System Constants
    const int PAL_TOTAL_LINES = 625;
    const int PAL_VISIBLE_LINES = 576;
    const int PAL_VBI_LINES_PER_FIELD = 25;
    const double PAL_LINE_DURATION = 64e-6;
    const double PAL_FRAME_DURATION = 0.05;  // 25 Hz

    // Initialize with correct parameters
    palbDemodulator = new PALBDemodulator(m_sampleRate);
    palFrameBuffer = new FrameBuffer(m_sampleRate, PAL_FRAME_DURATION); // 0.02s per field

    // If receiving from 478 MHz, set carrier offset
    // Assuming your SDR is tuned to 479.25 MHz (vision carrier)
    palbDemodulator->setVideoCarrier(0.0);  // Already at baseband
    palbDemodulator->setAudioCarrier(5.5e6); // Audio 5.5 MHz above video

    // PAL-B/G timing
    palbDemodulator->setPixelsPerLine(PAL_TOTAL_LINES);
    palbDemodulator->setVisibleLines(PAL_VISIBLE_LINES);
    palbDemodulator->setVBILines(PAL_VBI_LINES_PER_FIELD);
    palbDemodulator->setLineDuration(PAL_LINE_DURATION);

    // Horizontal timing (10.5 μs from sync start to active video)
    palbDemodulator->setHorizontalOffset(0.164); // (4.7+5.8)/64 ≈ 0.164

    // AM demodulation for analog TV
    palbDemodulator->setDemodMode(PALBDemodulator::DEMOD_AM);

    // Processing
    palbDemodulator->setDecimationFactor(2);
    palbDemodulator->setDeinterlace(false);

    // AGC for AM video
    palbDemodulator->setAGCAttack(0.001f);
    palbDemodulator->setAGCDecay(0.0001f);

    // Sync detection
    palbDemodulator->setVSyncThreshold(0.15f);

    // Video levels (adjust based on signal)
    palbDemodulator->setVideoBrightness(0.0f);
    palbDemodulator->setVideoContrast(1.0f);
    palbDemodulator->setVideoGamma(1.0f);
    palbDemodulator->setInvertVideo(true);

    m_threadPool = new QThreadPool(this);
    if (m_threadPool) {
        m_threadPool->setMaxThreadCount(QThread::idealThreadCount() / 2);
    }

    setupUi();

    logTimer = new QTimer(this);
    connect(logTimer, &QTimer::timeout, this, &MainWindow::updateLogDisplay);
    logTimer->start(500);

    QTimer::singleShot(500, this, [this]() {
        initializeHackTvLib();
    });
}

MainWindow::~MainWindow()
{
    // Prevent destructor from running during shutdown
    if (m_shuttingDown.load()) {
        return;
    }

    m_shuttingDown.store(true);

    try {
        qDebug() << "MainWindow destructor starting...";

        if (palFrameBuffer) {
            delete palFrameBuffer;
            palFrameBuffer = nullptr;
        }

        if (palbDemodulator) {
            delete palbDemodulator;
            palbDemodulator = nullptr;
        }

        // Stop timers first
        if (logTimer) {
            logTimer->stop();
        }

        // Stop processing
        m_isProcessing.store(false);

        // Wait briefly for threads
        if (m_threadPool) {
            m_threadPool->waitForDone(500);
        }

        // Clean up in reverse order of creation
        fmDemodulator.reset();
        rationalResampler.reset();
        lowPassFilter.reset();
        audioOutput.reset();
        m_hackTvLib.reset();

        // Thread pool last
        if (m_threadPool) {
            delete m_threadPool;
            m_threadPool = nullptr;
        }

        qDebug() << "MainWindow destructor completed";

    } catch (...) {
        // Don't throw from destructor
        qDebug() << "Exception in destructor - ignored";
    }
}

void MainWindow::setupUi()
{
    sliderStyle = "QSlider::groove:horizontal { "
                  "border: 1px solid #999999; "
                  "height: 8px; "
                  "background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #B1B1B1, stop:1 #c4c4c4); "
                  "margin: 2px 0; "
                  "} "
                  "QSlider::handle:horizontal { "
                  "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2980b9, stop:1 #3498db); "
                  "border: 1px solid #5c5c5c; "
                  "width: 18px; "
                  "margin: -2px 0; "
                  "border-radius: 3px; "
                  "}";

    labelStyle = "QLabel { background-color: #ad6d0a ; color: white; border-radius: 5px; font-weight: bold; padding: 2px; }";

    setMinimumSize(QSize(1024, 768));

    QWidget *centralWidget = new QWidget(this);
    mainLayout = new QVBoxLayout(centralWidget);

    addOutputGroup();
    addRxGroup();
    addModeGroup();
    addVideoControls();
    addinputTypeGroup();
    setCentralWidget(centralWidget);

    frequencyEdit->setText(QString::number(m_frequency));
    cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));
    cPlotter->setHiLowCutFrequencies(m_LowCutFreq, m_HiCutFreq);
    freqCtrl->setFrequency(m_frequency);

    // Connect signals and slots
    connect(executeButton, &QPushButton::clicked, this, &MainWindow::executeCommand);
    connect(chooseFileButton, &QPushButton::clicked, this, &MainWindow::chooseFile);
    connect(inputTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onInputTypeChanged);
    connect(rxtxCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onRxTxTypeChanged);
    connect(sampleRateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSampleRateChanged);

    //rxtxCombo->setCurrentIndex(0);
    onRxTxTypeChanged(0);
    setCurrentSampleRate(DEFAULT_SAMPLE_RATE);
}

void MainWindow::setCurrentSampleRate(int sampleRate)
{
    m_sampleRate = sampleRate;

    int index = sampleRateCombo->findData(sampleRate);
    if (index != -1) {
        sampleRateCombo->setCurrentIndex(index);
    } else {
        // If the exact sample rate is not found, find the closest one
        int closestIndex = 0;
        int smallestDiff = (std::numeric_limits<int>::max)();
        for (int i = 0; i < sampleRateCombo->count(); ++i) {
            int diff = std::abs(sampleRateCombo->itemData(i).toInt() - sampleRate);
            if (diff < smallestDiff) {
                smallestDiff = diff;
                closestIndex = i;
            }
        }
        sampleRateCombo->setCurrentIndex(closestIndex);
    }
}

void MainWindow::initializeHackTvLib() {
    try {
        qDebug() << "Creating HackTvLib instance...";

        // Create instance
        m_hackTvLib = std::make_unique<HackTvLib>();

        if (!m_hackTvLib) {
            qDebug() << "Failed to create HackTvLib instance";
            return;
        }

        qDebug() << "Setting up callbacks...";

        // Set callbacks with safety checks
        m_hackTvLib->setLogCallback([this](const std::string& msg) {
            // Add safety check for shutdown
            if (!m_shuttingDown.load()) {
                handleLog(msg);
            }
        });

        m_hackTvLib->setReceivedDataCallback([this](const int8_t* data, size_t len) {
            // Add safety checks
            if (!m_shuttingDown.load() && data && len > 0) {
                handleReceivedData(data, len);
            }
        });

        // Set mic disabled
        m_hackTvLib->setMicEnabled(false);

        // Set audio volume if available
        if (audioOutput) {
            audioOutput->setVolume(m_volumeLevel);
        }

        qDebug() << "HackTvLib initialized successfully";

        // DO NOT call start() here - let executeButton handle it

    } catch (const std::exception& e) {
        qDebug() << QString("HackTvLib initialization failed: %1").arg(e.what());
        m_hackTvLib.reset();  // Clean up on failure
    } catch (...) {
        qDebug() << "HackTvLib initialization failed with unknown exception";
        m_hackTvLib.reset();  // Clean up on failure
    }
}

void MainWindow::handleReceivedData(const int8_t *data, size_t len)
{
    if (!m_isProcessing.load() || !data || len != 262144 || !m_threadPool)
        return;

    const int samples_count = len / 2;

    // Create a shared pointer to a single vector
    auto samplesPtr = std::make_shared<std::vector<std::complex<float>>>(samples_count);

// Fill the vector with IQ samples
#pragma omp parallel for
    for (int i = 0; i < samples_count; i++) {
        (*samplesPtr)[i] = std::complex<float>(
            static_cast<int8_t>(data[i * 2]) / 128.0f,
            static_cast<int8_t>(data[i * 2 + 1]) / 128.0f
            );
    }

    // Process FFT and demodulation in parallel, sharing the same data
    QFuture<void> processFFT = QtConcurrent::run(m_threadPool, [this, samplesPtr]() {
        this->processFft(*samplesPtr);
    });

    QFuture<void> demodSamples = QtConcurrent::run(m_threadPool, [this, samplesPtr]() {
        this->processDemod(*samplesPtr);
    });
}

void MainWindow::processDemod(const std::vector<std::complex<float>>& samples)
{
    constexpr double FM_BAND_LOW = 87e6;
    constexpr double FM_BAND_HIGH = 108e6;
    constexpr double PAL_BAND_LOW = 45e6;
    constexpr double PAL_BAND_HIGH = 860e6;

    // ========================================================================
    // FM RADYO DEMODÜLASYONU
    // ========================================================================
    if (m_frequency >= FM_BAND_LOW && m_frequency <= FM_BAND_HIGH)
    {
        if (lowPassFilter && rationalResampler && fmDemodulator && audioOutput)
        {
            try {
                auto filteredSamples = lowPassFilter->apply(samples);
                auto resampledSamples = rationalResampler->resample(std::move(filteredSamples));
                auto demodulatedAudio = fmDemodulator->demodulate(std::move(resampledSamples));

                if (!demodulatedAudio.empty()) {
                    // Kazanç ve kesme (clipping)
                    for (auto& sample : demodulatedAudio) {
                        sample = std::clamp(sample * audioGain, -0.9f, 0.9f);
                    }
                    audioOutput->enqueueAudio(std::move(demodulatedAudio));
                }
            }
            catch (const std::exception& e) {
                qCritical() << "Exception in FM signal processing:" << e.what();
            }
        }
        return;
    }

    // ========================================================================
    // PAL-B TV DEMODÜLASYONU
    // ========================================================================
    if (m_frequency >= PAL_BAND_LOW && m_frequency <= PAL_BAND_HIGH)
    {
        if (!palbDemodulator || !palFrameBuffer || !audioOutput || samples.size() > 10000000) {
            if (samples.size() > 10000000) {
                qWarning() << "Sample buffer too large:" << samples.size() << "- skipping";
            }
            return;
        }

        palFrameBuffer->addBuffer(samples);

        // --- 1. SES İŞLEME ---
        qsizetype quarterFrameSize = palFrameBuffer->targetSize() / 4;

        if (palFrameBuffer->size() >= quarterFrameSize) {
            int expectedAudio = 0;
            // Ses işlemi meşgul değilse ve kilit alınabilirse...
            if (audioDemodulationInProgress.testAndSetAcquire(expectedAudio, 1)) {
                auto audioSamples = palFrameBuffer->getSamples(quarterFrameSize);

                if (!audioSamples.empty() && audioSamples.size() < 5000000) {
                    auto audioPtr = std::make_shared<std::vector<std::complex<float>>>(
                        std::move(audioSamples)
                        );
                    // Asenkron görevi başlat
                    startPalAudioProcessing(audioPtr);
                } else {
                    // İşlemeyeceksek kilidi hemen bırak
                    audioDemodulationInProgress.storeRelease(0);
                }
            }
        }

        // --- 2. VİDEO İŞLEME ---
        if (palFrameBuffer->isFrameReady()) {
            int expectedVideo = 0;
            // Video işlemi meşgul değilse ve kilit alınabilirse...
            if (palDemodulationInProgress.testAndSetAcquire(expectedVideo, 1)) {
                auto fullFrame = palFrameBuffer->getFrame();

                if (!fullFrame.empty() && fullFrame.size() < 10000000) {
                    auto framePtr = std::make_shared<std::vector<std::complex<float>>>(
                        std::move(fullFrame)
                        );
                    // Asenkron görevi başlat
                    startPalVideoProcessing(framePtr);
                } else {
                    // İşlemeyeceksek kilidi hemen bırak
                    palDemodulationInProgress.storeRelease(0);
                }
            }
        }
    }
}

// --- startPalAudioProcessing Uygulaması ---
void MainWindow::startPalAudioProcessing(std::shared_ptr<std::vector<std::complex<float>>> audioPtr)
{
    QtConcurrent::run(QThreadPool::globalInstance(), [this, audioPtr]() {
        AtomicGuard guard(audioDemodulationInProgress); // CRITICAL: Kilit ilk açılmalı

        try {
            if (audioPtr->size() > 5000000) {
                qCritical() << "Audio frame too large:" << audioPtr->size();
                return;
            }

            auto audio = palbDemodulator->demodulateAudioOnly(*audioPtr);

            if (!audio.empty() && audio.size() < 1000000) {
                const float gain = audioGain;
                for (auto& sample : audio) {
                    sample = std::clamp(sample * gain, -0.95f, 0.95f);
                }
                audioOutput->enqueueAudio(std::move(audio));
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

// --- startPalVideoProcessing Uygulaması ---
void MainWindow::startPalVideoProcessing(std::shared_ptr<std::vector<std::complex<float>>> framePtr)
{
    QtConcurrent::run(m_threadPool, [this, framePtr]() {
        AtomicGuard guard(palDemodulationInProgress); // CRITICAL: Kilit ilk açılmalı

        try {
            if (framePtr->size() > 10000000) {
                qCritical() << "Video frame too large:" << framePtr->size();
                return;
            }

            auto image = palbDemodulator->demodulateVideoOnly(*framePtr);

            if (!image.isNull()) {
                // UI Güncelleme ve ölçekleme mantığı
                QImage displayImage = image;
                if (image.width() > 1024 || image.height() > 768) {
                    displayImage = image.scaled(1024, 768, Qt::KeepAspectRatio, Qt::FastTransformation);
                }

                QMetaObject::invokeMethod(this, [this, img = std::move(displayImage)]() {
                        if (this) {
                            updateDisplay(img);
                        }
                    }, Qt::QueuedConnection);
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

void MainWindow::processFft(const std::vector<std::complex<float>>& samples)
{
    // Protect FFT processing with mutex to prevent concurrent access to shared resources
    static QMutex fftMutex;
    QMutexLocker locker(&fftMutex);

    int fft_size = 2048;
    std::vector<float> fft_output(fft_size);
    float signal_level_dbfs;

    // Process FFT calculations
    getFft(samples, fft_output, signal_level_dbfs, fft_size);

    // Use invokeMethod to update UI components from the main thread
    QMetaObject::invokeMethod(cMeter, "setLevel",
                              Qt::QueuedConnection,
                              Q_ARG(float, signal_level_dbfs));

    // Create a copy of the data for thread safety
    float* fft_data = new float[fft_size];
    std::memcpy(fft_data, fft_output.data(), fft_size * sizeof(float));

    // Update the plotter in the main thread
    QMetaObject::invokeMethod(this, "updatePlotter",
                              Qt::QueuedConnection,
                              Q_ARG(float*, fft_data),
                              Q_ARG(int, fft_size));
}

// Add this method to your MainWindow class
void MainWindow::updatePlotter(float* fft_data, int size)
{
    // This runs in the main thread
    cPlotter->setNewFttData(fft_data, fft_data, size);

    // Clean up the memory we allocated
    delete[] fft_data;
}

void MainWindow::addVideoControls()
{
    QGroupBox* videoGroup = new QGroupBox("Video Controls");
    QGridLayout* videoLayout = new QGridLayout(videoGroup);
    videoLayout->setSpacing(10);

    // Labels in first row
    QLabel* brightLabel = new QLabel("Brightness:");
    QLabel* contrastLabel = new QLabel("Contrast:");
    QLabel* gammaLabel = new QLabel("Gamma:");

    // Sliders in second row
    QSlider* brightSlider = new QSlider(Qt::Horizontal);
    brightSlider->setRange(-50, 50);
    brightSlider->setValue(20);
    brightSlider->setMinimumWidth(80);

    QSlider* contrastSlider = new QSlider(Qt::Horizontal);
    contrastSlider->setRange(50, 200);
    contrastSlider->setValue(130);
    contrastSlider->setMinimumWidth(80);

    QSlider* gammaSlider = new QSlider(Qt::Horizontal);
    gammaSlider->setRange(50, 150);
    gammaSlider->setValue(80);
    gammaSlider->setMinimumWidth(80);

    // Values in third row
    QLabel* brightValue = new QLabel("20");
    brightValue->setAlignment(Qt::AlignCenter);

    QLabel* contrastValue = new QLabel("1.3");
    contrastValue->setAlignment(Qt::AlignCenter);

    QLabel* gammaValue = new QLabel("0.8");
    gammaValue->setAlignment(Qt::AlignCenter);

    // Add to grid - 3x3 layout
    videoLayout->addWidget(brightLabel, 0, 0, Qt::AlignCenter);
    videoLayout->addWidget(contrastLabel, 0, 1, Qt::AlignCenter);
    videoLayout->addWidget(gammaLabel, 0, 2, Qt::AlignCenter);

    videoLayout->addWidget(brightSlider, 1, 0);
    videoLayout->addWidget(contrastSlider, 1, 1);
    videoLayout->addWidget(gammaSlider, 1, 2);

    videoLayout->addWidget(brightValue, 2, 0, Qt::AlignCenter);
    videoLayout->addWidget(contrastValue, 2, 1, Qt::AlignCenter);
    videoLayout->addWidget(gammaValue, 2, 2, Qt::AlignCenter);

    // Connect signals
    connect(brightSlider, &QSlider::valueChanged, [this, brightValue](int value) {
        m_videoBrightness = value / 100.0f;
        brightValue->setText(QString::number(value));
        if (palbDemodulator) {
            palbDemodulator->setVideoBrightness(m_videoBrightness);
        }
    });

    connect(contrastSlider, &QSlider::valueChanged, [this, contrastValue](int value) {
        m_videoContrast = value / 100.0f;
        contrastValue->setText(QString::number(m_videoContrast, 'f', 1));
        if (palbDemodulator) {
            palbDemodulator->setVideoContrast(m_videoContrast);
        }
    });

    connect(gammaSlider, &QSlider::valueChanged, [this, gammaValue](int value) {
        m_videoGamma = value / 100.0f;
        gammaValue->setText(QString::number(m_videoGamma, 'f', 2));
        if (palbDemodulator) {
            palbDemodulator->setVideoGamma(m_videoGamma);
        }
    });

    mainLayout->addWidget(videoGroup);
}

void MainWindow::addOutputGroup()
{
    // Output device group
    outputGroup = new QGroupBox("Output Device", this);
    QGridLayout *outputLayout = new QGridLayout(outputGroup);
    outputLayout->setVerticalSpacing(15);
    outputLayout->setHorizontalSpacing(15);

    QVector<QPair<QString, QString>> devices = {
                                                {"HackRF", "hackrf"},
                                                {"RtlSdr", "rtlsdr"},
                                                };

    QLabel *outputLabel = new QLabel("Device:", this);
    outputCombo = new QComboBox(this);
    for (const auto &device : devices) {
        outputCombo->addItem(device.first, device.second);
    }
    ampEnabled = new QCheckBox("Amp", this);
    ampEnabled->setChecked(true);
    colorDisabled = new QCheckBox("Disable colour", this);
    colorDisabled->setChecked(false);
    QLabel *freqLabel = new QLabel("Frequency (Hz):", this);
    frequencyEdit = new QLineEdit(this);
    frequencyEdit->setFixedWidth(150);
    QLabel *channelLabel = new QLabel("Channel:", this);
    channelCombo = new QComboBox(this);
    QLabel *sampleRateLabel = new QLabel("Sample Rate (MHz):", this);
    sampleRateCombo = new QComboBox(this);
    sampleRateCombo->setFixedWidth(100);

    std::map<int, QString> sortedSampleRates {
        {2000000, "2"},
        {4000000, "4"},
        {8000000, "8"},
        {10000000, "10"},
        {12500000, "12.5"},
        {16000000, "16"},
        {20000000, "20"}
    };
    for (const auto& [rate, displayText] : sortedSampleRates) {
        sampleRateCombo->addItem(displayText + " MHz", rate);
    }

    QLabel *rxtxLabel = new QLabel("RxTx Mode:", this);
    rxtxCombo = new QComboBox(this);
    rxtxCombo->addItem("RX", "rx");
    rxtxCombo->addItem("TX", "tx");

    outputLayout->addWidget(outputLabel, 0, 0);
    outputLayout->addWidget(outputCombo, 0, 1);
    outputLayout->addWidget(rxtxLabel, 0, 2);
    outputLayout->addWidget(rxtxCombo, 0, 3);
    outputLayout->addWidget(ampEnabled, 0, 4);
    outputLayout->addWidget(colorDisabled, 0, 5);

    outputLayout->addWidget(channelLabel, 1, 0);
    outputLayout->addWidget(channelCombo, 1, 1);
    outputLayout->addWidget(freqLabel, 1, 2);
    outputLayout->addWidget(frequencyEdit, 1, 3);
    outputLayout->addWidget(sampleRateLabel, 1, 4);
    outputLayout->addWidget(sampleRateCombo, 1, 5);

    txControlsLayout = new QGridLayout();

    // Amplitude
    txAmplitudeSlider = new QSlider(Qt::Horizontal);
    txAmplitudeSlider->setRange(0, 100);  // 0.0 to 1.0 in 100 steps
    txAmplitudeSlider->setValue(tx_amplitude*100);  // Default to 1.0
    txAmplitudeSpinBox = new QDoubleSpinBox();
    txAmplitudeSlider->setMinimumHeight(30);
    txAmplitudeSpinBox->setMinimumHeight(30);
    txAmplitudeSpinBox->setMinimumWidth(60);
    txAmplitudeSpinBox->setRange(0.0, 5.0);
    txAmplitudeSpinBox->setValue(tx_amplitude);
    txAmplitudeSpinBox->setSingleStep(0.01);
    QLabel *txAmplitudeLabel = new QLabel("Amplitude : ");
    txAmplitudeLabel->setStyleSheet(labelStyle);
    txControlsLayout->addWidget(txAmplitudeLabel, 0, 0);
    txControlsLayout->addWidget(txAmplitudeSlider, 0, 1);
    txControlsLayout->addWidget(txAmplitudeSpinBox, 0, 2);

    // Filter Size
    txFilterSizeSlider = new QSlider(Qt::Horizontal);
    txFilterSizeSlider->setRange(0, 500);  // 0.0 to 10.0 in 1000 steps
    txFilterSizeSlider->setValue(tx_filter_size*100);  // Default to 0.0
    txFilterSizeSpinBox = new QDoubleSpinBox();
    txFilterSizeSlider->setMinimumHeight(30);
    txFilterSizeSpinBox->setMinimumHeight(30);
    txFilterSizeSpinBox->setMinimumWidth(60);
    txFilterSizeSpinBox->setRange(0.0, 5.0);
    txFilterSizeSpinBox->setValue(tx_filter_size);
    txFilterSizeSpinBox->setSingleStep(0.01);
    QLabel *txFilterSizeLabel = new QLabel("Filter Size : ");
    txFilterSizeLabel->setStyleSheet(labelStyle);
    txControlsLayout->addWidget(txFilterSizeLabel, 1, 0);
    txControlsLayout->addWidget(txFilterSizeSlider, 1, 1);
    txControlsLayout->addWidget(txFilterSizeSpinBox, 1, 2);

    // Modulation Index
    txModulationIndexSlider = new QSlider(Qt::Horizontal);
    txModulationIndexSlider->setRange(0, 1000);  // 0.0 to 10.0 in 1000 steps
    txModulationIndexSlider->setValue(tx_modulation_index*100);  // Default to 5.0
    txModulationIndexSpinBox = new QDoubleSpinBox();
    txModulationIndexSlider->setMinimumHeight(30);
    txModulationIndexSpinBox->setMinimumHeight(30);
    txModulationIndexSpinBox->setMinimumWidth(60);
    txModulationIndexSpinBox->setRange(0.0, 10.0);
    txModulationIndexSpinBox->setValue(tx_modulation_index);
    txModulationIndexSpinBox->setSingleStep(0.01);
    QLabel *txModulationIndexLabel = new QLabel("Modulation Index : ");
    txModulationIndexLabel->setStyleSheet(labelStyle);
    txControlsLayout->addWidget(txModulationIndexLabel, 2, 0);
    txControlsLayout->addWidget(txModulationIndexSlider, 2, 1);
    txControlsLayout->addWidget(txModulationIndexSpinBox, 2, 2);

    // Interpolation
    txInterpolationSlider = new QSlider(Qt::Horizontal);
    txInterpolationSlider->setRange(0, 100);  // 0.0 to 100.0 in 100 steps
    txInterpolationSlider->setValue(tx_interpolation);  // Default to 48.0
    txInterpolationSpinBox = new QDoubleSpinBox();
    txInterpolationSlider->setMinimumHeight(30);
    txInterpolationSpinBox->setMinimumHeight(30);
    txInterpolationSpinBox->setMinimumWidth(60);
    txInterpolationSpinBox->setRange(0.0, 100.0);
    txInterpolationSpinBox->setValue(tx_interpolation);
    txInterpolationSpinBox->setSingleStep(1.0);
    QLabel *txInterpolationLabel = new QLabel("Interpolation : ");
    txInterpolationLabel->setStyleSheet(labelStyle);
    txControlsLayout->addWidget(txInterpolationLabel, 3, 0);
    txControlsLayout->addWidget(txInterpolationSlider, 3, 1);
    txControlsLayout->addWidget(txInterpolationSpinBox, 3, 2);

    txAmpSlider = new QSlider(Qt::Horizontal);
    txAmpSlider->setRange(0, HACKRF_TX_AMP_MAX_DB);  // 0.0 to 100.0 in 100 steps
    txAmpSlider->setValue(m_txAmpGain);  // Default to 48.0
    txAmpSpinBox = new QSpinBox();
    txAmpSlider->setMinimumHeight(30);
    txAmpSpinBox->setMinimumHeight(30);
    txAmpSpinBox->setMinimumWidth(60);
    txAmpSpinBox->setRange(0, HACKRF_TX_AMP_MAX_DB);
    txAmpSpinBox->setValue(m_txAmpGain);
    txAmpSpinBox->setSingleStep(1);
    QLabel *txAmpLabel = new QLabel("Tx Gain : ");
    txAmpLabel->setStyleSheet(labelStyle);
    txControlsLayout->addWidget(txAmpLabel, 4, 0);
    txControlsLayout->addWidget(txAmpSlider, 4, 1);
    txControlsLayout->addWidget(txAmpSpinBox, 4, 2);

    tx_line = new QFrame();
    tx_line->setFrameShape(QFrame::HLine);
    tx_line->setFrameShadow(QFrame::Sunken);
    outputLayout->addWidget(tx_line, 2, 0, 1, 6);
    outputLayout->addLayout(txControlsLayout, 3, 0, 1, 6);
    mainLayout->addWidget(outputGroup);

    connect(txAmpSlider, &QSlider::valueChanged, [this](int value) {
        this->txAmpSpinBox->setValue(value);
        m_txAmpGain = value;
        if(m_isProcessing)
        {
            m_hackTvLib->setTxAmpGain(m_txAmpGain);
        }
        saveSettings();
    });

    connect(txAmplitudeSlider, &QSlider::valueChanged, [this](int value) {
        float amplitude = value / 100.0f;
        this->txAmplitudeSpinBox->setValue(amplitude);
        tx_amplitude = amplitude;
        if(m_isProcessing)
        {
            m_hackTvLib->setAmplitude(tx_amplitude);
        }
        saveSettings();
    });
    connect(txAmplitudeSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double value) {
        txAmplitudeSlider->setValue(static_cast<int>(value * 100));
        tx_amplitude = value;
        if(m_isProcessing)
        {
            m_hackTvLib->setAmplitude(tx_amplitude);
        }
        saveSettings();
    });

    connect(txFilterSizeSlider, &QSlider::valueChanged, [this](int value) {
        float filterSize = value / 100.0f;
        txFilterSizeSpinBox->setValue(filterSize);
        tx_filter_size = filterSize;
        if(m_isProcessing)
        {
            m_hackTvLib->setFilter_size(tx_filter_size);
        }
        saveSettings();
    });

    connect(txFilterSizeSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double value) {
        txFilterSizeSlider->setValue(static_cast<int>(value * 100));
        tx_filter_size = value;
        if(m_isProcessing)
        {
            m_hackTvLib->setFilter_size(tx_filter_size);
        }
        saveSettings();
    });

    connect(txModulationIndexSlider, &QSlider::valueChanged, [this](int value) {
        float modulationIndex = value / 100.0f;
        txModulationIndexSpinBox->setValue(modulationIndex);
        tx_modulation_index = modulationIndex;
        if(m_isProcessing)
        {
            m_hackTvLib->setModulation_index(tx_modulation_index);
        }
        saveSettings();
    });
    connect(txModulationIndexSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double value) {
        txModulationIndexSlider->setValue(static_cast<int>(value * 100));
        tx_modulation_index = value;
        if(m_isProcessing)
        {
            m_hackTvLib->setModulation_index(tx_modulation_index);
        }
        saveSettings();
    });

    connect(txInterpolationSlider, &QSlider::valueChanged, [this](int value) {
        txInterpolationSpinBox->setValue(value);
        tx_interpolation = value;
        if(m_isProcessing)
        {
            m_hackTvLib->setInterpolation(tx_interpolation);
        }
        saveSettings();
    });
    connect(txInterpolationSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double value) {
        txInterpolationSlider->setValue(static_cast<int>(value));
        tx_interpolation = value;
        if(m_isProcessing)
        {
            m_hackTvLib->setInterpolation(tx_interpolation);
        }
        saveSettings();
    });

}

void MainWindow::addinputTypeGroup()
{
    // Input type group
    inputTypeGroup = new QGroupBox("Input Type", this);
    QVBoxLayout *inputTypeLayout = new QVBoxLayout(inputTypeGroup);
    inputTypeCombo = new QComboBox(this);
    inputTypeCombo->addItems({ "Fm Transmitter", "File", "Test", "Video Stream"});
    inputTypeLayout->addWidget(inputTypeCombo);

    // Input file group
    QWidget *inputFileWidget = new QWidget(this);
    QHBoxLayout *inputFileLayout = new QHBoxLayout(inputFileWidget);
    inputFileEdit = new QLineEdit(this);
    chooseFileButton = new QPushButton("Choose File", this);
    inputFileLayout->addWidget(inputFileEdit);
    inputFileLayout->addWidget(chooseFileButton);
    inputTypeLayout->addWidget(inputFileWidget);

    // FFmpeg options
    ffmpegOptionsEdit = new QLineEdit(this);
    ffmpegOptionsEdit->setText("rtsp://192.168.2.249:554/stream1");
    ffmpegOptionsEdit->setVisible(false);  // Initially hidden
    inputTypeLayout->addWidget(ffmpegOptionsEdit);

    mainLayout->addWidget(inputTypeGroup);
    mainLayout->addWidget(modeGroup);

    logBrowser = new QTextBrowser(this);

    QGroupBox *logGroup = new QGroupBox("Info", this);
    QHBoxLayout *logLayout = new QHBoxLayout(logGroup);
    logLayout->addWidget(logBrowser);    
    tvDisplay->setMinimumHeight(350);
    logLayout->addWidget(tvDisplay);
    logLayout->setStretchFactor(logBrowser, 1);  // Changed from 1 to 2
    logLayout->setStretchFactor(tvDisplay, 1);   // Changed from 2 to 1
    mainLayout->addWidget(logGroup);
    logGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    executeButton = new QPushButton("Start", this);
    exitButton = new QPushButton("Exit", this);
    connect(exitButton, &QPushButton::clicked, this, &MainWindow::exitApp);

    buttonLayout->addWidget(executeButton);
    buttonLayout->addWidget(exitButton);

    mainLayout->addLayout(buttonLayout);

    fileDialog = new QFileDialog(this);
    fileDialog->setFileMode(QFileDialog::ExistingFile);
    fileDialog->setNameFilter("Video Files (*.flv *.mp4);;All Files (*)");

    QString initialDir = QDir::homePath() + "/Desktop/Videos";
    if (!QDir(initialDir).exists()) {
        initialDir = QDir::homePath() + "/Videos";
    }
    fileDialog->setDirectory(initialDir);
}

void MainWindow::addModeGroup()
{
    modeGroup = new QGroupBox("Mode", this);
    QHBoxLayout *modeLayout = new QHBoxLayout(modeGroup);
    populateChannelCombo();

    modeCombo = new QComboBox(this);

    QVector<QPair<QString, QString>> modes = {
        {"PAL-I (625 lines, 25 fps/50 Hz, 6.0 MHz FM audio)", "i"},
        {"PAL-B/G (625 lines, 25 fps/50 Hz, 5.5 MHz FM audio)", "g"},
        {"PAL-D/K (625 lines, 25 fps/50 Hz, 6.5 MHz FM audio)", "pal-d"},
        {"PAL-FM (625 lines, 25 fps/50 Hz, 6.5 MHz FM audio)", "pal-fm"},
        {"PAL-N (625 lines, 25 fps/50 Hz, 4.5 MHz AM audio)", "pal-n"},
        {"PAL-M (525 lines, 30 fps/60 Hz, 4.5 MHz FM audio)", "pal-m"},
        {"SECAM-L (625 lines, 25 fps/50 Hz, 6.5 MHz AM audio)", "l"},
        {"SECAM-D/K (625 lines, 25 fps/50 Hz, 6.5 MHz FM audio)", "d"},
        {"NTSC-M (525 lines, 29.97 fps/59.94 Hz, 4.5 MHz FM audio)", "m"},
        {"NTSC-A (405 lines, 25 fps/50 Hz, -3.5 MHz AM audio)", "ntsc-a"},
        {"CCIR System A (405 lines, 25 fps/50 Hz, -3.5 MHz AM audio)", "a"}
    };

    for (const auto &mode : modes) {
        modeCombo->addItem(mode.first, mode.second);
    }

    // Set PAL-B/G as default
    int defaultIndex = modeCombo->findData("g");
    if (defaultIndex != -1) {
        modeCombo->setCurrentIndex(defaultIndex);
    }

    modeLayout->addWidget(modeCombo);

}

void MainWindow::addRxGroup()
{
    freqCtrl = new CFreqCtrl();
    freqCtrl->setup(0, 0, 6000e6, 1, FCTL_UNIT_MHZ);
    freqCtrl->setDigitColor(QColor("#FFC300"));
    freqCtrl->setFrequency(DEFAULT_FREQUENCY);
    connect(freqCtrl, &CFreqCtrl::newFrequency, this, &MainWindow::onFreqCtrl_setFrequency);
    freqCtrl->setMinimumHeight(40);

    cPlotter = new CPlotter(this);
    cPlotter->setTooltipsEnabled(true);

    cPlotter->setSampleRate(m_sampleRate);
    cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));
    cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));

    cPlotter->setFftRange(-140.0f, 20.0f);
    cPlotter->setPandapterRange(-140.f, 20.f);
    cPlotter->setDemodRanges(-1*DEFAULT_CUT_OFF, -_KHZ(5), _KHZ(5), DEFAULT_CUT_OFF, true);

    cPlotter->setFreqUnits(1000);
    cPlotter->setPercent2DScreen(50);
    cPlotter->setFilterBoxEnabled(true);
    cPlotter->setCenterLineEnabled(true);
    cPlotter->setClickResolution(1);

    cPlotter->setFftPlotColor(QColor("#CEECF5"));
    cPlotter->setFreqStep(_KHZ(5));

    //cPlotter->setPeakDetection(true ,2);
    cPlotter->setFftFill(true);
    cPlotter->setMinimumHeight(200);

    connect(cPlotter, &CPlotter::newDemodFreq, this, &MainWindow::on_plotter_newDemodFreq);
    connect(cPlotter, &CPlotter::newFilterFreq, this, &MainWindow::on_plotter_newFilterFreq);

    cMeter = new CMeter(this);
    cMeter->setMinimumHeight(50);

    rxGroup = new QGroupBox("Receiver", this);
    rxGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 2px solid #3498db; border-radius: 5px; margin-top: 1ex; } "
                           "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; padding: 0 10px; }");

    QVBoxLayout *rxLayout = new QVBoxLayout(rxGroup);
    rxLayout->setSpacing(15);
    rxLayout->setContentsMargins(15, 25, 15, 15);

    // Add cMeter and freqCtrl (assuming they should be at the top)
    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->addWidget(cMeter);
    topLayout->addWidget(freqCtrl);
    rxLayout->addLayout(topLayout);

    QVBoxLayout *midLayout = new QVBoxLayout();
    midLayout->addWidget(cPlotter);

    // Controls layout
    QHBoxLayout *controlsLayout = new QHBoxLayout();

    // Volume controls
    QVBoxLayout *volumeLayout = new QVBoxLayout();
    volumeLayout->setSpacing(5);

    volumeLabel = new QLabel("Volume:", rxGroup);
    volumeLabel->setStyleSheet("QLabel { color: white; font-weight: bold; }");

    volumeSlider = new QSlider(Qt::Horizontal, rxGroup);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(m_volumeLevel);
    volumeSlider->setTickPosition(QSlider::TicksBelow);
    volumeSlider->setTickInterval(1);

    volumeLevelLabel = new QLabel(QString::number(m_volumeLevel), rxGroup);
    volumeLevelLabel->setAlignment(Qt::AlignCenter);
    volumeLevelLabel->setMinimumWidth(40);

    volumeLayout->addWidget(volumeLabel);
    volumeLayout->addWidget(volumeSlider);
    volumeLayout->addWidget(volumeLevelLabel);

    // LNA Gain controls
    QVBoxLayout *lnaLayout = new QVBoxLayout();
    lnaLayout->setSpacing(5);

    lnaLabel = new QLabel("LNA Gain:", rxGroup);
    lnaLabel->setStyleSheet("QLabel { color: white; font-weight: bold; }");

    lnaSlider = new QSlider(Qt::Horizontal, rxGroup);
    lnaSlider->setRange(0, HACKRF_RX_LNA_MAX_DB);
    lnaSlider->setValue(m_lnaGain);  // Default value, adjust as needed
    lnaSlider->setTickPosition(QSlider::TicksBelow);
    lnaSlider->setTickInterval(1);

    lnaLevelLabel = new QLabel(QString::number(m_lnaGain), rxGroup);
    lnaLevelLabel->setAlignment(Qt::AlignCenter);
    lnaLevelLabel->setMinimumWidth(40);

    lnaLayout->addWidget(lnaLabel);
    lnaLayout->addWidget(lnaSlider);
    lnaLayout->addWidget(lnaLevelLabel);

    // VGA Gain controls
    QVBoxLayout *vgaLayout = new QVBoxLayout();
    vgaLayout->setSpacing(5);

    vgaLabel = new QLabel("VGA Gain:", rxGroup);
    vgaLabel->setStyleSheet("QLabel { color: white; font-weight: bold; }");

    vgaSlider = new QSlider(Qt::Horizontal, rxGroup);
    vgaSlider->setRange(0, HACKRF_RX_VGA_MAX_DB);
    vgaSlider->setValue(m_vgaGain);  // Default value, adjust as needed
    vgaSlider->setTickPosition(QSlider::TicksBelow);
    vgaSlider->setTickInterval(1);

    vgaLevelLabel = new QLabel(QString::number(m_vgaGain), rxGroup);
    vgaLevelLabel->setAlignment(Qt::AlignCenter);
    vgaLevelLabel->setMinimumWidth(40);

    vgaLayout->addWidget(vgaLabel);
    vgaLayout->addWidget(vgaSlider);
    vgaLayout->addWidget(vgaLevelLabel);

    // Rx Amp Gain controls
    QVBoxLayout *rxAmpLayout = new QVBoxLayout();
    rxAmpLayout->setSpacing(5);

    rxAmpLabel  = new QLabel("Amp Gain:", rxGroup);
    rxAmpLabel ->setStyleSheet("QLabel { color: white; font-weight: bold; }");

    rxAmpSlider  = new QSlider(Qt::Horizontal, rxGroup);
    rxAmpSlider ->setRange(0, HACKRF_RX_AMP_MAX_DB);
    rxAmpSlider ->setValue(m_rxAmpGain);  // Default value, adjust as needed
    rxAmpSlider ->setTickPosition(QSlider::TicksBelow);
    rxAmpSlider ->setTickInterval(1);

    rxAmpLevelLabel  = new QLabel(QString::number(m_rxAmpGain), rxGroup);
    rxAmpLevelLabel ->setAlignment(Qt::AlignCenter);
    rxAmpLevelLabel ->setMinimumWidth(40);

    rxAmpLayout->addWidget(rxAmpLabel);
    rxAmpLayout->addWidget(rxAmpSlider );
    rxAmpLayout->addWidget(rxAmpLevelLabel );
    rxAmpSlider->setStyleSheet(sliderStyle);

    // Add all controls to the main controls layout
    controlsLayout->addLayout(volumeLayout);
    controlsLayout->addLayout(lnaLayout);
    controlsLayout->addLayout(vgaLayout);
    controlsLayout->addLayout(rxAmpLayout);

    volumeSlider->setStyleSheet(sliderStyle);
    lnaSlider->setStyleSheet(sliderStyle);
    vgaSlider->setStyleSheet(sliderStyle);
    rxAmpSlider->setStyleSheet(sliderStyle);

    volumeLevelLabel->setStyleSheet(labelStyle);
    lnaLevelLabel->setStyleSheet(labelStyle);
    vgaLevelLabel->setStyleSheet(labelStyle);
    rxAmpLevelLabel->setStyleSheet(labelStyle);

    // Connect sliders to their respective slots
    connect(volumeSlider, &QSlider::valueChanged, this, &MainWindow::onVolumeSliderValueChanged);
    connect(lnaSlider, &QSlider::valueChanged, this, &MainWindow::onLnaSliderValueChanged);
    connect(vgaSlider, &QSlider::valueChanged, this, &MainWindow::onVgaSliderValueChanged);
    connect(rxAmpSlider, &QSlider::valueChanged, this, &MainWindow::onRxAmpSliderValueChanged);

    midLayout->addLayout(controlsLayout);

    rxLayout->addLayout(midLayout);
    mainLayout->addWidget(rxGroup);
}

void MainWindow::onVolumeSliderValueChanged(int value)
{
    if (audioOutput) {
        audioOutput->setVolume(value);
    } else {
        qDebug() << "audioOutput is null in onVolumeSliderValueChanged";
    }
    volumeLevelLabel->setText(QString::number(value));
    m_volumeLevel = value;
    saveSettings();
}

void MainWindow::onLnaSliderValueChanged(int value)
{
    lnaLevelLabel->setText(QString::number(value));
    m_lnaGain = value;
    if (m_isProcessing)
        m_hackTvLib->setLnaGain(m_lnaGain);

    saveSettings();
}

void MainWindow::onVgaSliderValueChanged(int value)
{
    vgaLevelLabel->setText(QString::number(value));
    m_vgaGain = value;
    if (m_isProcessing)
        m_hackTvLib->setVgaGain(m_vgaGain);

    saveSettings();
}

void MainWindow::onRxAmpSliderValueChanged(int value)
{
    rxAmpLevelLabel->setText(QString::number(value));
    m_rxAmpGain = value;
    if (m_isProcessing)
        m_hackTvLib->setRxAmpGain(m_rxAmpGain);

    saveSettings();
}

void MainWindow::saveSettings()
{
    QSettings settings(m_sSettingsFile, QSettings::IniFormat);
    settings.beginGroup("Rf");

    settings.setValue("frequency", m_frequency);
    settings.setValue("samplerate", m_sampleRate);
    settings.setValue("lowcutfreq", m_LowCutFreq);
    settings.setValue("hicutfreq", m_HiCutFreq);
    settings.setValue("tx_amplitude", tx_amplitude);
    settings.setValue("tx_filter_size", tx_filter_size);
    settings.setValue("tx_modulation_index", tx_modulation_index);
    settings.setValue("tx_interpolation", tx_interpolation);
    settings.setValue("m_volumeLevel", m_volumeLevel);
    settings.setValue("m_txAmpGain", m_txAmpGain);
    settings.setValue("m_rxAmpGain", m_rxAmpGain);
    settings.setValue("m_lnaGain", m_lnaGain);
    settings.setValue("m_vgaGain", m_vgaGain);
    settings.endGroup();
}

void MainWindow::loadSettings()
{
    QSettings settings(m_sSettingsFile, QSettings::IniFormat);
    settings.beginGroup("Rf");
    m_frequency = settings.value("frequency").toInt();
    m_sampleRate = settings.value("samplerate").toInt();
    m_LowCutFreq = settings.value("lowcutfreq").toInt();
    m_HiCutFreq = settings.value("hicutfreq").toInt();
    tx_amplitude = settings.value("tx_amplitude").toDouble();
    tx_filter_size = settings.value("tx_filter_size").toDouble();
    tx_modulation_index = settings.value("tx_modulation_index").toDouble();
    tx_interpolation = settings.value("tx_interpolation").toDouble();
    m_volumeLevel = settings.value("m_volumeLevel").toInt();
    m_txAmpGain = settings.value("m_txAmpGain").toInt();
    m_rxAmpGain = settings.value("m_rxAmpGain").toInt();
    m_lnaGain = settings.value("m_lnaGain").toInt();
    m_vgaGain = settings.value("m_vgaGain").toInt();
    settings.endGroup();
}

void MainWindow::handleSamples(const std::vector<std::complex<float>>& samples)
{
    // FIXED - Remove &m_threadPool parameter
    QFuture<void> fftFuture = QtConcurrent::run([this, samples]() {
        this->processFft(samples);
    });

    QFuture<void> demodFuture = QtConcurrent::run([this, samples]() {
        this->processDemod(samples);
    });

    fftFuture.waitForFinished();
    demodFuture.waitForFinished();
}

void MainWindow::updateDisplay(const QImage& image)
{
    // Thread-safe: QImage'i kopyala
    if (!image.isNull()) {
        QImage safeCopy = image.copy();
        tvDisplay->updateDisplay(safeCopy);
    }
}

void MainWindow::onFreqCtrl_setFrequency(qint64 freq)
{
    m_frequency = freq;
    cPlotter->setCenterFreq(static_cast<quint64>(freq));
    if (m_isProcessing)
        m_hackTvLib->setFrequency(m_frequency);
    frequencyEdit->setText(QString::number(m_frequency));
    saveSettings();
}

void MainWindow::on_plotter_newDemodFreq(qint64 freq, qint64 delta)
{
    m_frequency = freq;
    cPlotter->setCenterFreq(static_cast<quint64>(freq));
    if (m_isProcessing)
        m_hackTvLib->setFrequency(m_frequency);
    frequencyEdit->setText(QString::number(m_frequency));
    freqCtrl->setFrequency(m_frequency);
    saveSettings();
}

void MainWindow::on_plotter_newFilterFreq(int low, int high)
{
    m_LowCutFreq = low;
    m_HiCutFreq = high;
    if (m_isProcessing)
        lowPassFilter->designFilter(m_sampleRate, m_LowCutFreq, transitionWidth);
    saveSettings();
}

void MainWindow::executeCommand()
{
    if (palFrameBuffer) {
        palFrameBuffer->clear();
        qDebug() << "PAL FrameBuffer cleared on stop";
    }

    if (!m_hackTvLib) {
        qDebug() << "HackTvLib not initialized, trying to initialize...";
        initializeHackTvLib();
        if (!m_hackTvLib) {
            qDebug() << "Failed to initialize HackTvLib";
            return;
        }
    }

    if (executeButton->text() == "Start")
    {
        palDemodulationInProgress.storeRelease(0);

        QStringList args = buildCommand();

        if(mode == "rx")
        {
            lowPassFilter = std::make_unique<LowPassFilter>(m_sampleRate, m_CutFreq, transitionWidth);
            rationalResampler = std::make_unique<RationalResampler>(interpolation, decimation);
            fmDemodulator = std::make_unique<FMDemodulator>(quadratureRate, audioDecimation);
        }

        cPlotter->setSampleRate(m_sampleRate);
        cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));
        cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));

        // Convert QStringList to std::vector<std::string>
        std::vector<std::string> stdArgs;
        stdArgs.reserve(args.size());
        for (const QString& arg : args) {
            stdArgs.push_back(arg.toStdString());
        }

        try
        {
            m_hackTvLib->setArguments(stdArgs);
            m_hackTvLib->setAmplitude(tx_amplitude);
            if(m_hackTvLib->start()) {
                executeButton->setText("Stop");
                QString argsString = args.join(' ');
                logBrowser->append(argsString);
                m_isProcessing.store(true);
            } else {
                logBrowser->append("Failed to start HackTvLib.");
            }
        }
        catch (const std::exception& e) {
            QMessageBox::critical(this, "Error", QString("HackTvLib error: %1").arg(e.what()));
        }
    }
    else if (executeButton->text() == "Stop")
    {
        try
        {
            palDemodulationInProgress.storeRelease(0);

            m_isProcessing.store(false);

            if(m_hackTvLib->stop())
                executeButton->setText("Start");
            else
                logBrowser->append("Failed to stop HackTvLib.");
        }
        catch (const std::exception& e) {
            QMessageBox::critical(this, "Error", QString("HackTvLib error: %1").arg(e.what()));
        }
    }
}

QStringList MainWindow::buildCommand()
{
    QStringList args;

    auto output = outputCombo->currentData().toString();
    m_hackTvLib->setMicEnabled(false);

    mode = rxtxCombo->currentText().toLower();
    args << "--rx-tx-mode" << mode;

    args << "-o" << output;

    if (ampEnabled->isChecked()) {
        args << "-a" ;
    }

    if (colorDisabled->isChecked()) {
        args << "--nocolour" ;
    }

    args << "--repeat";
    args << "--a2stereo";
    args << "--filter";
    args << "--acp";

    switch(inputTypeCombo->currentIndex())
    {
    case 0: // fmtransmitter
        args << "fmtransmitter";
        if(mode == "tx")
        {
            m_hackTvLib->setMicEnabled(true);
            sampleRateCombo->setCurrentIndex(0);
        }
        break;
    case 1: // File
        if (!inputFileEdit->text().isEmpty()) {
            args << inputFileEdit->text();
        }
        break;
    case 2: // Test
        args << "test";
        break;
    case 3: // FFmpeg
    {
        QString ffmpegArg = "ffmpeg:";
        if (!ffmpegOptionsEdit->text().isEmpty()) {
            ffmpegArg += ffmpegOptionsEdit->text();
        }
        args << ffmpegArg;
        break;
    }
    default:
        args << "test";
        break;
    }

    m_sampleRate =  sampleRateCombo->currentData().toInt();
    m_frequency = frequencyEdit->text().toInt();

    auto sample_rate = QString::number(m_sampleRate);

    args << "-f" << frequencyEdit->text()
         << "-s" << sample_rate
         << "-m" << modeCombo->currentData().toString();

    return args;
}

void MainWindow::chooseFile()
{
    if (fileDialog->exec()) {
        QStringList selectedFiles = fileDialog->selectedFiles();
        if (!selectedFiles.isEmpty()) {
            inputFileEdit->setText(selectedFiles.first());
            qDebug() << inputFileEdit->text();
        }
    }
}

void MainWindow::handleLog(const std::string& logMessage)
{
    pendingLogs.append(QString::fromStdString(logMessage));
}

void MainWindow::updateLogDisplay()
{
    if (!pendingLogs.isEmpty()) {
        for (const QString& log : pendingLogs) {
            logBrowser->append(log);
        }
        pendingLogs.clear();
    }
}

void MainWindow::onInputTypeChanged(int index)
{
    if(m_isProcessing && m_hackTvLib->stop())
    {
        m_isProcessing.store(false);
        executeButton->setText("Start");
    }

    isFmTransmit = (index == 0);
    isFile = (index == 1);
    isTest = (index == 2);
    isFFmpeg = (index == 3);

    if(isFmTransmit)
        sampleRateCombo->setCurrentIndex(0);
    else
    {
        sampleRateCombo->setCurrentIndex(5);
        int defaultIndex = channelCombo->findText("E39"); // 615250000Hz
        if (defaultIndex != -1) {
            channelCombo->setCurrentIndex(defaultIndex);
        }
    }

    inputFileEdit->setVisible(isFile);
    chooseFileButton->setVisible(isFile);
    ffmpegOptionsEdit->setVisible(isFFmpeg);
    modeGroup->setVisible(isFile || isTest || isFFmpeg);

    txAmplitudeSlider->setVisible(isFmTransmit);
    txAmplitudeSpinBox->setVisible(isFmTransmit);
    txFilterSizeSlider->setVisible(isFmTransmit);
    txFilterSizeSpinBox->setVisible(isFmTransmit);
    txModulationIndexSlider->setVisible(isFmTransmit);
    txModulationIndexSpinBox->setVisible(isFmTransmit);
    txInterpolationSlider->setVisible(isFmTransmit);
    txInterpolationSpinBox->setVisible(isFmTransmit);
    txAmpSlider->setVisible(isFmTransmit);
    txAmpSpinBox->setVisible(isFmTransmit);
    tx_line->setVisible(isFmTransmit);

    // Also hide/show labels
    for (int i = 0; i < txControlsLayout->rowCount(); ++i) {
        QLayoutItem* item = txControlsLayout->itemAtPosition(i, 0);
        if (item && item->widget()) {
            item->widget()->setVisible(isFmTransmit);
        }
    }
}

void MainWindow::onRxTxTypeChanged(int index)
{
    if(m_isProcessing && m_hackTvLib->stop())
    {
        m_isProcessing.store(false);
        executeButton->setText("Start");
    }

    isTx = (index == 1);
    inputTypeGroup->setVisible(isTx);
    modeGroup->setVisible(isTx);
    rxGroup->setVisible(!isTx);

    if(isTx)
    {
        inputTypeCombo->setCurrentIndex(0);  // Start in Tx mode
        onInputTypeChanged(0);
    }

    txAmplitudeSlider->setVisible(isTx);
    txAmplitudeSpinBox->setVisible(isTx);
    txFilterSizeSlider->setVisible(isTx);
    txFilterSizeSpinBox->setVisible(isTx);
    txModulationIndexSlider->setVisible(isTx);
    txModulationIndexSpinBox->setVisible(isTx);
    txInterpolationSlider->setVisible(isTx);
    txInterpolationSpinBox->setVisible(isTx);
    txAmpSlider->setVisible(isTx);
    txAmpSpinBox->setVisible(isTx);
    tx_line->setVisible(isTx);

    // Also hide/show labels
    for (int i = 0; i < txControlsLayout->rowCount(); ++i) {
        QLayoutItem* item = txControlsLayout->itemAtPosition(i, 0);
        if (item && item->widget()) {
            item->widget()->setVisible(isTx);
        }
    }

    adjustSize();
    update();
}

void MainWindow::onSampleRateChanged(int index)
{
    m_sampleRate = sampleRateCombo->currentData().toInt();
    if(m_isProcessing && m_hackTvLib->stop())
    {
        m_isProcessing.store(false);
        executeButton->setText("Start");    lowPassFilter->designFilter(m_sampleRate, m_CutFreq, 10e3);
        cPlotter->setSampleRate(m_sampleRate);
        cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));
        cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));
        m_hackTvLib->setSampleRate(m_sampleRate);
        saveSettings();

        if (palbDemodulator) {
            palbDemodulator->setSampleRate(m_sampleRate);
        }

        // YENİ: FrameBuffer'ı güncelle
        if (palFrameBuffer) {
            palFrameBuffer->setSampleRate(m_sampleRate);
        }
    }
}

void MainWindow::populateChannelCombo()
{
    struct Channel {
        QString name;
        long long frequency;
    };

    QVector<Channel> channels = {
                                 {"E2", 48250000},
                                 {"E3", 55250000},
                                 {"E4", 62250000},
                                 {"E5", 175250000},
                                 {"E6", 182250000},
                                 {"E7", 189250000},
                                 {"E8", 196250000},
                                 {"E9", 203250000},
                                 {"E10", 210250000},
                                 {"E11", 217250000},
                                 {"E12", 224250000},
                                 {"E21", 471250000},
                                 {"E22", 479250000},
                                 {"E21", 471250000},
                                 {"E22", 479250000},
                                 {"E23", 487250000},
                                 {"E24", 495250000},
                                 {"E25", 503250000},
                                 {"E26", 511250000},
                                 {"E27", 519250000},
                                 {"E28", 527250000},
                                 {"E29", 535250000},
                                 {"E30", 543250000},
                                 {"E31", 551250000},
                                 {"E32", 559250000},
                                 {"E33", 567250000},
                                 {"E34", 575250000},
                                 {"E35", 583250000},
                                 {"E36", 591250000},
                                 {"E37", 599250000},
                                 {"E38", 607250000},
                                 {"E39", 615250000},
                                 {"E40", 623250000},
                                 {"E41", 631250000},
                                 {"E42", 639250000},
                                 {"E43", 647250000},
                                 {"E44", 655250000},
                                 {"E45", 663250000},
                                 {"E46", 671250000},
                                 {"E47", 679250000},
                                 {"E48", 687250000},
                                 {"E49", 695250000},
                                 {"E50", 703250000},
                                 {"E51", 711250000},
                                 {"E52", 719250000},
                                 {"E53", 727250000},
                                 {"E54", 735250000},
                                 {"E55", 743250000},
                                 {"E56", 751250000},
                                 {"E57", 759250000},
                                 {"E58", 767250000},
                                 {"E59", 775250000},
                                 {"E60", 783250000},
                                 {"E61", 791250000},
                                 {"E62", 799250000},
                                 {"E63", 807250000},
                                 {"E64", 815250000},
                                 {"E65", 823250000},
                                 {"E66", 831250000},
                                 {"E67", 839250000},
                                 {"E68", 847250000},
                                 {"E69", 855250000},
                                 };

    int indexToSelect = 0; // Default to the first item
    long long closestFrequency = std::abs(m_frequency - channels[0].frequency);

    for (int i = 0; i < channels.size(); ++i) {
        const auto &channel = channels[i];
        channelCombo->addItem(channel.name, channel.frequency);

        // Find the closest frequency
        long long diff = std::abs(m_frequency - channel.frequency);
        if (diff < closestFrequency) {
            closestFrequency = diff;
            indexToSelect = i;
        }
    }

    // Set the combo box to the closest frequency
    channelCombo->setCurrentIndex(indexToSelect);


    connect(channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onChannelChanged);
}

void MainWindow::onChannelChanged(int index)
{
    long long frequency = channelCombo->itemData(index).toLongLong();
    frequencyEdit->setText(QString::number(frequency));
    freqCtrl->setFrequency(frequency);
    m_frequency = frequency;
    if(m_isProcessing)
    {
        m_hackTvLib->setFrequency(m_frequency);
    }
    saveSettings();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    qDebug() << "Window close event received";

    // Call safe shutdown instead of default close
    event->ignore(); // Prevent default close

    // Do safe shutdown
    exitApp();
}

void MainWindow::exitApp()
{
    // Prevent multiple exit calls
    if (m_shuttingDown.exchange(true)) {
        return;
    }

    saveSettings();

    try {

        palDemodulationInProgress = 0;
        audioDemodulationInProgress = 0;

        // 1. Stop all timers first
        if (logTimer) {
            logTimer->stop();
        }

        // 2. Stop processing
        if (m_isProcessing.load()) {
            m_isProcessing.store(false);
        }

        // 3. Wait for threads to finish
        if (m_threadPool) {
            m_threadPool->waitForDone(500); // Reduced timeout
        }

        if (m_hackTvLib) {
            m_hackTvLib.reset();
        }

        audioOutput.reset();
        fmDemodulator.reset();
        rationalResampler.reset();
        lowPassFilter.reset();

        qDebug() << "Exiting...";

#ifdef Q_OS_WIN
        DWORD currentPID = GetCurrentProcessId();
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, currentPID);
        if (hProcess != NULL)
        {
            TerminateProcess(hProcess, 0);
            CloseHandle(hProcess);
        }
#else \ \
    // FIXED: Use Qt's proper shutdown instead of exit(0)
        QApplication::quit();
#endif

    } catch (const std::exception& e) {
        qDebug() << "Exception in exitApp:" << e.what();
#ifdef Q_OS_WIN
        // Force exit on Windows if there's an exception
        std::exit(1);
#else \ \
    // Use Qt quit on Linux even if there's an exception
        QApplication::quit();
#endif

    } catch (const std::exception& e) {
        qDebug() << "Exception during shutdown:" << e.what();
        std::exit(0);
    } catch (...) {
        qDebug() << "Unknown exception during shutdown";
        std::exit(0);
    }
}
