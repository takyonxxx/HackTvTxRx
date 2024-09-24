#include "mainwindow.h"
#include <QFuture>
#include <QLabel>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include "constants.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    m_hackTvLib(std::make_unique<HackTvLib>()),

    m_frequency(DEFAULT_FREQUENCY),
    m_sampleRate(DEFAULT_SAMPLE_RATE),
    m_LowCutFreq(-1*int(DEFAULT_CUT_OFF)),
    m_HiCutFreq(DEFAULT_CUT_OFF),
    m_isProcessing(false)
{
    QString homePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    m_sSettingsFile = homePath + "/hacktv_settings.ini";
    m_threadPool.setMaxThreadCount(QThread::idealThreadCount());

    if (QFile(m_sSettingsFile).exists())
        loadSettings();
    else
        saveSettings();

    setupUi();

    frequencyEdit->setText(QString::number(m_frequency));
    setCurrentSampleRate(m_sampleRate);
    cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));
    cPlotter->setHiLowCutFrequencies(m_LowCutFreq, m_HiCutFreq);
    freqCtrl->setFrequency(m_frequency);

    try {        
        m_hackTvLib->setLogCallback([this](const std::string& msg) {
            handleLog(msg);
        });
        m_hackTvLib->setReceivedDataCallback([this](const int8_t* data, size_t len) {
            handleReceivedData(data, len);
        });
        m_hackTvLib->setMicEnabled(false);

        audioOutput = std::make_unique<AudioOutput>();
        if (!audioOutput) {
            throw std::runtime_error("Failed to create AudioOutput");
        }

        m_signalProcessor = new SignalProcessor(this);
        connect(m_signalProcessor, &SignalProcessor::samplesReady, this, &MainWindow::handleSamples);
        m_signalProcessor->start();
    }
    catch (const std::exception& e) {
        qDebug() << "Exception in createHackTvLib:" << e.what();
        QMessageBox::critical(this, "HackTvLib Error", QString("Failed to create HackTvLib: %1").arg(e.what()));
    }
    catch (...) {
        qDebug() << "Unknown exception in createHackTvLib";
        QMessageBox::critical(this, "HackTvLib Error", "An unknown error occurred while creating HackTvLib");
    }

    logTimer = new QTimer(this);
    connect(logTimer, &QTimer::timeout, this, &MainWindow::updateLogDisplay);
    logTimer->start(100);
}

MainWindow::~MainWindow()
{
    m_signalProcessor->stop();
    m_signalProcessor->wait();
    audioOutput.reset();
    fmDemodulator.reset();
    rationalResampler.reset();
    lowPassFilter.reset();
}

