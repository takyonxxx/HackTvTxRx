#include "mainwindow.h"
#include <QApplication>
#include <QFuture>
#include <QLabel>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <memory>
#include "constants.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),   
    logBrowser(nullptr),
    m_hackTvLib(nullptr),
    m_threadPool(nullptr),
    m_frequency(DEFAULT_FREQUENCY),
    m_sampleRate(DEFAULT_SAMPLE_RATE),
    m_volumeLevel(10),
    m_LowCutFreq(-120000),
    m_HiCutFreq(120000),
    m_shuttingDown(false),
    m_isProcessing(false)
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


    logBrowser = new QTextBrowser(this);
    logBrowser->setVisible(false); // Log browser is hidden - logs go to qDebug
    audioOutput = std::make_unique<AudioOutput>();
    // Set audio volume if available
    if (audioOutput) {
        audioOutput->setVolume(m_volumeLevel);
    }

    m_threadPool = new QThreadPool(this);
    if (m_threadPool) {
        m_threadPool->setMaxThreadCount(QThread::idealThreadCount() / 2);
    }

    setupUi();

    rxtxCombo->setCurrentIndex(0);
    onRxTxTypeChanged(0);
    setCurrentSampleRate(DEFAULT_SAMPLE_RATE);

    logTimer = new QTimer(this);
    connect(logTimer, &QTimer::timeout, this, &MainWindow::updateLogDisplay);
    logTimer->start(500);

    qDebug() << "Sdr device initialized.";
    // HackTvLib is created on-demand when START is pressed (lazy init)
    // This avoids DLL/USB subsystem crashes at startup
}

MainWindow::~MainWindow()
{
    m_shuttingDown.store(true);
    m_isProcessing.store(false);

    if (m_hackTvLib) {
        m_hackTvLib->clearCallbacks();
        m_hackTvLib->stop();
        delete m_hackTvLib;
        m_hackTvLib = nullptr;
    }
}

void MainWindow::setupUi()
{
    sliderStyle = ""; // Use global stylesheet from main.cpp
    labelStyle = "QLabel { background-color: #ad6d0a ; color: white; border-radius: 3px; font-weight: bold; padding: 1px 4px; font-size: 11px; }";

    setWindowTitle("HackTvRxTx");

    QWidget *centralWidget = new QWidget(this);
    mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    addOutputGroup();
    addRxGroup();
    addModeGroup();
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

    // When device type changes, update gain controls and sample rate
    connect(outputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        QString device = outputCombo->currentData().toString();
        updateGainControlsForDevice(device);
        if (device == "rtlsdr") {
            sampleRateCombo->setCurrentIndex(0); // 2 MHz for RTL-SDR
        }
    });
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

