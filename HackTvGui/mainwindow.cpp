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

    // Apply loaded settings to UI widgets
    // (loadSettings already set member variables, now sync UI)
    if (m_sampleRate > 0) {
        setCurrentSampleRate(m_sampleRate);
    } else {
        setCurrentSampleRate(DEFAULT_SAMPLE_RATE);
    }
    if (m_frequency > 0) {
        freqCtrl->setFrequency(m_frequency);
        cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));
    }
    if (volumeSlider) {
        volumeSlider->setValue(m_volumeLevel);
        volumeLevelLabel->setText(QString::number(m_volumeLevel));
    }
    if (lnaSlider) {
        lnaSlider->setValue(m_lnaGain);
        lnaLevelLabel->setText(QString::number(m_lnaGain));
    }
    if (vgaSlider) {
        vgaSlider->setValue(m_vgaGain);
        vgaLevelLabel->setText(QString::number(m_vgaGain));
    }
    if (rxAmpSlider) {
        rxAmpSlider->setValue(m_rxAmpGain);
        rxAmpLevelLabel->setText(QString::number(m_rxAmpGain));
    }
    if (txAmpSlider) {
        txAmpSlider->setValue(m_txAmpGain);
        txAmpSpinBox->setValue(m_txAmpGain);
    }
    if (txAmplitudeSlider) {
        txAmplitudeSlider->setValue(static_cast<int>(tx_amplitude * 100));
        txAmplitudeSpinBox->setValue(tx_amplitude);
    }
    if (txModulationIndexSlider) {
        txModulationIndexSlider->setValue(static_cast<int>(tx_modulation_index * 100));
        txModulationIndexSpinBox->setValue(tx_modulation_index);
    }
    if (txAmpSlider) {
        txAmpSlider->setValue(m_txAmpGain);
        txAmpSpinBox->setValue(m_txAmpGain);
    }

    // Restore RX demod sliders from loaded settings
    if (rxGainSlider) {
        rxGainSlider->setValue(static_cast<int>(rxGain * 10));
        rxGainLevelLabel->setText(QString::number(rxGain, 'f', 1));
    }
    if (rxModIndexSlider) {
        rxModIndexSlider->setValue(static_cast<int>(rxModIndex * 10));
        rxModIndexLevelLabel->setText(QString::number(rxModIndex, 'f', 1));
    }
    if (rxDeemphSlider) {
        rxDeemphSlider->setValue(rxDeemph);
        rxDeemphLevelLabel->setText(rxDeemph == 0 ? "OFF" : QString("%1us").arg(rxDeemph));
    }

    // Restore checkboxes from settings
    {
        QSettings settings(m_sSettingsFile, QSettings::IniFormat);
        settings.beginGroup("Rf");
        bool amp = settings.value("ampEnabled", true).toBool();
        bool noColor = settings.value("colorDisabled", false).toBool();
        settings.endGroup();
        if (ampEnabled) ampEnabled->setChecked(amp);
        if (colorDisabled) colorDisabled->setChecked(noColor);
    }

    // Connect checkboxes AFTER restore to avoid triggering saveSettings during init
    connect(ampEnabled, &QCheckBox::stateChanged, this, [this]() {
        if (m_isProcessing && m_hackTvLib) {
            m_hackTvLib->setAmpEnable(ampEnabled->isChecked());
        }
        saveSettings();
    });
    connect(colorDisabled, &QCheckBox::stateChanged, this, [this]() {
        // No Color requires restart - just save for next start
        saveSettings();
    });

    logTimer = new QTimer(this);
    connect(logTimer, &QTimer::timeout, this, &MainWindow::updateLogDisplay);
    logTimer->start(500);

    qDebug() << "Sdr device initialized.";
    // HackTvLib is created on-demand when START is pressed (lazy init)
    // This avoids DLL/USB subsystem crashes at startup
    m_initDone = true;
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
    labelStyle = "QLabel { background-color: #ad6d0a; color: #fff8ee; border-radius: 3px; font-weight: bold; padding: 2px 6px; font-size: 11px; }";

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
    if (wbfmDemodulator && audioOutput)
    {
        try {
            auto demodulatedAudio = wbfmDemodulator->demodulate(samples);

            if (!demodulatedAudio.empty()) {
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
    outputGroup->setStyleSheet(
        "QGroupBox { border: 1px solid #0096c8; border-radius: 6px; margin-top: 1.2em; "
        "padding-top: 1em; background-color: rgba(0, 40, 60, 30); }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 8px; "
        "color: #00ffcc; font-weight: bold; text-transform: uppercase; font-size: 11px; }"
    );

    QGridLayout *outputLayout = new QGridLayout(outputGroup);
    outputLayout->setVerticalSpacing(8);
    outputLayout->setHorizontalSpacing(10);
    outputLayout->setContentsMargins(14, 24, 14, 10);

    QString settingsLabelStyle = "QLabel { color: #90c8e0; font-size: 11px; font-weight: bold; }";

    QVector<QPair<QString, QString>> devices = {
                                                {"HackRF", "hackrf"},
                                                {"RtlSdr", "rtlsdr"},
                                                };

    QLabel *outputLabel = new QLabel("Device:", this);
    outputLabel->setStyleSheet(settingsLabelStyle);
    outputCombo = new QComboBox(this);
    for (const auto &device : devices) {
        outputCombo->addItem(device.first, device.second);
    }
    outputCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QLabel *rxtxLabel = new QLabel("Mode:", this);
    rxtxLabel->setStyleSheet(settingsLabelStyle);
    rxtxCombo = new QComboBox(this);
    rxtxCombo->addItem("RX", "rx");
    rxtxCombo->addItem("TX", "tx");

    QLabel *sampleRateLabel = new QLabel("BW:", this);
    sampleRateLabel->setStyleSheet(settingsLabelStyle);
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

    ampEnabled = new QCheckBox("Amp", this);
    ampEnabled->setMinimumWidth(65);
    colorDisabled = new QCheckBox("No Color", this);
    colorDisabled->setMinimumWidth(95);

    channelLabel = new QLabel("Ch:", this);
    channelLabel->setStyleSheet(settingsLabelStyle);
    channelCombo = new QComboBox(this);
    channelCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // frequencyEdit removed — m_frequency is the authoritative source

    // Single row: Device [___combo___] | Mode [_combo_] | BW [_combo_] | Amp | NoColor | Ch [_combo_]
    outputLayout->addWidget(outputLabel,     0, 0);
    outputLayout->addWidget(outputCombo,     0, 1);
    outputLayout->addWidget(rxtxLabel,       0, 2);
    outputLayout->addWidget(rxtxCombo,       0, 3);
    outputLayout->addWidget(sampleRateLabel, 0, 4);
    outputLayout->addWidget(sampleRateCombo, 0, 5);
    outputLayout->addWidget(ampEnabled,      0, 6);
    outputLayout->addWidget(colorDisabled,   0, 7);
    outputLayout->addWidget(channelLabel,    0, 8);
    outputLayout->addWidget(channelCombo,    0, 9);

    // Column stretches
    outputLayout->setColumnStretch(0, 0);  // Device label
    outputLayout->setColumnStretch(1, 4);  // Device combo - widest
    outputLayout->setColumnStretch(2, 0);  // Mode label
    outputLayout->setColumnStretch(3, 2);  // Mode combo
    outputLayout->setColumnStretch(4, 0);  // BW label
    outputLayout->setColumnStretch(5, 2);  // BW combo
    outputLayout->setColumnStretch(6, 0);  // Amp
    outputLayout->setColumnStretch(7, 0);  // NoColor
    outputLayout->setColumnStretch(8, 0);  // Ch label
    outputLayout->setColumnStretch(9, 2);  // Ch combo

    int col = 10; // total columns for TX controls row span

    // TX Controls layout (hidden in RX mode)
    txControlsLayout = new QGridLayout();
    txControlsLayout->setSpacing(6);
    txControlsLayout->setContentsMargins(0, 4, 0, 0);

    // TX Amplitude (same as HackRfRadio: range 1-100, /100 = 0.01-1.00, default 0.50)
    txAmplitudeSlider = new QSlider(Qt::Horizontal);
    txAmplitudeSlider->setRange(1, 100);
    txAmplitudeSlider->setValue(static_cast<int>(tx_amplitude * 100));
    txAmplitudeSpinBox = new QDoubleSpinBox();
    txAmplitudeSpinBox->setMinimumWidth(60);
    txAmplitudeSpinBox->setRange(0.01, 1.0);
    txAmplitudeSpinBox->setValue(tx_amplitude);
    txAmplitudeSpinBox->setSingleStep(0.01);
    QLabel *txAmplitudeLabel = new QLabel("TX Amp:");
    txAmplitudeLabel->setStyleSheet(labelStyle);
    txControlsLayout->addWidget(txAmplitudeLabel, 0, 0);
    txControlsLayout->addWidget(txAmplitudeSlider, 0, 1);
    txControlsLayout->addWidget(txAmplitudeSpinBox, 0, 2);

    // TX Modulation Index (same as HackRfRadio: range 1-500, /100 = 0.01-5.00, default 0.40)
    txModulationIndexSlider = new QSlider(Qt::Horizontal);
    txModulationIndexSlider->setRange(1, 500);
    txModulationIndexSlider->setValue(static_cast<int>(tx_modulation_index * 100));
    txModulationIndexSpinBox = new QDoubleSpinBox();
    txModulationIndexSpinBox->setMinimumWidth(60);
    txModulationIndexSpinBox->setRange(0.01, 5.0);
    txModulationIndexSpinBox->setValue(tx_modulation_index);
    txModulationIndexSpinBox->setSingleStep(0.01);
    QLabel *txModulationIndexLabel = new QLabel("Mod Idx:");
    txModulationIndexLabel->setStyleSheet(labelStyle);
    txControlsLayout->addWidget(txModulationIndexLabel, 0, 3);
    txControlsLayout->addWidget(txModulationIndexSlider, 0, 4);
    txControlsLayout->addWidget(txModulationIndexSpinBox, 0, 5);

    // Tx IF Gain (0-47, default 47)
    txAmpSlider = new QSlider(Qt::Horizontal);
    txAmpSlider->setRange(0, 47);
    txAmpSlider->setValue(m_txAmpGain);
    txAmpSpinBox = new QSpinBox();
    txAmpSpinBox->setMinimumWidth(60);
    txAmpSpinBox->setRange(0, 47);
    txAmpSpinBox->setValue(m_txAmpGain);
    txAmpSpinBox->setSingleStep(1);
    QLabel *txAmpLabel = new QLabel("TX Power:");
    txAmpLabel->setStyleSheet(labelStyle);
    txControlsLayout->addWidget(txAmpLabel, 1, 0);
    txControlsLayout->addWidget(txAmpSlider, 1, 1);
    txControlsLayout->addWidget(txAmpSpinBox, 1, 2);

    // Hidden placeholders for Filter/Interp (kept for Video TX compatibility but never shown in FM TX)
    txFilterSizeSlider = new QSlider(Qt::Horizontal); txFilterSizeSlider->setVisible(false);
    txFilterSizeSpinBox = new QDoubleSpinBox(); txFilterSizeSpinBox->setVisible(false);
    txInterpolationSlider = new QSlider(Qt::Horizontal); txInterpolationSlider->setVisible(false);
    txInterpolationSpinBox = new QDoubleSpinBox(); txInterpolationSpinBox->setVisible(false);

    // Stretch the slider columns
    txControlsLayout->setColumnStretch(1, 1);
    txControlsLayout->setColumnStretch(4, 1);

    tx_line = new QFrame();
    tx_line->setFrameShape(QFrame::HLine);
    tx_line->setFrameShadow(QFrame::Sunken);
    tx_line->setStyleSheet("QFrame { color: #0078a0; }");
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

    cPlotter->setFftRange(-110.0f, 0.0f);
    cPlotter->setPandapterRange(-110.f, 0.f);
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

    // Vertical gain slider (dB range control) next to plotter
    QSlider *plotterGainSlider = new QSlider(Qt::Vertical, this);
    plotterGainSlider->setRange(-130, 0);
    plotterGainSlider->setValue(-110);
    plotterGainSlider->setToolTip("Spectrum floor (dB)");
    plotterGainSlider->setFixedWidth(22);
    plotterGainSlider->setStyleSheet(
        "QSlider::groove:vertical { border: 1px solid #334455; width: 6px; background: #1a2a3a; border-radius: 3px; }"
        "QSlider::handle:vertical { background: #FFC300; height: 14px; margin: 0 -4px; border-radius: 7px; }"
    );
    connect(plotterGainSlider, &QSlider::valueChanged, this, [this](int value) {
        cPlotter->setPandapterRange(static_cast<float>(value), 0.f);
        cPlotter->setFftRange(static_cast<float>(value), 0.f);
    });

    connect(cPlotter, &CPlotter::newDemodFreq, this, &MainWindow::on_plotter_newDemodFreq);
    connect(cPlotter, &CPlotter::newFilterFreq, this, &MainWindow::on_plotter_newFilterFreq);
    connect(cPlotter, &CPlotter::wheelFreqChange, this, [this](int direction) {
        qint64 step = freqCtrl->getActiveStep();
        m_frequency += direction * step;
        freqCtrl->setFrequency(m_frequency);
        cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));
        if (m_isProcessing && m_hackTvLib)
            m_hackTvLib->setFrequency(m_frequency);
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

    // Plotter + vertical gain slider in horizontal layout
    QHBoxLayout *plotterLayout = new QHBoxLayout();
    plotterLayout->setSpacing(2);
    plotterLayout->addWidget(cPlotter, 1);
    plotterLayout->addWidget(plotterGainSlider, 0);
    rxLayout->addLayout(plotterLayout, 1);

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

    // RTL-SDR specific controls (hidden by default, shown when RTL-SDR selected)
    rtlPpmLabel = new QLabel("PPM:", rxGroup);
    rtlPpmLabel->setStyleSheet("QLabel { color: #c8f0ff; font-size: 11px; font-weight: bold; }");
    rtlPpmSpinBox = new QSpinBox(rxGroup);
    rtlPpmSpinBox->setRange(-200, 200);
    rtlPpmSpinBox->setValue(0);
    rtlPpmSpinBox->setSuffix(" ppm");
    rtlPpmSpinBox->setToolTip("Frequency correction in PPM");

    rtlDirectLabel = new QLabel("Sampling:", rxGroup);
    rtlDirectLabel->setStyleSheet("QLabel { color: #c8f0ff; font-size: 11px; font-weight: bold; }");
    rtlDirectCombo = new QComboBox(rxGroup);
    rtlDirectCombo->addItem("Normal", 0);
    rtlDirectCombo->addItem("Direct I (HF)", 1);
    rtlDirectCombo->addItem("Direct Q (HF)", 2);
    rtlDirectCombo->setToolTip("Direct sampling for HF reception (0-28.8 MHz)");

    rtlOffsetCheck = new QCheckBox("Offset Tuning", rxGroup);
    rtlOffsetCheck->setChecked(false);
    rtlOffsetCheck->setToolTip("Avoid DC spike by offset tuning");

    controlsGrid->addWidget(rtlPpmLabel, 1, 0);
    controlsGrid->addWidget(rtlPpmSpinBox, 1, 1);
    controlsGrid->addWidget(rtlDirectLabel, 1, 3);
    controlsGrid->addWidget(rtlDirectCombo, 1, 4, 1, 2);
    controlsGrid->addWidget(rtlOffsetCheck, 1, 6, 1, 2);

    // Connect RTL-SDR controls
    connect(rtlPpmSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int ppm) {
        if (m_isProcessing && m_hackTvLib) {
            m_hackTvLib->setFreqCorrection(ppm);
        }
        saveSettings();
    });

    connect(rtlDirectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        int mode = rtlDirectCombo->currentData().toInt();
        if (m_isProcessing && m_hackTvLib) {
            m_hackTvLib->setDirectSampling(mode);
        }
        saveSettings();
    });

    connect(rtlOffsetCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_isProcessing && m_hackTvLib) {
            m_hackTvLib->setOffsetTuning(checked);
        }
        saveSettings();
    });

    // Initially hide RTL-SDR controls
    rtlPpmLabel->setVisible(false);
    rtlPpmSpinBox->setVisible(false);
    rtlDirectLabel->setVisible(false);
    rtlDirectCombo->setVisible(false);
    rtlOffsetCheck->setVisible(false);

    // Row 2: RX Gain | FM Mod Index | De-Emphasis (FM demodulator controls)
    QString rxSliderLabel = "QLabel { color: #c8f0ff; font-size: 11px; font-weight: bold; }";

    rxGainLabel = new QLabel("Gain:", rxGroup);
    rxGainLabel->setStyleSheet(rxSliderLabel);
    rxGainSlider = new QSlider(Qt::Horizontal, rxGroup);
    rxGainSlider->setRange(0, 100);
    rxGainSlider->setValue(static_cast<int>(rxGain * 10));
    rxGainLevelLabel = new QLabel(QString::number(rxGain, 'f', 1), rxGroup);
    rxGainLevelLabel->setAlignment(Qt::AlignCenter);
    rxGainLevelLabel->setFixedWidth(32);
    rxGainLevelLabel->setStyleSheet(labelStyle);
    controlsGrid->addWidget(rxGainLabel, 2, 0);
    controlsGrid->addWidget(rxGainSlider, 2, 1);
    controlsGrid->addWidget(rxGainLevelLabel, 2, 2);

    rxModIndexLabel = new QLabel("ModIdx:", rxGroup);
    rxModIndexLabel->setStyleSheet(rxSliderLabel);
    rxModIndexSlider = new QSlider(Qt::Horizontal, rxGroup);
    rxModIndexSlider->setRange(1, 100);
    rxModIndexSlider->setValue(static_cast<int>(rxModIndex * 10));
    rxModIndexLevelLabel = new QLabel(QString::number(rxModIndex, 'f', 1), rxGroup);
    rxModIndexLevelLabel->setAlignment(Qt::AlignCenter);
    rxModIndexLevelLabel->setFixedWidth(32);
    rxModIndexLevelLabel->setStyleSheet(labelStyle);
    controlsGrid->addWidget(rxModIndexLabel, 2, 3);
    controlsGrid->addWidget(rxModIndexSlider, 2, 4);
    controlsGrid->addWidget(rxModIndexLevelLabel, 2, 5);

    rxDeemphLabel = new QLabel("DeEm:", rxGroup);
    rxDeemphLabel->setStyleSheet(rxSliderLabel);
    rxDeemphSlider = new QSlider(Qt::Horizontal, rxGroup);
    rxDeemphSlider->setRange(0, 1000);
    rxDeemphSlider->setValue(rxDeemph);
    rxDeemphLevelLabel = new QLabel(QString::number(rxDeemph), rxGroup);
    rxDeemphLevelLabel->setAlignment(Qt::AlignCenter);
    rxDeemphLevelLabel->setFixedWidth(32);
    rxDeemphLevelLabel->setStyleSheet(labelStyle);
    controlsGrid->addWidget(rxDeemphLabel, 2, 6);
    controlsGrid->addWidget(rxDeemphSlider, 2, 7);
    controlsGrid->addWidget(rxDeemphLevelLabel, 2, 8);

    // Connect RX demod sliders
    connect(rxGainSlider, &QSlider::valueChanged, this, [this](int value) {
        rxGain = value / 10.0f;
        rxGainLevelLabel->setText(QString::number(rxGain, 'f', 1));
        if (wbfmDemodulator) wbfmDemodulator->setOutputGain(rxGain);
        saveSettings();
    });

    connect(rxModIndexSlider, &QSlider::valueChanged, this, [this](int value) {
        rxModIndex = value / 10.0f;
        rxModIndexLevelLabel->setText(QString::number(rxModIndex, 'f', 1));
        if (wbfmDemodulator) wbfmDemodulator->setRxModIndex(rxModIndex);
        saveSettings();
    });

    connect(rxDeemphSlider, &QSlider::valueChanged, this, [this](int value) {
        rxDeemph = value;
        rxDeemphLevelLabel->setText(value == 0 ? "OFF" : QString("%1us").arg(value));
        if (wbfmDemodulator) wbfmDemodulator->setDeemphTau(static_cast<float>(rxDeemph));
        saveSettings();
    });

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

    // RTL-SDR controls: PPM, Direct Sampling, Offset Tuning
    rtlPpmLabel->setVisible(isRtlSdr);
    rtlPpmSpinBox->setVisible(isRtlSdr);
    rtlDirectLabel->setVisible(isRtlSdr);
    rtlDirectCombo->setVisible(isRtlSdr);
    rtlOffsetCheck->setVisible(isRtlSdr);
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
    if (!m_initDone) return;  // don't save during constructor init

    QSettings settings(m_sSettingsFile, QSettings::IniFormat);
    settings.beginGroup("Rf");

    settings.setValue("frequency", m_frequency);
    settings.setValue("samplerate", m_sampleRate);
    settings.setValue("lowcutfreq", m_LowCutFreq);
    settings.setValue("hicutfreq", m_HiCutFreq);
    // Store floats as integers (x1000) to avoid Turkish locale comma/dot issues
    settings.setValue("tx_amplitude_i", static_cast<int>(tx_amplitude * 1000));
    settings.setValue("tx_filter_size_i", static_cast<int>(tx_filter_size * 1000));
    settings.setValue("tx_modulation_index_i", static_cast<int>(tx_modulation_index * 1000));
    settings.setValue("tx_interpolation_i", static_cast<int>(tx_interpolation * 1000));
    settings.setValue("m_volumeLevel", m_volumeLevel);
    settings.setValue("m_txAmpGain", m_txAmpGain);
    settings.setValue("m_rxAmpGain", m_rxAmpGain);
    settings.setValue("m_lnaGain", m_lnaGain);
    settings.setValue("m_vgaGain", m_vgaGain);
    settings.setValue("audioGain_i", static_cast<int>(audioGain * 1000));
    settings.setValue("rxGain_i", static_cast<int>(rxGain * 1000));
    settings.setValue("rxModIndex_i", static_cast<int>(rxModIndex * 1000));
    settings.setValue("rxDeemph", rxDeemph);
    settings.setValue("ampEnabled", ampEnabled->isChecked());
    settings.setValue("colorDisabled", colorDisabled->isChecked());
    settings.endGroup();
}