void MainWindow::setCurrentSampleRate(int sampleRate)
{
    int index = sampleRateCombo->findData(sampleRate);
    if (index != -1) {
        sampleRateCombo->setCurrentIndex(index);
    } else {
        // If the exact sample rate is not found, find the closest one
        int closestIndex = 0;
        int smallestDiff = std::numeric_limits<int>::max();
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

void MainWindow::setupUi()
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    resize(800, 600);

    // Output device group
    QGroupBox *outputGroup = new QGroupBox("Output Device", this);
    QGridLayout *outputLayout = new QGridLayout(outputGroup);
    outputLayout->setVerticalSpacing(15);
    outputLayout->setHorizontalSpacing(15);

    QVector<QPair<QString, QString>> devices = {
        {"HackRF", "hackrf"},
        {"SoapySDR", "soapysdr"},
        {"FL2000", "fl2k"},
        //{"File", "C:\\test.mp4"}
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
    a2Stereo = new QCheckBox("A2 Stereo", this);
    a2Stereo->setChecked(true);
    repeat = new QCheckBox("Repeat", this);
    repeat->setChecked(true);
    acp = new QCheckBox("Acp", this);
    acp->setChecked(true);
    filter = new QCheckBox("Filter", this);
    filter->setChecked(true);  
    QLabel *freqLabel = new QLabel("Frequency (Hz):", this);
    frequencyEdit = new QLineEdit(this);
    frequencyEdit->setFixedWidth(75);
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
    txControlsLayout->addWidget(new QLabel("TX Amplitude:"), 0, 0);
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
    txControlsLayout->addWidget(new QLabel("TX Filter Size:"), 1, 0);
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
    txControlsLayout->addWidget(new QLabel("TX Modulation Index:"), 2, 0);
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
    txControlsLayout->addWidget(new QLabel("TX Interpolation:"), 3, 0);
    txControlsLayout->addWidget(txInterpolationSlider, 3, 1);
    txControlsLayout->addWidget(txInterpolationSpinBox, 3, 2);

    // Connect signals and slots
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

    outputLayout->addWidget(outputLabel, 0, 0);
    outputLayout->addWidget(outputCombo, 0, 1);

    outputLayout->addWidget(ampEnabled, 1, 0);
    outputLayout->addWidget(a2Stereo, 1, 1);
    outputLayout->addWidget(repeat, 1, 2);
    outputLayout->addWidget(acp, 1, 3);
    outputLayout->addWidget(filter, 1, 4);
    outputLayout->addWidget(colorDisabled, 1, 5);

    outputLayout->addWidget(channelLabel, 2, 0);
    outputLayout->addWidget(channelCombo, 2, 1);
    outputLayout->addWidget(sampleRateLabel, 2, 2);
    outputLayout->addWidget(sampleRateCombo, 2, 3);

    outputLayout->addWidget(freqLabel, 3, 0);
    outputLayout->addWidget(frequencyEdit, 3, 1);
    outputLayout->addWidget(rxtxLabel, 3, 2);
    outputLayout->addWidget(rxtxCombo, 3, 3);

    tx_line = new QFrame();
    tx_line->setFrameShape(QFrame::HLine);
    tx_line->setFrameShadow(QFrame::Sunken);
    outputLayout->addWidget(tx_line, 4, 0, 1, 6);  // Span all 6 columns

    outputLayout->addLayout(txControlsLayout, 5, 0, 1, 6);  // Span all 6 columns

    mainLayout->addWidget(outputGroup);

    freqCtrl = new CFreqCtrl();
    freqCtrl->setup(0, 0, 6000e6, 1, FCTL_UNIT_MHZ);
    freqCtrl->setDigitColor(QColor("#FFC300"));
    freqCtrl->setFrequency(DEFAULT_FREQUENCY);
    connect(freqCtrl, &CFreqCtrl::newFrequency, this, &MainWindow::onFreqCtrl_setFrequency);
    freqCtrl->setMinimumHeight(50);

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
    QGridLayout *rxLayout = new QGridLayout(rxGroup);
    rxLayout->setVerticalSpacing(15);
    rxLayout->setHorizontalSpacing(15);
    rxLayout->addWidget(cMeter);
    rxLayout->setVerticalSpacing(15);
    rxLayout->setHorizontalSpacing(15);
    rxLayout->addWidget(freqCtrl);
    rxLayout->addWidget(cPlotter);
    mainLayout->addWidget(rxGroup);

    // Mode group
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

    // Input type group
    inputTypeGroup = new QGroupBox("Input Type", this);
    QVBoxLayout *inputTypeLayout = new QVBoxLayout(inputTypeGroup);
    inputTypeCombo = new QComboBox(this);
    inputTypeCombo->addItems({ "Fm Transmitter", "File", "Test", "FFmpeg"});
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
    ffmpegOptionsEdit->setText("rtsp://localhost:8554/live");
    ffmpegOptionsEdit->setVisible(false);  // Initially hidden
    inputTypeLayout->addWidget(ffmpegOptionsEdit);

    mainLayout->addWidget(inputTypeGroup);    
    mainLayout->addWidget(modeGroup);

    logBrowser = new QTextBrowser(this);
    mainLayout->addWidget(logBrowser);

    QGroupBox *logGroup = new QGroupBox("Info", this);
    QGridLayout *logLayout = new QGridLayout(logGroup);
    logLayout->setVerticalSpacing(15);
    logLayout->setHorizontalSpacing(15);
    logLayout->addWidget(logBrowser);
    mainLayout->addWidget(logGroup);

    executeButton = new QPushButton("Start", this);
    exitButton = new QPushButton("Exit", this);
    connect(exitButton, &QPushButton::clicked, this, &MainWindow::close);
    mainLayout->addWidget(executeButton);
    mainLayout->addWidget(exitButton);

    mainLayout->addStretch();

    setCentralWidget(centralWidget);

    fileDialog = new QFileDialog(this);
    fileDialog->setFileMode(QFileDialog::ExistingFile);
    fileDialog->setNameFilter("Video Files (*.flv *.mp4);;All Files (*)");

    QString initialDir = QDir::homePath() + "/Desktop/Videos";
    if (!QDir(initialDir).exists()) {
        initialDir = QDir::homePath() + "/Videos";
    }
    fileDialog->setDirectory(initialDir);   

    // Connect signals and slots
    connect(executeButton, &QPushButton::clicked, this, &MainWindow::executeCommand);
    connect(chooseFileButton, &QPushButton::clicked, this, &MainWindow::chooseFile);
    connect(inputTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onInputTypeChanged);
    connect(rxtxCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onRxTxTypeChanged);
    connect(sampleRateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSampleRateChanged);

    inputTypeGroup->setVisible(false);
    modeGroup->setVisible(false);
    rxGroup->setVisible(true);

    // Hide TX controls initially
    txAmplitudeSlider->setVisible(false);
    txAmplitudeSpinBox->setVisible(false);
    txFilterSizeSlider->setVisible(false);
    txFilterSizeSpinBox->setVisible(false);
    txModulationIndexSlider->setVisible(false);
    txModulationIndexSpinBox->setVisible(false);
    txInterpolationSlider->setVisible(false);
    txInterpolationSpinBox->setVisible(false);
    tx_line->setVisible(false);

    for (int i = 0; i < txControlsLayout->rowCount(); ++i) {
        QLayoutItem* item = txControlsLayout->itemAtPosition(i, 0);
        if (item && item->widget()) {
            item->widget()->setVisible(false);
        }
    }

    rxtxCombo->setCurrentIndex(0);  // Start in RX mode
    onRxTxTypeChanged(0);
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
    settings.endGroup();
}

void MainWindow::handleSamples(const std::vector<std::complex<float>>& samples)
{
    QFuture<void> fftFuture = QtConcurrent::run(&m_threadPool, [this, samples]() {
        this->processFft(samples);
    });

    QFuture<void> demodFuture = QtConcurrent::run(&m_threadPool, [this, samples]() {
        this->processDemod(samples);
    });

    fftFuture.waitForFinished();
}

void MainWindow::processFft(const std::vector<std::complex<float>>& samples)
{
    int fft_size = 2048;
    std::vector<float> fft_output(fft_size);
    float signal_level_dbfs;
    getFft(samples, fft_output, signal_level_dbfs, fft_size);
    cMeter->setLevel(signal_level_dbfs);

    cPlotter->setNewFttData(fft_output.data(), fft_output.data(), fft_size);
}

void MainWindow::processDemod(const std::vector<std::complex<float>>& samples)
{
    if (lowPassFilter && rationalResampler && fmDemodulator && audioOutput) {
        auto filteredSamples = lowPassFilter->apply(samples);
        auto resampledSamples = rationalResampler->resample(filteredSamples);
        auto demodulatedSamples = fmDemodulator->demodulate(resampledSamples);

#pragma omp parallel for
        for (size_t i = 0; i < demodulatedSamples.size(); ++i) {
            demodulatedSamples[i] *= audioGain;
        }

        QMetaObject::invokeMethod(this, "processAudio",
                                  Qt::QueuedConnection,
                                  Q_ARG(const std::vector<float>&, demodulatedSamples));
    } else {
        qDebug() << "One or more components of the signal chain are not initialized.";
    }
}

void MainWindow::processAudio(const std::vector<float>& demodulatedSamples)
{
    audioOutput->processAudio(demodulatedSamples);
}

void MainWindow::processReceivedData(const int8_t *data, size_t len)
{
    if (!m_isProcessing.load() || !data || len != 262144) {
        return;
    }

    std::vector<std::complex<float>> samples(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        float i_sample = data[i] / 128.0f;
        float q_sample = data[i + 1] / 128.0f;
        samples[i/2] = std::complex<float>(i_sample, q_sample);
    }

    m_signalProcessor->addSamples(samples.data());
}

void MainWindow::handleReceivedData(const int8_t *data, size_t len)
{
    QMetaObject::invokeMethod(this, "processReceivedData", Qt::QueuedConnection,
                              Q_ARG(const int8_t*, data),
                              Q_ARG(size_t, len));
}

void MainWindow::onFreqCtrl_setFrequency(qint64 freq)
{
    m_frequency = freq;
    cPlotter->setCenterFreq(static_cast<quint64>(freq));
    if (m_isProcessing)
        m_hackTvLib->setFrequency(m_frequency);
    frequencyEdit->setText(QString::number(m_frequency));
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
        lowPassFilter->designFilter(m_sampleRate, m_HiCutFreq, transitionWidth);
    saveSettings();
}

void MainWindow::executeCommand()
{
    if (executeButton->text() == "Start")
    {
        QStringList args = buildCommand();

        qDebug() << mode;

        if(mode == "rx")
        {
            lowPassFilter = std::make_unique<LowPassFilter>(m_sampleRate, m_HiCutFreq, transitionWidth);
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

    if (repeat->isChecked()) {
        args << "--repeat";
    }

    if (a2Stereo->isChecked()) {
        args << "--a2stereo";
    }

    if (filter->isChecked()) {
        args << "--filter";
    }

    if (acp->isChecked()) {
        args << "--acp";
    }

    switch(inputTypeCombo->currentIndex())
    {
    case 0: // fmtransmitter
        args << "fmtransmitter";
        m_hackTvLib->setMicEnabled(true);
        sampleRateCombo->setCurrentIndex(0);
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

    if(!isFmTransmit)
        sampleRateCombo->setCurrentIndex(6);
    else
        sampleRateCombo->setCurrentIndex(0);

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
    tx_line->setVisible(isTx);

    // Also hide/show labels
    for (int i = 0; i < txControlsLayout->rowCount(); ++i) {
        QLayoutItem* item = txControlsLayout->itemAtPosition(i, 0);
        if (item && item->widget()) {
            item->widget()->setVisible(isTx);
        }
    }

    setMinimumSize(800, 600);  // Set a minimum size
    setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
    resize(800, 600);
    adjustSize();
    update();
}

void MainWindow::onSampleRateChanged(int index)
{
    m_sampleRate = sampleRateCombo->currentData().toInt();
    if(m_isProcessing)
    {
        if(mode == "tx")
        {
            if(!m_hackTvLib->stop())
                m_isProcessing.store(false);
            else
                logBrowser->append("Failed to stop HackTvLib.");

            m_hackTvLib->setSampleRate(m_sampleRate);

            if(m_hackTvLib->start()) {
                m_isProcessing.store(true);
            } else {
                logBrowser->append("Failed to start HackTvLib.");
            }
        }
        else
             m_hackTvLib->setSampleRate(m_sampleRate);

        lowPassFilter->designFilter(m_sampleRate, m_HiCutFreq, 10e3);
        cPlotter->setSampleRate(m_sampleRate);
        cPlotter->setSpanFreq(static_cast<quint32>(m_sampleRate));
        cPlotter->setCenterFreq(static_cast<quint64>(m_frequency));
    }
    saveSettings();
}

void MainWindow::populateChannelCombo()
{
    struct Channel {
        QString name;
        long long frequency;
    };

    QVector<Channel> channels = {
                                 {"Radio", 445900000},
                                 {"RadioTraffic", 88400000},
                                 {"PowerFm", 100000000},
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

    connect(channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onChannelChanged);    

    for (const auto &channel : channels) {
        channelCombo->addItem(channel.name, channel.frequency);
    }

    int defaultIndex = channelCombo->findText("Radio");
    if (defaultIndex != -1) {
        channelCombo->setCurrentIndex(defaultIndex);
    }
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
        saveSettings();
    }
}
