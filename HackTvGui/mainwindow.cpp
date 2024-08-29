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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    m_hackTvLib(std::make_unique<HackTvLib>())
{
    setupUi();

    try {        
        m_hackTvLib->setLogCallback([this](const std::string& msg) {
            handleLog(msg);
        });        
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

void MainWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // Output device group
    QGroupBox *outputGroup = new QGroupBox("Output Device", this);
    QGridLayout *outputLayout = new QGridLayout(outputGroup);
    outputLayout->setVerticalSpacing(10);
    outputLayout->setHorizontalSpacing(15);

    // Row 1: Output device and Amp
    QLabel *outputLabel = new QLabel("Device:", this);
    outputEdit = new QLineEdit(this);
    outputEdit->setText("hackrf");
    outputEdit->setFixedWidth(120);
    ampEnabledCheckBox = new QCheckBox("Amp", this);
    outputLayout->addWidget(outputLabel, 0, 0);
    outputLayout->addWidget(outputEdit, 0, 1);
    outputLayout->addWidget(ampEnabledCheckBox, 0, 2);

    // Row 2: Gain and Frequency
    QLabel *gainLabel = new QLabel("Gain:", this);
    gainEdit = new QLineEdit(this);
    gainEdit->setText("47");
    gainEdit->setFixedWidth(60);
    QLabel *freqLabel = new QLabel("Frequency (Hz):", this);
    frequencyEdit = new QLineEdit(this);
    frequencyEdit->setText("855250000");
    frequencyEdit->setFixedWidth(120);
    QLabel *channelLabel = new QLabel("Channel:", this);
    channelCombo = new QComboBox(this);
    outputLayout->addWidget(gainLabel, 1, 0);
    outputLayout->addWidget(gainEdit, 1, 1);
    outputLayout->addWidget(channelLabel, 1, 2);
    outputLayout->addWidget(channelCombo, 1, 3);
    outputLayout->addWidget(freqLabel, 1, 4);
    outputLayout->addWidget(frequencyEdit, 1, 5);

    // Row 3: Sample Rate
    QLabel *sampleRateLabel = new QLabel("Sample Rate (MHz):", this);
    sampleRateEdit = new QLineEdit(this);
    sampleRateEdit->setText("16");
    sampleRateEdit->setFixedWidth(60);
    outputLayout->addWidget(sampleRateLabel, 2, 0, 1, 2);
    outputLayout->addWidget(sampleRateEdit, 2, 2);

    mainLayout->addWidget(outputGroup);
    populateChannelCombo();

    // Mode group
    QGroupBox *modeGroup = new QGroupBox("Mode", this);
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
    QGroupBox *inputTypeGroup = new QGroupBox("Input Type", this);
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
    ffmpegOptionsEdit->setText("rtsp://example.com/stream");
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
    connect(channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onChannelChanged);
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

    args << "-o" << outputEdit->text();

    if (ampEnabledCheckBox->isChecked()) {
        args << "-a" ;
    }

    if (!gainEdit->text().isEmpty()) {
        args << "-g" << gainEdit->text();
    }

    auto sample_rate = QString::number(sampleRateEdit->text().toInt() * 1000000);

    args << "-f" << frequencyEdit->text()
         << "-s" << sample_rate
         << "-m" << modeCombo->currentData().toString();

    args << "--repeat";

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

    QString argsString = args.join(' ');
    logBrowser->append(argsString);

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

void MainWindow::populateChannelCombo()
{
    struct Channel {
        QString name;
        long long frequency;
    };

    QVector<Channel> channels = {
        {"VHF 2", 48250000},
        {"VHF 3", 55250000},
        {"VHF 4", 62250000},
        {"VHF 5", 175250000},
        {"VHF 6", 182250000},
        {"VHF 7", 189250000},
        {"VHF 8", 196250000},
        {"VHF 9", 203250000},
        {"VHF 10", 210250000},
        {"VHF 11", 217250000},
        {"VHF 12", 224250000},
        {"UHF 21", 471250000},
        {"UHF 22", 479250000},
        {"UHF 23", 487250000},
        {"UHF 24", 495250000},
        {"UHF 25", 503250000},
        {"UHF 26", 511250000},
        {"UHF 27", 519250000},
        {"UHF 28", 527250000},
        {"UHF 29", 535250000},
        {"UHF 30", 543250000},
        {"UHF 31", 551250000},
        {"UHF 32", 559250000},
        {"UHF 33", 567250000},
        {"UHF 34", 575250000},
        {"UHF 35", 583250000},
        {"UHF 36", 591250000},
        {"UHF 37", 599250000},
        {"UHF 38", 607250000},
        {"UHF 39", 615250000},
        {"UHF 40", 623250000},
        {"UHF 41", 631250000},
        {"UHF 42", 639250000},
        {"UHF 43", 647250000},
        {"UHF 44", 655250000},
        {"UHF 45", 663250000},
        {"UHF 46", 671250000},
        {"UHF 47", 679250000},
        {"UHF 48", 687250000},
        {"UHF 49", 695250000},
        {"UHF 50", 703250000},
        {"UHF 51", 711250000},
        {"UHF 52", 719250000},
        {"UHF 53", 727250000},
        {"UHF 54", 735250000},
        {"UHF 55", 743250000},
        {"UHF 56", 751250000},
        {"UHF 57", 759250000},
        {"UHF 58", 767250000},
        {"UHF 59", 775250000},
        {"UHF 60", 783250000},
        {"UHF 61", 791250000},
        {"UHF 62", 799250000},
        {"UHF 63", 807250000},
        {"UHF 64", 815250000},
        {"UHF 65", 823250000},
        {"UHF 66", 831250000},
        {"UHF 67", 839250000},
        {"UHF 68", 847250000},
        {"UHF 69", 855250000}
    };

    channelCombo->addItem("Custom");

    for (const auto &channel : channels) {
        channelCombo->addItem(channel.name, channel.frequency);
    }

    int defaultIndex = channelCombo->findText("UHF 69");
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
    frequencyEdit->setText(QString::number(frequency));
}