void MainWindow::loadSettings()
{
    QSettings settings(m_sSettingsFile, QSettings::IniFormat);
    settings.beginGroup("Rf");
    m_frequency = settings.value("frequency").toLongLong();
    m_sampleRate = settings.value("samplerate").toInt();
    m_LowCutFreq = settings.value("lowcutfreq").toInt();
    m_HiCutFreq = settings.value("hicutfreq").toInt();
    // Integer-stored floats (x1000) - locale-safe
    // If new _i key exists, use it. Otherwise keep member default value.
    if (settings.contains("tx_amplitude_i"))
        tx_amplitude = settings.value("tx_amplitude_i").toInt() / 1000.0;
    if (settings.contains("tx_filter_size_i"))
        tx_filter_size = settings.value("tx_filter_size_i").toInt() / 1000.0;
    if (settings.contains("tx_modulation_index_i"))
        tx_modulation_index = settings.value("tx_modulation_index_i").toInt() / 1000.0;
    if (settings.contains("tx_interpolation_i"))
        tx_interpolation = settings.value("tx_interpolation_i").toInt() / 1000.0;
    m_volumeLevel = settings.value("m_volumeLevel", 10).toInt();
    m_txAmpGain = settings.value("m_txAmpGain", 40).toInt();
    m_rxAmpGain = settings.value("m_rxAmpGain", 0).toInt();
    m_lnaGain = settings.value("m_lnaGain", 40).toInt();
    m_vgaGain = settings.value("m_vgaGain", 40).toInt();
    if (settings.contains("audioGain_i"))
        audioGain = settings.value("audioGain_i").toInt() / 1000.0f;
    if (settings.contains("rxGain_i"))
        rxGain = settings.value("rxGain_i").toInt() / 1000.0f;
    if (settings.contains("rxModIndex_i"))
        rxModIndex = settings.value("rxModIndex_i").toInt() / 1000.0f;
    rxDeemph = settings.value("rxDeemph", 0).toInt();
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
    saveSettings();
}