void MainWindow::initializeHackTvLib()
{
    try {
        m_hackTvLib = new HackTvLib(this);

        if (!m_hackTvLib) {
            qDebug() << "Failed to create HackTvLib instance";
            // Retry after 3 seconds
            QTimer::singleShot(3000, this, [this]() {
                if (!m_hackTvLib && !m_shuttingDown.load()) {
                    qDebug() << "Retrying HackTvLib initialization...";
                    initializeHackTvLib();
                }
            });
            return;
        }

        qDebug() << "HackTvLib initialized successfully";
    } catch (const std::exception& e) {
        qDebug() << "Exception during HackTvLib init:" << e.what();
        m_hackTvLib = nullptr;
        // Retry after 3 seconds
        QTimer::singleShot(3000, this, [this]() {
            if (!m_hackTvLib && !m_shuttingDown.load()) {
                qDebug() << "Retrying HackTvLib initialization...";
                initializeHackTvLib();
            }
        });
    } catch (...) {
        qDebug() << "Unknown exception during HackTvLib init";
        m_hackTvLib = nullptr;
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

    // Frame-drop: skip if a plotter update is already queued on the main thread.
    // This prevents backlog buildup during resize or other main-thread blocking.
    if (m_fftUpdatePending.testAndSetAcquire(0, 1))
    {
        float* fft_data = new float[fft_size];
        std::memcpy(fft_data, fft_output.data(), fft_size * sizeof(float));

        QMetaObject::invokeMethod(this, "updatePlotter",
                                  Qt::QueuedConnection,
                                  Q_ARG(float*, fft_data),
                                  Q_ARG(int, fft_size));
    }
}

// Add this method to your MainWindow class
void MainWindow::updatePlotter(float* fft_data, int size)
{
    // Clear the pending flag so next FFT frame can be queued
    m_fftUpdatePending.storeRelease(0);

    // This runs in the main thread
    cPlotter->setNewFttData(fft_data, fft_data, size);

    // Clean up the memory we allocated
    delete[] fft_data;
}

void MainWindow::addOutputGroup()
{
    // Output device group - compact single-row layout
    outputGroup = new QGroupBox("Device Settings", this);
    QGridLayout *outputLayout = new QGridLayout(outputGroup);
    outputLayout->setVerticalSpacing(4);
    outputLayout->setHorizontalSpacing(8);
    outputLayout->setContentsMargins(8, 16, 8, 6);

    QVector<QPair<QString, QString>> devices = {
                                                {"HackRF", "hackrf"},
                                                {"RtlSdr", "rtlsdr"},
                                                };

    QLabel *outputLabel = new QLabel("Device:", this);
    outputCombo = new QComboBox(this);
    for (const auto &device : devices) {
        outputCombo->addItem(device.first, device.second);
    }
    outputCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QLabel *rxtxLabel = new QLabel("Mode:", this);
    rxtxCombo = new QComboBox(this);
    rxtxCombo->addItem("RX", "rx");
    rxtxCombo->addItem("TX", "tx");

    ampEnabled = new QCheckBox("Amp", this);
    ampEnabled->setChecked(true);
    colorDisabled = new QCheckBox("No Color", this);
    colorDisabled->setChecked(false);
    colorDisabled->setMinimumWidth(85);

    QLabel *channelLabel = new QLabel("Ch:", this);
    channelCombo = new QComboBox(this);
    channelCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QLabel *freqLabel = new QLabel("Freq (Hz):", this);
    frequencyEdit = new QLineEdit(this);
    frequencyEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QLabel *sampleRateLabel = new QLabel("SR:", this);
    sampleRateCombo = new QComboBox(this);

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

    // Row 0: Device | Mode | Amp | NoColor
    outputLayout->addWidget(outputLabel, 0, 0);
    outputLayout->addWidget(outputCombo, 0, 1);
    outputLayout->addWidget(rxtxLabel, 0, 2);
    outputLayout->addWidget(rxtxCombo, 0, 3);
    outputLayout->addWidget(ampEnabled, 0, 4);
    outputLayout->addWidget(colorDisabled, 0, 5, 1, 2); // span 2 columns

    // Row 1: Channel | Freq | SampleRate
    outputLayout->addWidget(channelLabel, 1, 0);
    outputLayout->addWidget(channelCombo, 1, 1);
    outputLayout->addWidget(freqLabel, 1, 2);
    outputLayout->addWidget(frequencyEdit, 1, 3, 1, 2);
    outputLayout->addWidget(sampleRateLabel, 1, 5);
    outputLayout->addWidget(sampleRateCombo, 1, 6);

    // Set stretch
    outputLayout->setColumnStretch(1, 3); // device/channel combo
    outputLayout->setColumnStretch(3, 3); // rxtx/freq
    outputLayout->setColumnStretch(4, 1); // freq extends
    outputLayout->setColumnStretch(6, 1); // sample rate combo
    int col = 7; // for TX controls row reference

    // TX Controls layout (hidden in RX mode)
    txControlsLayout = new QGridLayout();
    txControlsLayout->setSpacing(4);

    // Amplitude
    txAmplitudeSlider = new QSlider(Qt::Horizontal);
    txAmplitudeSlider->setRange(0, 100);
    txAmplitudeSlider->setValue(tx_amplitude*100);
    txAmplitudeSpinBox = new QDoubleSpinBox();
    txAmplitudeSpinBox->setMinimumWidth(55);
    txAmplitudeSpinBox->setRange(0.0, 5.0);
    txAmplitudeSpinBox->setValue(tx_amplitude);
    txAmplitudeSpinBox->setSingleStep(0.01);
    QLabel *txAmplitudeLabel = new QLabel("Amplitude:");
    txAmplitudeLabel->setStyleSheet(labelStyle);
    txControlsLayout->addWidget(txAmplitudeLabel, 0, 0);
    txControlsLayout->addWidget(txAmplitudeSlider, 0, 1);
    txControlsLayout->addWidget(txAmplitudeSpinBox, 0, 2);

    // Filter Size
    txFilterSizeSlider = new QSlider(Qt::Horizontal);
    txFilterSizeSlider->setRange(0, 500);
    txFilterSizeSlider->setValue(tx_filter_size*100);
    txFilterSizeSpinBox = new QDoubleSpinBox();
    txFilterSizeSpinBox->setMinimumWidth(55);
    txFilterSizeSpinBox->setRange(0.0, 5.0);
    txFilterSizeSpinBox->setValue(tx_filter_size);
    txFilterSizeSpinBox->setSingleStep(0.01);
    QLabel *txFilterSizeLabel = new QLabel("Filter:");
    txFilterSizeLabel->setStyleSheet(labelStyle);
    txControlsLayout->addWidget(txFilterSizeLabel, 0, 3);
    txControlsLayout->addWidget(txFilterSizeSlider, 0, 4);
    txControlsLayout->addWidget(txFilterSizeSpinBox, 0, 5);

    // Modulation Index
    txModulationIndexSlider = new QSlider(Qt::Horizontal);
    txModulationIndexSlider->setRange(0, 1000);
    txModulationIndexSlider->setValue(tx_modulation_index*100);
    txModulationIndexSpinBox = new QDoubleSpinBox();
    txModulationIndexSpinBox->setMinimumWidth(55);
    txModulationIndexSpinBox->setRange(0.0, 10.0);
    txModulationIndexSpinBox->setValue(tx_modulation_index);
    txModulationIndexSpinBox->setSingleStep(0.01);
    QLabel *txModulationIndexLabel = new QLabel("Mod Idx:");
    txModulationIndexLabel->setStyleSheet(labelStyle);
    txControlsLayout->addWidget(txModulationIndexLabel, 1, 0);
    txControlsLayout->addWidget(txModulationIndexSlider, 1, 1);
    txControlsLayout->addWidget(txModulationIndexSpinBox, 1, 2);

    // Interpolation
    txInterpolationSlider = new QSlider(Qt::Horizontal);
    txInterpolationSlider->setRange(0, 100);
    txInterpolationSlider->setValue(tx_interpolation);
    txInterpolationSpinBox = new QDoubleSpinBox();
    txInterpolationSpinBox->setMinimumWidth(55);
    txInterpolationSpinBox->setRange(0.0, 100.0);
    txInterpolationSpinBox->setValue(tx_interpolation);
    txInterpolationSpinBox->setSingleStep(1.0);
    QLabel *txInterpolationLabel = new QLabel("Interp:");
    txInterpolationLabel->setStyleSheet(labelStyle);
    txControlsLayout->addWidget(txInterpolationLabel, 1, 3);
    txControlsLayout->addWidget(txInterpolationSlider, 1, 4);
    txControlsLayout->addWidget(txInterpolationSpinBox, 1, 5);

    // Tx Gain
    txAmpSlider = new QSlider(Qt::Horizontal);
    txAmpSlider->setRange(0, HACKRF_TX_AMP_MAX_DB);
    txAmpSlider->setValue(m_txAmpGain);
    txAmpSpinBox = new QSpinBox();
    txAmpSpinBox->setMinimumWidth(55);
    txAmpSpinBox->setRange(0, HACKRF_TX_AMP_MAX_DB);
    txAmpSpinBox->setValue(m_txAmpGain);
    txAmpSpinBox->setSingleStep(1);
    QLabel *txAmpLabel = new QLabel("Tx Gain:");
    txAmpLabel->setStyleSheet(labelStyle);
    txControlsLayout->addWidget(txAmpLabel, 2, 0);
    txControlsLayout->addWidget(txAmpSlider, 2, 1);
    txControlsLayout->addWidget(txAmpSpinBox, 2, 2);

    // Stretch the slider columns
    txControlsLayout->setColumnStretch(1, 1);
    txControlsLayout->setColumnStretch(4, 1);

    tx_line = new QFrame();
    tx_line->setFrameShape(QFrame::HLine);
    tx_line->setFrameShadow(QFrame::Sunken);
    tx_line->setFixedHeight(2);
    outputLayout->addWidget(tx_line, 2, 0, 1, col);
    outputLayout->addLayout(txControlsLayout, 3, 0, 1, col);
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
    // Input type group - compact horizontal
    inputTypeGroup = new QGroupBox("Input Type", this);
    QHBoxLayout *inputTypeLayout = new QHBoxLayout(inputTypeGroup);
    inputTypeLayout->setSpacing(6);
    inputTypeLayout->setContentsMargins(8, 16, 8, 6);
    inputTypeCombo = new QComboBox(this);
    inputTypeCombo->addItems({ "Fm Transmitter Mic", "Fm Transmitter File", "Video File", "Video Test Signal", "Video Rtsp Stream"});
    inputTypeLayout->addWidget(inputTypeCombo);

    inputFileEdit = new QLineEdit(this);
    inputFileEdit->setPlaceholderText("Select file...");
    chooseFileButton = new QPushButton("Browse", this);
    inputTypeLayout->addWidget(inputFileEdit, 1);
    inputTypeLayout->addWidget(chooseFileButton);

    ffmpegOptionsEdit = new QLineEdit(this);
    ffmpegOptionsEdit->setText("rtsp://192.168.2.249:554/stream1");
    ffmpegOptionsEdit->setVisible(false);
    inputTypeLayout->addWidget(ffmpegOptionsEdit, 1);

    mainLayout->addWidget(inputTypeGroup);
    mainLayout->addWidget(modeGroup);

    // Bottom button bar
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(6);
    executeButton = new QPushButton("START", this);
    executeButton->setMinimumHeight(28);
    exitButton = new QPushButton("EXIT", this);
    exitButton->setMinimumHeight(28);
    connect(exitButton, &QPushButton::clicked, this, &MainWindow::exitApp);
    clearButton = new QPushButton("CLEAR", this);
    clearButton->setMinimumHeight(28);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::clear);
    hardResetButton = new QPushButton("HARD RESET", this);
    hardResetButton->setMinimumHeight(28);
    hardResetButton->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #7a1a1a, stop:1 #551010); "
        "color: #ffcccc; border: 1px solid #cc3333; border-radius: 4px; padding: 6px 15px; "
        "font-weight: bold; text-transform: uppercase; font-size: 11px; }"
        "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #992222, stop:1 #701515); "
        "border: 1px solid #ff4444; }"
        "QPushButton:pressed { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #551010, stop:1 #3a0a0a); }"
    );
    connect(hardResetButton, &QPushButton::clicked, this, &MainWindow::hardReset);

    buttonLayout->addWidget(executeButton, 2);
    buttonLayout->addWidget(hardResetButton, 1);
    buttonLayout->addWidget(clearButton, 1);
    buttonLayout->addWidget(exitButton, 1);

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
    modeLayout->setContentsMargins(8, 16, 8, 6);
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
    freqCtrl->setMinimumHeight(50);
    freqCtrl->setMaximumHeight(65);
    freqCtrl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    cPlotter = new CPlotter(this);
    cPlotter->setTooltipsEnabled(true);

    cPlotter->setSampleRate(m_sampleRate);
    cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));
    cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));

    cPlotter->setFftRange(-100.0f, 0.0f);
    cPlotter->setPandapterRange(-140.f, 20.f);
    cPlotter->setDemodRanges(-200000, -_KHZ(5), _KHZ(5), 200000, true);

    cPlotter->setFreqUnits(1000);
    cPlotter->setPercent2DScreen(50);
    cPlotter->setFilterBoxEnabled(true);
    cPlotter->setCenterLineEnabled(true);
    cPlotter->setClickResolution(1);

    cPlotter->setFftPlotColor(QColor("#CEECF5"));
    cPlotter->setFreqStep(_KHZ(5));

    cPlotter->setFftFill(true);
    cPlotter->setMinimumHeight(150);
    cPlotter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(cPlotter, &CPlotter::newDemodFreq, this, &MainWindow::on_plotter_newDemodFreq);
    connect(cPlotter, &CPlotter::newFilterFreq, this, &MainWindow::on_plotter_newFilterFreq);
    connect(cPlotter, &CPlotter::wheelFreqChange, this, [this](int direction) {
        qint64 step = freqCtrl->getActiveStep();
        m_frequency += direction * step;
        freqCtrl->setFrequency(m_frequency);
        cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));
        if (m_isProcessing && m_hackTvLib)
            m_hackTvLib->setFrequency(m_frequency);
        frequencyEdit->setText(QString::number(m_frequency));
        saveSettings();
    });

    cMeter = new CMeter(this);
    cMeter->setMinimumHeight(50);
    cMeter->setMaximumHeight(65);
    cMeter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    rxGroup = new QGroupBox("Receiver", this);
    rxGroup->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid #0096c8; border-radius: 5px; margin-top: 1ex; } "
                           "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; padding: 0 8px; color: #00ffcc; }");

    QVBoxLayout *rxLayout = new QVBoxLayout(rxGroup);
    rxLayout->setSpacing(4);
    rxLayout->setContentsMargins(6, 18, 6, 6);

    // Top bar: Meter (30%) | FreqCtrl (70%)
    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->setSpacing(4);
    topLayout->addWidget(cMeter, 3);
    topLayout->addWidget(freqCtrl, 7);
    rxLayout->addLayout(topLayout);

    // Plotter takes all available space
    rxLayout->addWidget(cPlotter, 1);

    // Compact horizontal slider controls: Label Slider Value | Label Slider Value | ...
    QGridLayout *controlsGrid = new QGridLayout();
    controlsGrid->setSpacing(4);
    controlsGrid->setContentsMargins(0, 2, 0, 0);

    // Volume
    volumeLabel = new QLabel("Vol:", rxGroup);
    volumeLabel->setStyleSheet("QLabel { color: #c8f0ff; font-size: 11px; font-weight: bold; }");
    volumeSlider = new QSlider(Qt::Horizontal, rxGroup);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(m_volumeLevel);
    volumeLevelLabel = new QLabel(QString::number(m_volumeLevel), rxGroup);
    volumeLevelLabel->setAlignment(Qt::AlignCenter);
    volumeLevelLabel->setFixedWidth(32);
    volumeLevelLabel->setStyleSheet(labelStyle);
    controlsGrid->addWidget(volumeLabel, 0, 0);
    controlsGrid->addWidget(volumeSlider, 0, 1);
    controlsGrid->addWidget(volumeLevelLabel, 0, 2);

    // LNA Gain
    lnaLabel = new QLabel("LNA:", rxGroup);
    lnaLabel->setStyleSheet("QLabel { color: #c8f0ff; font-size: 11px; font-weight: bold; }");
    lnaSlider = new QSlider(Qt::Horizontal, rxGroup);
    lnaSlider->setRange(0, HACKRF_RX_LNA_MAX_DB);
    lnaSlider->setValue(m_lnaGain);
    lnaLevelLabel = new QLabel(QString::number(m_lnaGain), rxGroup);
    lnaLevelLabel->setAlignment(Qt::AlignCenter);
    lnaLevelLabel->setFixedWidth(32);
    lnaLevelLabel->setStyleSheet(labelStyle);
    controlsGrid->addWidget(lnaLabel, 0, 3);
    controlsGrid->addWidget(lnaSlider, 0, 4);
    controlsGrid->addWidget(lnaLevelLabel, 0, 5);

    // VGA Gain
    vgaLabel = new QLabel("VGA:", rxGroup);
    vgaLabel->setStyleSheet("QLabel { color: #c8f0ff; font-size: 11px; font-weight: bold; }");
    vgaSlider = new QSlider(Qt::Horizontal, rxGroup);
    vgaSlider->setRange(0, HACKRF_RX_VGA_MAX_DB);
    vgaSlider->setValue(m_vgaGain);
    vgaLevelLabel = new QLabel(QString::number(m_vgaGain), rxGroup);
    vgaLevelLabel->setAlignment(Qt::AlignCenter);
    vgaLevelLabel->setFixedWidth(32);
    vgaLevelLabel->setStyleSheet(labelStyle);
    controlsGrid->addWidget(vgaLabel, 0, 6);
    controlsGrid->addWidget(vgaSlider, 0, 7);
    controlsGrid->addWidget(vgaLevelLabel, 0, 8);

    // Rx Amp Gain
    rxAmpLabel = new QLabel("Amp:", rxGroup);
    rxAmpLabel->setStyleSheet("QLabel { color: #c8f0ff; font-size: 11px; font-weight: bold; }");
    rxAmpSlider = new QSlider(Qt::Horizontal, rxGroup);
    rxAmpSlider->setRange(0, HACKRF_RX_AMP_MAX_DB);
    rxAmpSlider->setValue(m_rxAmpGain);
    rxAmpLevelLabel = new QLabel(QString::number(m_rxAmpGain), rxGroup);
    rxAmpLevelLabel->setAlignment(Qt::AlignCenter);
    rxAmpLevelLabel->setFixedWidth(32);
    rxAmpLevelLabel->setStyleSheet(labelStyle);
    controlsGrid->addWidget(rxAmpLabel, 0, 9);
    controlsGrid->addWidget(rxAmpSlider, 0, 10);
    controlsGrid->addWidget(rxAmpLevelLabel, 0, 11);

    // RTL-SDR specific gain controls (hidden by default, shown when RTL-SDR selected)
    rtlGainLabel = new QLabel("Gain:", rxGroup);
    rtlGainLabel->setStyleSheet("QLabel { color: #c8f0ff; font-size: 11px; font-weight: bold; }");
    rtlGainCombo = new QComboBox(rxGroup);
    rtlGainCombo->setMinimumWidth(80);
    // R820T typical gain values (dB)
    QVector<QPair<QString, int>> rtlGains = {
        {"0.0 dB", 0}, {"0.9 dB", 9}, {"1.4 dB", 14}, {"2.7 dB", 27},
        {"3.7 dB", 37}, {"7.7 dB", 77}, {"8.7 dB", 87}, {"12.5 dB", 125},
        {"14.4 dB", 144}, {"15.7 dB", 157}, {"16.6 dB", 166}, {"19.7 dB", 197},
        {"20.7 dB", 207}, {"22.9 dB", 229}, {"25.4 dB", 254}, {"28.0 dB", 280},
        {"29.7 dB", 297}, {"32.8 dB", 328}, {"33.8 dB", 338}, {"36.4 dB", 364},
        {"37.2 dB", 372}, {"38.6 dB", 386}, {"40.2 dB", 402}, {"42.1 dB", 421},
        {"43.4 dB", 434}, {"43.9 dB", 439}, {"44.5 dB", 445}, {"48.0 dB", 480},
        {"49.6 dB", 496}
    };
    for (const auto& g : rtlGains) {
        rtlGainCombo->addItem(g.first, g.second);
    }
    // Default to ~20 dB
    int defaultGainIdx = rtlGainCombo->findData(207);
    if (defaultGainIdx >= 0) rtlGainCombo->setCurrentIndex(defaultGainIdx);

    rtlAgcLabel = new QLabel("AGC:", rxGroup);
    rtlAgcLabel->setStyleSheet("QLabel { color: #c8f0ff; font-size: 11px; font-weight: bold; }");
    rtlAgcCheckBox = new QCheckBox("Auto", rxGroup);
    rtlAgcCheckBox->setChecked(false);

    controlsGrid->addWidget(rtlGainLabel, 1, 0);
    controlsGrid->addWidget(rtlGainCombo, 1, 1, 1, 2);
    controlsGrid->addWidget(rtlAgcLabel, 1, 3);
    controlsGrid->addWidget(rtlAgcCheckBox, 1, 4);

    // Connect RTL-SDR gain controls
    connect(rtlGainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        int gainValue = rtlGainCombo->currentData().toInt();
        if (m_isProcessing && m_hackTvLib) {
            // Pass gain value directly — hacktvlib routes to rtlSdrDevice->setGain()
            // We use LNA slider value as a carrier: encode RTL gain as-is
            // But LNA is unsigned int 0-40 range... use setRxAmpGain as proxy
            // Actually, map gainValue back to LNA range: gain/496*40
            unsigned int lnaEquiv = static_cast<unsigned int>(gainValue * 40.0f / 496.0f);
            m_hackTvLib->setLnaGain(lnaEquiv);
        }
        saveSettings();
    });

    connect(rtlAgcCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        rtlGainCombo->setEnabled(!checked);
        if (m_isProcessing && m_hackTvLib) {
            m_hackTvLib->setVgaGain(checked ? 0 : 1);
        }
        saveSettings();
    });

    // Initially hide RTL-SDR controls (shown when RTL-SDR selected)
    rtlGainLabel->setVisible(false);
    rtlGainCombo->setVisible(false);
    rtlAgcLabel->setVisible(false);
    rtlAgcCheckBox->setVisible(false);

    // Stretch the slider columns equally
    controlsGrid->setColumnStretch(1, 1);
    controlsGrid->setColumnStretch(4, 1);
    controlsGrid->setColumnStretch(7, 1);
    controlsGrid->setColumnStretch(10, 1);

    // Connect sliders
    connect(volumeSlider, &QSlider::valueChanged, this, &MainWindow::onVolumeSliderValueChanged);
    connect(lnaSlider, &QSlider::valueChanged, this, &MainWindow::onLnaSliderValueChanged);
    connect(vgaSlider, &QSlider::valueChanged, this, &MainWindow::onVgaSliderValueChanged);
    connect(rxAmpSlider, &QSlider::valueChanged, this, &MainWindow::onRxAmpSliderValueChanged);

    rxLayout->addLayout(controlsGrid);
    mainLayout->addWidget(rxGroup, 1); // stretch factor 1 so rxGroup expands
}

