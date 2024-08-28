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
    setFixedSize(300, 600);

    try {
        m_hackTvLib = std::make_unique<HackTvLib>();
        m_hackTvLib->setLogCallback([this](const std::string& msg) {
            handleLog(msg);
        });
        qDebug() << "HackTvLib created successfully";
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
    QHBoxLayout *outputLayout = new QHBoxLayout(outputGroup);
    outputEdit = new QLineEdit(this);
    outputEdit->setText("hackrf");
    outputLayout->addWidget(outputEdit);
    mainLayout->addWidget(outputGroup);

    // Frequency group
    QGroupBox *freqGroup = new QGroupBox("Frequency (Hz)", this);
    QHBoxLayout *freqLayout = new QHBoxLayout(freqGroup);
    frequencyEdit = new QLineEdit(this);
    frequencyEdit->setText("471250000");
    freqLayout->addWidget(frequencyEdit);
    mainLayout->addWidget(freqGroup);

    // Sample rate group
    QGroupBox *sampleGroup = new QGroupBox("Sample Rate (Mhz)", this);
    QHBoxLayout *sampleLayout = new QHBoxLayout(sampleGroup);
    sampleRateEdit = new QLineEdit(this);
    sampleRateEdit->setText("16");
    sampleLayout->addWidget(sampleRateEdit);
    mainLayout->addWidget(sampleGroup);

    // Mode group
    QGroupBox *modeGroup = new QGroupBox("Mode", this);
    QHBoxLayout *modeLayout = new QHBoxLayout(modeGroup);
    modeCombo = new QComboBox(this);
    modeCombo->addItems({"pal", "ntsc", "secam"}); // Add more modes as needed
    modeLayout->addWidget(modeCombo);
    mainLayout->addWidget(modeGroup);

    // Input file group
    QGroupBox *inputGroup = new QGroupBox("Input File", this);
    QHBoxLayout *inputLayout = new QHBoxLayout(inputGroup);
    inputFileEdit = new QLineEdit(this);
    chooseFileButton = new QPushButton("Choose File", this);
    inputLayout->addWidget(inputFileEdit);
    inputLayout->addWidget(chooseFileButton);
    mainLayout->addWidget(inputGroup);

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
    args << "-o" << outputEdit->text()
         << "-f" << frequencyEdit->text()
         << "-s" << sampleRateEdit->text()
         << "-m" << modeCombo->currentText()
         << inputFileEdit->text();
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
