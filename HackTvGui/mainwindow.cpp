#include "mainwindow.h"
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include "filters.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    m_hackTvLib(std::make_unique<HackTvLib>())
{
    setupUi();

    try {        
        m_hackTvLib->setLogCallback([this](const std::string& msg) {
            handleLog(msg);
        });
        m_hackTvLib->setReceivedDataCallback([this](const int16_t* data, size_t samples) {
            handleReceivedData(data, samples);
        });
        audioOutput = new AudioOutput(this, DEFAULT_AUDIO_SAMPLE_RATE);
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
    delete audioOutput;
}

void MainWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

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
    QLabel *gainLabel = new QLabel("Gain:", this);
    gainEdit = new QLineEdit(this);
    gainEdit->setText("47");
    gainEdit->setFixedWidth(35);
    QLabel *freqLabel = new QLabel("Frequency (Hz):", this);
    frequencyEdit = new QLineEdit(this);
    frequencyEdit->setFixedWidth(75);
    QLabel *channelLabel = new QLabel("Channel:", this);
    channelCombo = new QComboBox(this);
    QLabel *sampleRateLabel = new QLabel("Sample Rate (MHz):", this);
    sampleRateEdit = new QLineEdit(this);
    sampleRateEdit->setText("16");
    sampleRateEdit->setFixedWidth(35);
    QLabel *rxtxLabel = new QLabel("RxTx Mode:", this);
    rxtxCombo = new QComboBox(this);
    rxtxCombo->addItem("TX", "tx");
    rxtxCombo->addItem("RX", "rx");

    outputLayout->addWidget(outputLabel, 0, 0);
    outputLayout->addWidget(outputCombo, 0, 1);

    outputLayout->addWidget(ampEnabled, 1, 0);
    outputLayout->addWidget(a2Stereo, 1, 1);
    outputLayout->addWidget(repeat, 1, 2);
    outputLayout->addWidget(acp, 1, 3);
    outputLayout->addWidget(filter, 1, 4);

    outputLayout->addWidget(channelLabel, 2, 0);
    outputLayout->addWidget(channelCombo, 2, 1);
    outputLayout->addWidget(sampleRateLabel, 2, 2);
    outputLayout->addWidget(sampleRateEdit, 2, 3);
    outputLayout->addWidget(gainLabel, 2, 4);
    outputLayout->addWidget(gainEdit, 2, 5);

    outputLayout->addWidget(freqLabel, 3, 0);
    outputLayout->addWidget(frequencyEdit, 3, 1);    
    outputLayout->addWidget(rxtxLabel, 3, 2);
    outputLayout->addWidget(rxtxCombo, 3, 3);
    outputLayout->addWidget(colorDisabled, 3, 4);

    mainLayout->addWidget(outputGroup);
    populateChannelCombo();

    // Mode group
    modeGroup = new QGroupBox("Mode", this);
    QHBoxLayout *modeLayout = new QHBoxLayout(modeGroup);
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
    mainLayout->addWidget(modeGroup);

    // Input type group
    inputTypeGroup = new QGroupBox("Input Type", this);
    QVBoxLayout *inputTypeLayout = new QVBoxLayout(inputTypeGroup);
    inputTypeCombo = new QComboBox(this);
    inputTypeCombo->addItems({"File", "Test", "FFmpeg"});
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

    logBrowser = new QTextBrowser(this);
    mainLayout->addWidget(logBrowser);

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
}