void MainWindow::updateGainControlsForDevice(const QString& device)
{
    bool isRtlSdr = (device == "rtlsdr");

    // HackRF controls
    lnaLabel->setVisible(!isRtlSdr);
    lnaSlider->setVisible(!isRtlSdr);
    lnaLevelLabel->setVisible(!isRtlSdr);
    vgaLabel->setVisible(!isRtlSdr);
    vgaSlider->setVisible(!isRtlSdr);
    vgaLevelLabel->setVisible(!isRtlSdr);
    rxAmpLabel->setVisible(!isRtlSdr);
    rxAmpSlider->setVisible(!isRtlSdr);
    rxAmpLevelLabel->setVisible(!isRtlSdr);

    // RTL-SDR: no gain controls — auto gain only
    rtlGainLabel->setVisible(false);
    rtlGainCombo->setVisible(false);
    rtlAgcLabel->setVisible(false);
    rtlAgcCheckBox->setVisible(false);
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

void MainWindow::clear()
{
    logBrowser->clear();
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
    m_CutFreq = std::abs(high);
    if (m_isProcessing && lowPassFilter)
        lowPassFilter->designFilter(m_sampleRate, m_CutFreq, 50000);
    saveSettings();
}

void MainWindow::executeCommand()
{

    if (executeButton->text() == "START")
    {
        // CRITICAL: frequencyEdit is the authoritative frequency source.
        // Sync m_frequency, freqCtrl and cPlotter from it before anything else.
        m_frequency = frequencyEdit->text().toLongLong();
        freqCtrl->setFrequency(m_frequency);

        // Hard reset: destroy existing instance and create fresh one
        // This ensures clean USB state even after previous crash
        if (m_hackTvLib) {
            qDebug() << "Resetting HackTvLib for clean start...";
            m_hackTvLib->clearCallbacks();
            m_hackTvLib->stop();
            delete m_hackTvLib;
            m_hackTvLib = nullptr;

            // Give USB subsystem time to fully release the device
            QThread::msleep(500);
        }

        qDebug() << "Creating HackTvLib...";
        initializeHackTvLib();
        if (!m_hackTvLib) {
            qDebug() << "ERROR: Failed to create HackTvLib!";
            return;
        }

        // Small delay after init to let USB device settle
        QThread::msleep(200);

        QStringList args = buildCommand();

        if(mode == "rx")
        {
            // WFM demodulation chain:
            // LowPassFilter: sampleRate → cutoff filter + internal decimation
            //   (2 MHz → decim=7 → ~286 kHz output)
            // RationalResampler: pass-through (1:1) — LPF already decimated
            // FMDemodulator: quadratureRate with internal audio decimation to ~48 kHz
            //
            // LowPassFilter.calculateDecimation gives:
            //   2 MHz → 7, 4 MHz → 14, 8 MHz → 28, 16 MHz → 56
            // So post-LPF rate ≈ sampleRate / decim ≈ 286 kHz

            int wfmCutoff = 120000; // 120 kHz for full WFM bandwidth
            double wfmTransition = 50000;

            // Calculate what LowPassFilter will decimate to
            int lpfDecim;
            if (m_sampleRate <= 2000000) lpfDecim = 7;
            else if (m_sampleRate <= 4000000) lpfDecim = 14;
            else if (m_sampleRate <= 8000000) lpfDecim = 28;
            else if (m_sampleRate <= 10000000) lpfDecim = 35;
            else if (m_sampleRate <= 12500000) lpfDecim = 44;
            else if (m_sampleRate <= 16000000) lpfDecim = 56;
            else lpfDecim = 70;

            double postLpfRate = static_cast<double>(m_sampleRate) / lpfDecim;

            // RationalResampler: 1:1 pass-through (no additional resampling needed)
            int rxInterpolation = 1;
            int rxDecimation = 1;

            // Audio decimation: target exactly 48 kHz for AudioOutput compatibility
            // Use round() instead of floor() to get closest match
            int rxAudioDecimation = std::max(1, static_cast<int>(std::round(postLpfRate / 48000.0)));
            double actualAudioRate = postLpfRate / rxAudioDecimation;

            qDebug() << "RX demod chain:"
                     << "sampleRate=" << m_sampleRate
                     << "lpfDecim=" << lpfDecim
                     << "postLpfRate=" << postLpfRate
                     << "audioDecim=" << rxAudioDecimation
                     << "audioRate=" << actualAudioRate;

            lowPassFilter = std::make_unique<LowPassFilter>(m_sampleRate, wfmCutoff, wfmTransition);
            rationalResampler = std::make_unique<RationalResampler>(rxInterpolation, rxDecimation);
            fmDemodulator = std::make_unique<FMDemodulator>(postLpfRate, rxAudioDecimation);
        }

        cPlotter->setSampleRate(m_sampleRate);
        cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));
        cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));

        std::vector<std::string> stdArgs;
        stdArgs.reserve(args.size());
        for (const QString& arg : args) {
            stdArgs.push_back(arg.toStdString());
        }

        m_hackTvLib->setArguments(stdArgs);
        m_hackTvLib->setAmplitude(tx_amplitude);

        // Thread-safe callbacks
        m_hackTvLib->setLogCallback([this](const std::string& msg) {
            if (!m_shuttingDown.load() && this && m_hackTvLib) {
                QMetaObject::invokeMethod(this, [this, msg]() {
                    if (this && !m_shuttingDown.load()) {
                        pendingLogs.append(QString::fromStdString(msg));
                    }
                }, Qt::QueuedConnection);
            }
        });

        m_hackTvLib->setReceivedDataCallback([this](const int8_t* data, size_t len) {
            if (!m_isProcessing.load() || !data || len != 262144 || !m_threadPool || m_shuttingDown.load())
                return;

            const int samples_count = len / 2;

            // Convert IQ data to complex samples (done here on the callback thread)
            auto samplesPtr = std::make_shared<std::vector<std::complex<float>>>(samples_count);
            for (int i = 0; i < samples_count; i++) {
                (*samplesPtr)[i] = std::complex<float>(
                    static_cast<int8_t>(data[i * 2]) / 128.0f,
                    static_cast<int8_t>(data[i * 2 + 1]) / 128.0f
                );
            }

            // Audio demod: dispatch directly to thread pool - does NOT go through main thread
            // This ensures audio is never blocked by plotter/resize on the main thread
            QtConcurrent::run(m_threadPool, [this, samplesPtr]() {
                this->processDemod(*samplesPtr);
            });

            // FFT: dispatch to thread pool (plotter update will be queued to main thread inside processFft)
            QtConcurrent::run(m_threadPool, [this, samplesPtr]() {
                this->processFft(*samplesPtr);
            });
        });

        if(!m_hackTvLib->start()) {
            logBrowser->append("Failed to start HackTvLib.");
            return;
        }

        executeButton->setText("STOP");
        QString argsString = args.join(' ');
        logBrowser->append(argsString);
        m_isProcessing.store(true);
    }
    else if (executeButton->text() == "STOP")
    {       
        m_isProcessing.store(false);

        if (m_hackTvLib) {
            m_hackTvLib->clearCallbacks();

            if(m_hackTvLib->stop())
                executeButton->setText("START");
            else
                logBrowser->append("Failed to stop HackTvLib.");
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
    case 0: // Fm Transmitter Mic
        args << "fmtransmitter";
        if(mode == "tx")
        {
            m_hackTvLib->setMicEnabled(true);
            sampleRateCombo->setCurrentIndex(0);
        }
        break;
    case 1: // Fm Transmitter File
        args << "fmtransmitter";
        if(mode == "tx" && !inputFileEdit->text().isEmpty())
        {
            m_hackTvLib->setMicEnabled(false);
            m_hackTvLib->setAudioFilePath(inputFileEdit->text().toStdString(), true);
            sampleRateCombo->setCurrentIndex(0);
        }
        break;
    case 2: // Video File
        if (!inputFileEdit->text().isEmpty()) {
            args << inputFileEdit->text();
        }
        break;
    case 3: // Video Test Signal
        args << "test";
        break;
    case 4: // Video Rtsp Stream
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
    m_frequency = frequencyEdit->text().toLongLong();

    auto sample_rate = QString::number(m_sampleRate);

    args << "-f" << frequencyEdit->text()
         << "-s" << sample_rate
         << "-m" << modeCombo->currentData().toString();

    return args;
}

void MainWindow::chooseFile()
{
    // Set file filter and directory based on current input type
    if (isFmFile) {
        fileDialog->setNameFilter("Audio Files (*.wav *.mp3 *.flac *.ogg *.aac *.wma *.m4a);;Video Files (*.mp4 *.mkv *.avi *.flv *.mov *.webm);;All Files (*)");
        QString musicDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (musicDir.isEmpty() || !QDir(musicDir).exists()) {
            musicDir = QDir::homePath() + "/Music";
        }
        fileDialog->setDirectory(musicDir);
    } else {
        fileDialog->setNameFilter("Video Files (*.flv *.mp4 *.mkv *.avi *.mov);;All Files (*)");
        QString videoDir = QDir::homePath() + "/Desktop/Videos";
        if (!QDir(videoDir).exists()) {
            videoDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        }
        fileDialog->setDirectory(videoDir);
    }

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
    // 0: Fm Transmitter Mic, 1: Fm Transmitter File, 2: Video File, 3: Video Test Signal, 4: Video Rtsp Stream
    isFmTransmit = (index == 0);
    isFmFile = (index == 1);
    isFile = (index == 2);
    isTest = (index == 3);
    isFFmpeg = (index == 4);

    bool isFmAny = isFmTransmit || isFmFile;

    if(isFmAny)
        sampleRateCombo->setCurrentIndex(0);
    else
    {
        sampleRateCombo->setCurrentIndex(5);
        int defaultIndex = channelCombo->findText("E39"); // 615250000Hz
        if (defaultIndex != -1) {
            channelCombo->setCurrentIndex(defaultIndex);
        }
    }

    inputFileEdit->setVisible(isFile || isFmFile);
    chooseFileButton->setVisible(isFile || isFmFile);
    ffmpegOptionsEdit->setVisible(isFFmpeg);
    modeGroup->setVisible(isFile || isTest || isFFmpeg);

    txAmplitudeSlider->setVisible(isFmAny);
    txAmplitudeSpinBox->setVisible(isFmAny);
    txFilterSizeSlider->setVisible(isFmAny);
    txFilterSizeSpinBox->setVisible(isFmAny);
    txModulationIndexSlider->setVisible(isFmAny);
    txModulationIndexSpinBox->setVisible(isFmAny);
    txInterpolationSlider->setVisible(isFmAny);
    txInterpolationSpinBox->setVisible(isFmAny);
    txAmpSlider->setVisible(isFmAny);
    txAmpSpinBox->setVisible(isFmAny);
    tx_line->setVisible(isFmAny);

    // Also hide/show labels (columns 0 and 3 in the 2-column grid)
    for (int i = 0; i < txControlsLayout->rowCount(); ++i) {
        for (int c : {0, 3}) {
            QLayoutItem* item = txControlsLayout->itemAtPosition(i, c);
            if (item && item->widget()) {
                item->widget()->setVisible(isFmAny);
            }
        }
    }
}

void MainWindow::onRxTxTypeChanged(int index)
{
    isTx = (index == 1);
    inputTypeGroup->setVisible(isTx);
    modeGroup->setVisible(isTx);
    rxGroup->setVisible(!isTx);
    colorDisabled->setVisible(isTx);

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

    // Also hide/show all TX control labels (in columns 0 and 3)
    for (int i = 0; i < txControlsLayout->rowCount(); ++i) {
        for (int c : {0, 3}) {
            QLayoutItem* item = txControlsLayout->itemAtPosition(i, c);
            if (item && item->widget()) {
                item->widget()->setVisible(isTx);
            }
        }
    }

    // No adjustSize() - window is fixed size (1024x740)
    update();
}

void MainWindow::onSampleRateChanged(int index)
{
    m_sampleRate = sampleRateCombo->currentData().toInt();
    if(m_isProcessing && m_hackTvLib->stop())
    {
        m_isProcessing.store(false);
        executeButton->setText("START");    lowPassFilter->designFilter(m_sampleRate, m_CutFreq, 10e3);
        cPlotter->setSampleRate(m_sampleRate);
        cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));
        cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));
        m_hackTvLib->setSampleRate(m_sampleRate);
        saveSettings();       
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