void MainWindow::on_plotter_newDemodFreq(qint64 freq, qint64 delta)
{
    m_frequency = freq;
    cPlotter->setCenterFreq(static_cast<quint64>(freq));
    if (m_isProcessing)
        m_hackTvLib->setFrequency(m_frequency);
    freqCtrl->setFrequency(m_frequency);
    saveSettings();
}

void MainWindow::on_plotter_newFilterFreq(int low, int high)
{
    m_LowCutFreq = low;
    m_HiCutFreq = high;
    m_CutFreq = std::abs(high);
    if (m_isProcessing && wbfmDemodulator)
        wbfmDemodulator->setBandwidth(m_CutFreq);
    if (m_isProcessing && lowPassFilter)
        lowPassFilter->designFilter(m_sampleRate, m_CutFreq, 50000);
    saveSettings();
}

void MainWindow::executeCommand()
{

    if (executeButton->text() == "START")
    {
        // m_frequency is the authoritative frequency source.
        // Sync freqCtrl and cPlotter from it before anything else.
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

            // New multi-stage WBFM demodulator (replaces the above chain for actual processing)
            wbfmDemodulator = std::make_unique<WBFMDemodulator>(
                static_cast<double>(m_sampleRate),
                std::max(150000.0, static_cast<double>(m_CutFreq) * 2.0));

            // Apply saved demod settings
            wbfmDemodulator->setOutputGain(rxGain);
            wbfmDemodulator->setRxModIndex(rxModIndex);
            wbfmDemodulator->setDeemphTau(static_cast<float>(rxDeemph));
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

        // Apply all saved parameters AFTER start (device now exists)
        // TX params
        m_hackTvLib->setAmplitude(tx_amplitude);
        m_hackTvLib->setModulation_index(tx_modulation_index);
        m_hackTvLib->setTxAmpGain(m_txAmpGain);
        m_hackTvLib->setAmpEnable(ampEnabled->isChecked());
        // RX params
        m_hackTvLib->setLnaGain(m_lnaGain);
        m_hackTvLib->setVgaGain(m_vgaGain);
        m_hackTvLib->setRxAmpGain(m_rxAmpGain);

        // For FM TX: enable external audio ring and start mic capture or file playback
        if (mode == "tx" && (isFmTransmit || isFmFile)) {
            // Give device time to initialize before enabling audio ring
            QTimer::singleShot(1000, this, [this]() {
                if (!m_hackTvLib || !m_hackTvLib->isDeviceReady()) return;

                m_hackTvLib->enableExternalAudioRing();

                if (isFmTransmit) {
                    // Start Qt AudioSource mic capture
                    startMicCapture();
                    qDebug() << "FM TX: Mic capture started via Qt AudioSource";
                } else if (isFmFile && !inputFileEdit->text().isEmpty()) {
                    // Start file audio playback to TX
                    startFilePlayback(inputFileEdit->text());
                    qDebug() << "FM TX: File playback started:" << inputFileEdit->text();
                }
            });
        }

        executeButton->setText("STOP");
        QString argsString = args.join(' ');
        logBrowser->append(argsString);
        m_isProcessing.store(true);
    }
    else if (executeButton->text() == "STOP")
    {       
        m_isProcessing.store(false);

        // Stop GUI-side audio capture/playback
        stopMicCapture();
        stopFilePlayback();

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
            // GUI will capture audio via Qt AudioSource
            // and feed it via enableExternalAudioRing/writeExternalAudio
            sampleRateCombo->setCurrentIndex(0);
        }
        break;
    case 1: // Fm Transmitter File
        args << "fmtransmitter";
        if(mode == "tx" && !inputFileEdit->text().isEmpty())
        {
            // GUI will decode and feed audio via ring buffer
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

    auto sample_rate = QString::number(m_sampleRate);

    args << "-f" << QString::number(m_frequency)
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
    txFilterSizeSlider->setVisible(false);
    txFilterSizeSpinBox->setVisible(false);
    txModulationIndexSlider->setVisible(isFmAny);
    txModulationIndexSpinBox->setVisible(isFmAny);
    txInterpolationSlider->setVisible(false);
    txInterpolationSpinBox->setVisible(false);
    // TX Gain always visible in any TX mode
    txAmpSlider->setVisible(true);
    txAmpSpinBox->setVisible(true);
    tx_line->setVisible(true);

    // Show/hide labels in TX controls grid
    for (int i = 0; i < txControlsLayout->rowCount(); ++i) {
        for (int c : {0, 3}) {
            QLayoutItem* item = txControlsLayout->itemAtPosition(i, c);
            if (item && item->widget()) {
                if (i == 1 && c == 0) {
                    // Row 1 col 0 = TX Power label: always visible
                    item->widget()->setVisible(true);
                } else if (c == 3) {
                    // Col 3 labels (ModIdx right side): only in FM
                    item->widget()->setVisible(isFmAny);
                } else {
                    // Row 0 col 0 = Amplitude label: only FM
                    item->widget()->setVisible(isFmAny);
                }
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
    ampEnabled->setVisible(isTx);
    channelLabel->setVisible(isTx);
    channelCombo->setVisible(isTx);

    if(isTx)
    {
        inputTypeCombo->setCurrentIndex(0);  // Start in Tx mode
        onInputTypeChanged(0);  // This sets correct visibility for all TX controls
    }
    else
    {
        // RX mode - hide all TX controls
        txAmplitudeSlider->setVisible(false);
        txAmplitudeSpinBox->setVisible(false);
        txFilterSizeSlider->setVisible(false);
        txFilterSizeSpinBox->setVisible(false);
        txModulationIndexSlider->setVisible(false);
        txModulationIndexSpinBox->setVisible(false);
        txInterpolationSlider->setVisible(false);
        txInterpolationSpinBox->setVisible(false);
        txAmpSlider->setVisible(false);
        txAmpSpinBox->setVisible(false);
        tx_line->setVisible(false);

        // Hide all TX control labels
        for (int i = 0; i < txControlsLayout->rowCount(); ++i) {
            for (int c : {0, 3}) {
                QLayoutItem* item = txControlsLayout->itemAtPosition(i, c);
                if (item && item->widget()) {
                    item->widget()->setVisible(false);
                }
            }
        }
    }

    // Window is resizable - no adjustSize() needed
    update();
}

void MainWindow::onSampleRateChanged(int index)
{
    m_sampleRate = sampleRateCombo->currentData().toInt();
    if(m_isProcessing && m_hackTvLib->stop())
    {
        m_isProcessing.store(false);
        executeButton->setText("START");    lowPassFilter->designFilter(m_sampleRate, m_CutFreq, 10e3);
        if (wbfmDemodulator)
            wbfmDemodulator->setSampleRate(static_cast<double>(m_sampleRate));
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
    m_frequency = frequency;
    freqCtrl->setFrequency(frequency);
    cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));
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
    wbfmDemodulator.reset();

    // 5. Reset UI state
    executeButton->setText("START");

    // 6. Sync frequency from m_frequency (authoritative source)
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

    // Stop GUI-side audio
    stopMicCapture();
    stopFilePlayback();

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

// ============================================================
// GUI-side Audio Capture for FM TX
// Replaces PortAudio mic in DLL with Qt AudioSource
// ============================================================

void MainWindow::startMicCapture()
{
    stopMicCapture();

    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);

    QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        qDebug() << "No audio input device found";
        return;
    }

    qDebug() << "FM TX mic:" << inputDevice.description();

    // Fallback if Float32 not supported
    if (!inputDevice.isFormatSupported(format)) {
        format = inputDevice.preferredFormat();
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Float);
        qDebug() << "Using preferred format, rate:" << format.sampleRate();
    }

    m_micSource = new QAudioSource(inputDevice, format, this);
    m_micSource->setBufferSize(4096);

    m_micDevice = m_micSource->start();
    if (!m_micDevice) {
        qDebug() << "Failed to start mic capture";
        delete m_micSource;
        m_micSource = nullptr;
        return;
    }

    // Pre-fill ring buffer with 50ms silence to prevent TX callback starvation
    if (m_hackTvLib) {
        std::vector<float> silence(2205, 0.0f); // 50ms at 44100 Hz
        m_hackTvLib->writeExternalAudio(silence.data(), silence.size());
    }

    // Polling timer: read mic data every 5ms (matches HackRfRadio approach)
    // This is more reliable than readyRead which depends on Qt event loop responsiveness
    m_micFlushTimer = new QTimer(this);
    connect(m_micFlushTimer, &QTimer::timeout, this, [this]() {
        if (!m_micDevice || !m_hackTvLib) return;

        // Read all available data from mic
        QByteArray data = m_micDevice->readAll();
        if (data.isEmpty()) return;

        const float* p = reinterpret_cast<const float*>(data.constData());
        size_t n = data.size() / sizeof(float);

        if (n > 0) {
            m_hackTvLib->writeExternalAudio(p, n);
        }
    });
    m_micFlushTimer->start(5); // 5ms polling - 200Hz rate, smooth audio feed

    qDebug() << "Mic capture started at" << format.sampleRate() << "Hz, poll=5ms";
}

void MainWindow::stopMicCapture()
{
    if (m_micFlushTimer) {
        m_micFlushTimer->stop();
        delete m_micFlushTimer;
        m_micFlushTimer = nullptr;
    }
    if (m_micSource) {
        m_micSource->stop();
        delete m_micSource;
        m_micSource = nullptr;
        m_micDevice = nullptr;
        qDebug() << "Mic capture stopped";
    }
}

void MainWindow::startFilePlayback(const QString& filePath)
{
    stopFilePlayback();

    m_fileAudioData.clear();
    m_filePlayPos = 0;

    QString ext = filePath.section('.', -1).toLower();

    if (ext == "wav") {
        // Direct WAV parsing (fast, no Qt decoder overhead)
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qDebug() << "Cannot open:" << filePath;
            return;
        }

        QByteArray header = file.read(44);
        if (header.size() < 44) { file.close(); return; }

        QByteArray pcm = file.readAll();
        file.close();

        int channels = *reinterpret_cast<const int16_t*>(header.constData() + 22);
        int sampleRate = *reinterpret_cast<const int32_t*>(header.constData() + 24);
        int bitsPerSample = *reinterpret_cast<const int16_t*>(header.constData() + 34);

        qDebug() << "WAV:" << channels << "ch" << sampleRate << "Hz" << bitsPerSample << "bit";

        if (bitsPerSample == 16) {
            const int16_t* samples = reinterpret_cast<const int16_t*>(pcm.constData());
            size_t totalSamples = pcm.size() / sizeof(int16_t);

            if (channels == 2) {
                m_fileAudioData.resize(totalSamples / 2);
                for (size_t i = 0; i < totalSamples / 2; i++) {
                    m_fileAudioData[i] = (samples[i*2] + samples[i*2+1]) / (2.0f * 32768.0f);
                }
            } else {
                m_fileAudioData.resize(totalSamples);
                for (size_t i = 0; i < totalSamples; i++) {
                    m_fileAudioData[i] = samples[i] / 32768.0f;
                }
            }

            if (sampleRate != 44100 && sampleRate > 0) {
                size_t newLen = static_cast<size_t>(m_fileAudioData.size() * 44100.0 / sampleRate);
                std::vector<float> resampled(newLen);
                double ratio = static_cast<double>(m_fileAudioData.size()) / newLen;
                for (size_t i = 0; i < newLen; i++) {
                    double pos = i * ratio;
                    size_t idx = static_cast<size_t>(pos);
                    double frac = pos - idx;
                    if (idx + 1 < m_fileAudioData.size())
                        resampled[i] = static_cast<float>(m_fileAudioData[idx] * (1.0 - frac) + m_fileAudioData[idx+1] * frac);
                    else if (idx < m_fileAudioData.size())
                        resampled[i] = m_fileAudioData[idx];
                }
                m_fileAudioData = std::move(resampled);
            }
        }
        startFilePlaybackTimer();
    }
    else {
        // MP3, FLAC, OGG, AAC, etc. via QAudioDecoder
        qDebug() << "Decoding audio file via QAudioDecoder:" << filePath;

        QAudioFormat decodeFormat;
        decodeFormat.setSampleRate(44100);
        decodeFormat.setChannelCount(1);
        decodeFormat.setSampleFormat(QAudioFormat::Int16);

        m_audioDecoder = new QAudioDecoder(this);
        m_audioDecoder->setAudioFormat(decodeFormat);
        m_audioDecoder->setSource(QUrl::fromLocalFile(filePath));

        connect(m_audioDecoder, &QAudioDecoder::bufferReady, this, [this]() {
            QAudioBuffer buf = m_audioDecoder->read();
            if (!buf.isValid()) return;

            const int16_t* data = buf.constData<int16_t>();
            int sampleCount = buf.sampleCount();

            size_t oldSize = m_fileAudioData.size();
            m_fileAudioData.resize(oldSize + sampleCount);
            for (int i = 0; i < sampleCount; i++) {
                m_fileAudioData[oldSize + i] = data[i] / 32768.0f;
            }
        });

        connect(m_audioDecoder, &QAudioDecoder::finished, this, [this, filePath]() {
            qDebug() << "Decode finished:" << m_fileAudioData.size() << "samples"
                     << "(" << m_fileAudioData.size() / 44100.0 << "s)";
            startFilePlaybackTimer();
            m_audioDecoder->deleteLater();
            m_audioDecoder = nullptr;
        });

        connect(m_audioDecoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error),
                this, [this, filePath](QAudioDecoder::Error error) {
            qDebug() << "QAudioDecoder error:" << error << "for file:" << filePath;
            if (m_audioDecoder) {
                m_audioDecoder->deleteLater();
                m_audioDecoder = nullptr;
            }
        });

        m_audioDecoder->start();
    }
}