void MainWindow::processReceivedData(const QVector<int16_t>& data)
{
    const float input_sample_rate = _MHZ(2);  // 2 MHz
    const float output_sample_rate = _KHZ(48);
    const float max_freq_deviation = _KHZ(75);

    try {

        std::vector<std::complex<float>> iq_samples;
        iq_samples.reserve(data.size() / 2);
        AGC agc(0.5f, 0.001f, 0.0001f);
        for (qsizetype i = 0; i < data.size() - 1; i += 2) {
            float I = agc.process(data[i] / 32768.0f);
            float Q = agc.process(data[i + 1] / 32768.0f);
            iq_samples.emplace_back(I, Q);
        }

        FMDemodulator demodulator(max_freq_deviation, input_sample_rate);
        std::vector<float> demodulated = demodulator.demodulate(iq_samples);

        //Apply pre-emphasis
        float pre_emphasis_alpha = 1.0f - std::exp(-1.0f / (input_sample_rate * 50e-6f));
        float pre_emphasis_y = 0.0f;
        for (float& sample : demodulated) {
            float x = sample;
            sample = sample + pre_emphasis_alpha * (sample - pre_emphasis_y);
            pre_emphasis_y = x;
        }

        //Design and apply lowpass filter
        std::vector<float> lpf_coeffs = design_lowpass_filter(15000.0f, input_sample_rate, 101);
        FIRFilter lpf(lpf_coeffs);
        std::vector<float> filtered;
        filtered.reserve(demodulated.size());
        for (float sample : demodulated) {
            filtered.push_back(lpf.process(sample));
        }

        // Rational resampling
        const int upsample_factor = 1;
        const int downsample_factor = 48;

        std::vector<float> resampled;
        resampled.reserve(filtered.size() * upsample_factor / downsample_factor);
        RationalResampler resampler(upsample_factor, downsample_factor);

        for (float sample : filtered) {
            std::vector<float> resampled_samples = resampler.process(sample);
            resampled.insert(resampled.end(), resampled_samples.begin(), resampled_samples.end());
        }

        // De-emphasis filter
        float de_emphasis_alpha = 1.0f - std::exp(-1.0f / (output_sample_rate * 75e-6f));
        float de_emphasis_y = 0.0f;
        for (float& sample : resampled) {
            de_emphasis_y += de_emphasis_alpha * (sample - de_emphasis_y);
            sample = de_emphasis_y;
        }

        // Normalize and apply soft clipping
        float max_amplitude = *std::max_element(resampled.begin(), resampled.end(),
                                                [](float a, float b) { return std::abs(a) < std::abs(b); });
        float scale_factor = (max_amplitude > 0.01f) ? 0.95f / max_amplitude : 1.0f;

        auto soft_clip = [](float x) {
            return std::tanh(x);
        };

        QByteArray audioData;
        audioData.reserve(resampled.size() * sizeof(float));

        for (float sample : resampled) {
            sample *= scale_factor;
            sample = soft_clip(sample);
            audioData.append(reinterpret_cast<const char*>(&sample), sizeof(float));
        }

        audioOutput->writeBuffer(audioData);

        qDebug() << "Audio" << audioData.size() / sizeof(float) << "samples. Max amplitude:" << max_amplitude;
        qDebug() << "First 10 audio samples:";
        for (qsizetype i = 0; i < std::min<qsizetype>(10, audioData.size() / sizeof(float)); ++i) {
            float sample = *reinterpret_cast<const float*>(audioData.constData() + i * sizeof(float));
            qDebug() << sample;
        }
    }
    catch (const std::exception& e) {
        qDebug() << "Exception caught in processReceivedData:" << e.what();
    }
    catch (...) {
        qDebug() << "Unknown exception caught in processReceivedData";
    }
}

void MainWindow::handleReceivedData(const int16_t* data, size_t samples)
{
    QMetaObject::invokeMethod(this, "processReceivedData", Qt::QueuedConnection,
                              Q_ARG(QVector<int16_t>, QVector<int16_t>(data, data + samples)));
}

void MainWindow::executeCommand()
{
    if (executeButton->text() == "Start")
    {
        QStringList args = buildCommand();

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

    args << "-o" << output;

    if (!gainEdit->text().isEmpty()) {
        args << "-g" << gainEdit->text();
    }

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

    QString mode = rxtxCombo->currentText().toLower();
    args << "--rx-tx-mode" << mode;

    auto sample_rate = QString::number(sampleRateEdit->text().toInt() * 1000000);

    args << "-f" << frequencyEdit->text()
         << "-s" << sample_rate
         << "-m" << modeCombo->currentData().toString();

    switch(inputTypeCombo->currentIndex())
    {
    case 1: // Test
        args << "test";        
        break;
    case 2: // FFmpeg
    {
        QString ffmpegArg = "ffmpeg:";
        if (!ffmpegOptionsEdit->text().isEmpty()) {
            ffmpegArg += ffmpegOptionsEdit->text();
        }
        args << ffmpegArg;
    }
    break;
    default: // File
        if (!inputFileEdit->text().isEmpty()) {
            args << inputFileEdit->text();
        }
        break;
    }
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
    bool isFile = (index == 0);
    bool isFFmpeg = (index == 2);

    inputFileEdit->setVisible(isFile);
    chooseFileButton->setVisible(isFile);
    ffmpegOptionsEdit->setVisible(isFFmpeg);    
}

void MainWindow::onRxTxTypeChanged(int index)
{
    bool isTx = (index == 0);
    inputTypeGroup->setVisible(isTx);
    modeGroup->setVisible(isTx);
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

    connect(channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onChannelChanged);

    channelCombo->addItem("Custom", "");

    for (const auto &channel : channels) {
        channelCombo->addItem(channel.name, channel.frequency);
    }

    int defaultIndex = channelCombo->findText("E12");
    if (defaultIndex != -1) {
        channelCombo->setCurrentIndex(defaultIndex);
    }
}

void MainWindow::onChannelChanged(int index)
{
    if (index == 0) {
        // "Custom" selected, do nothing
        return;
    }

    long long frequency = channelCombo->itemData(index).toLongLong();
    frequencyEdit->setText(QString::number(DEFAULT_FREQUENCY));
}
