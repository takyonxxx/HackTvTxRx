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
    : QMainWindow(parent)
{
    setupUi();
    resize(400, 600);

    try {
        m_hackTvLib = std::make_unique<HackTvLib>();
        m_hackTvLib->setLogCallback([this](const std::string& msg) {
            handleLog(msg);
        });
        logBrowser->append("HackTvLib created successfully");
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
    gainEdit->setText("40");
    gainEdit->setFixedWidth(60);

    QLabel *freqLabel = new QLabel("Frequency (Hz):", this);
    frequencyEdit = new QLineEdit(this);
    frequencyEdit->setText("471250000");
    frequencyEdit->setFixedWidth(120);

    outputLayout->addWidget(gainLabel, 1, 0);
    outputLayout->addWidget(gainEdit, 1, 1);
    outputLayout->addWidget(freqLabel, 1, 2);
    outputLayout->addWidget(frequencyEdit, 1, 3);

    // Row 3: Sample Rate
    QLabel *sampleRateLabel = new QLabel("Sample Rate (MHz):", this);
    sampleRateEdit = new QLineEdit(this);
    sampleRateEdit->setText("16");
    sampleRateEdit->setFixedWidth(60);

    outputLayout->addWidget(sampleRateLabel, 2, 0, 1, 2);
    outputLayout->addWidget(sampleRateEdit, 2, 2);

    mainLayout->addWidget(outputGroup);

    // Mode group
    QGroupBox *modeGroup = new QGroupBox("Mode", this);
    QHBoxLayout *modeLayout = new QHBoxLayout(modeGroup);
    modeCombo = new QComboBox(this);
    modeCombo->addItems({"pal", "ntsc", "secam"}); // Add more modes as needed
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

    // Execute button
    executeButton = new QPushButton("Start", this);
    mainLayout->addWidget(executeButton);

    // Spacer to push everything to the top
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
}

void MainWindow::executeCommand()
{
    if (executeButton->text() == "Start")
    {
        // Start the command
        QStringList args = buildCommand();
        int argc = args.size() + 1;
        std::vector<char*> argv(argc);
        argv[0] = strdup("HackTv");

        // Convert QStrings to C-style strings
        for(int i = 0; i < args.size(); ++i) {
            argv[i + 1] = strdup(args[i].toLocal8Bit().constData());
        }

        try
        {
            m_hackTvLib->start(argc, argv.data());
            executeButton->setText("Stop");
        }
        catch (const std::exception& e) {
            QMessageBox::critical(this, "Error", QString("HackTvLib error: %1").arg(e.what()));
        }

        // Clean up allocated memory
        for(char* arg : argv) {
            free(arg);
        }
    }
    else if (executeButton->text() == "Stop")
    {
        // Stop the command
        try
        {
            m_hackTvLib->stop();  // Assuming stop is the method to stop the command in HackTvLib
            executeButton->setText("Start");  // Change button text to "Start"
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
        args << "-a";
    }

    if (!gainEdit->text().isEmpty()) {
        args << "-g" << gainEdit->text();
    }

    args << "-f" << frequencyEdit->text()
         << "-s" << sampleRateEdit->text()
         << "-m" << modeCombo->currentText();

    // Add the input type and file
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
        args << inputFileEdit->text();
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