void MainWindow::startFilePlaybackTimer()
{
    if (m_fileAudioData.empty()) {
        qDebug() << "No audio data to play";
        return;
    }

    qDebug() << "Audio file loaded:" << m_fileAudioData.size() << "samples"
             << "(" << m_fileAudioData.size() / 44100.0 << "s)";

    m_filePlayPos = 0;
    m_filePlayTimer = new QTimer(this);
    connect(m_filePlayTimer, &QTimer::timeout, this, [this]() {
        if (!m_hackTvLib || m_fileAudioData.empty()) return;

        const size_t chunkSize = 882; // 20ms at 44100Hz
        size_t remaining = m_fileAudioData.size() - m_filePlayPos;

        if (remaining == 0) {
            if (m_fileLoop) {
                m_filePlayPos = 0;
                remaining = m_fileAudioData.size();
            } else {
                stopFilePlayback();
                return;
            }
        }

        size_t toSend = std::min(chunkSize, remaining);
        m_hackTvLib->writeExternalAudio(m_fileAudioData.data() + m_filePlayPos, toSend);
        m_filePlayPos += toSend;
    });
    m_filePlayTimer->start(20);
}

void MainWindow::stopFilePlayback()
{
    if (m_filePlayTimer) {
        m_filePlayTimer->stop();
        delete m_filePlayTimer;
        m_filePlayTimer = nullptr;
    }
    if (m_audioDecoder) {
        m_audioDecoder->stop();
        m_audioDecoder->deleteLater();
        m_audioDecoder = nullptr;
    }
    m_filePlayPos = 0;
    qDebug() << "File playback stopped";
}