void MainWindow::hardReset()
{
    qDebug() << "=== USB HARD RESET initiated ===";

    // 1. Stop processing immediately
    m_isProcessing.store(false);

    // 2. If HackTvLib exists, use it for hard reset then destroy
    if (m_hackTvLib) {
        qDebug() << "Hard reset: clearing callbacks...";
        m_hackTvLib->clearCallbacks();

        qDebug() << "Hard reset: calling hackrf_reset() via HackTvLib...";
        int result = m_hackTvLib->hardReset();
        qDebug() << "Hard reset: hackrf_reset() result:" << result;

        qDebug() << "Hard reset: deleting HackTvLib...";
        delete m_hackTvLib;
        m_hackTvLib = nullptr;
    } else {
        // No HackTvLib instance - create temporary one just for reset
        qDebug() << "Hard reset: no active session, creating temporary HackTvLib for USB reset...";
        HackTvLib* tempLib = new HackTvLib(this);
        int result = tempLib->hardReset();
        qDebug() << "Hard reset: hackrf_reset() result:" << result;
        delete tempLib;
    }

    // 3. Wait for USB re-enumeration (device detaches and re-attaches)
    qDebug() << "Hard reset: waiting for USB re-enumeration (3 seconds)...";
    executeButton->setEnabled(false);
    hardResetButton->setEnabled(false);

    QTimer::singleShot(3000, this, [this]() {
        executeButton->setEnabled(true);
        hardResetButton->setEnabled(true);
        qDebug() << "=== USB HARD RESET complete - device should be re-enumerated ===";
    });

    // 4. Reset DSP chain
    lowPassFilter.reset();
    rationalResampler.reset();
    fmDemodulator.reset();

    // 5. Reset UI state
    executeButton->setText("START");

    // 6. Sync frequency from frequencyEdit (authoritative source)
    m_frequency = frequencyEdit->text().toLongLong();
    freqCtrl->setFrequency(m_frequency);
    cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));
    cPlotter->setSampleRate(m_sampleRate);
    cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));

    // 7. Clear FFT pending flag
    m_fftUpdatePending.storeRelease(0);

    // 8. Clear log
    logBrowser->clear();
    pendingLogs.clear();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    event->ignore(); // Prevent default close
    exitApp();
}

void MainWindow::exitApp()
{
    m_shuttingDown.store(true);
    m_isProcessing.store(false);

    if (m_hackTvLib) {
        m_hackTvLib->clearCallbacks();
        m_hackTvLib->stop();
        delete m_hackTvLib;
        m_hackTvLib = nullptr;
        QThread::msleep(300); // Let USB release
    }

    try {
#ifdef Q_OS_WIN
        DWORD currentPID = GetCurrentProcessId();
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, currentPID);
        if (hProcess != NULL)
        {
            TerminateProcess(hProcess, 0);
            CloseHandle(hProcess);
        }
#else \
    // FIXED: Use Qt's proper shutdown instead of exit(0)
        QApplication::quit();
#endif

    } catch (const std::exception& e) {
        qDebug() << "Exception in exitApp:" << e.what();
#ifdef Q_OS_WIN
        // Force exit on Windows if there's an exception
        std::exit(1);
#else \
    // Use Qt quit on Linux even if there's an exception
        QApplication::quit();
#endif
    } catch (...) {
        qDebug() << "Unknown exception during shutdown";
        std::exit(0);
    }
}
