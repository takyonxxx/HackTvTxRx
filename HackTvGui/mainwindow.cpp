#include "mainwindow.h"
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
}

void MainWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    outputEdit = new QLineEdit(this);
    outputEdit->setPlaceholderText("Output device (e.g., hackrf)");
    mainLayout->addWidget(outputEdit);

    frequencyEdit = new QLineEdit(this);
    frequencyEdit->setPlaceholderText("Frequency (e.g., 471250000)");
    mainLayout->addWidget(frequencyEdit);

    sampleRateEdit = new QLineEdit(this);
    sampleRateEdit->setPlaceholderText("Sample rate (e.g., 16e6)");
    mainLayout->addWidget(sampleRateEdit);

    modeCombo = new QComboBox(this);
    modeCombo->addItems({"pal", "ntsc", "secam"}); // Add more modes as needed
    mainLayout->addWidget(modeCombo);

    inputFileEdit = new QLineEdit(this);
    inputFileEdit->setPlaceholderText("Input file path");
    mainLayout->addWidget(inputFileEdit);

    executeButton = new QPushButton("Execute", this);
    mainLayout->addWidget(executeButton);

    setCentralWidget(centralWidget);

    connect(executeButton, &QPushButton::clicked, this, &MainWindow::executeCommand);

    process = new QProcess(this);
    connect(process, &QProcess::errorOccurred, this, &MainWindow::handleError);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::handleFinished);
}

void MainWindow::executeCommand()
{
    QString command = buildCommand();
    process->start("hacktv.exe", command.split(" "));
}

void MainWindow::handleError(QProcess::ProcessError error)
{
    QMessageBox::critical(this, "Error", "An error occurred: " + process->errorString());
}

void MainWindow::handleFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        QMessageBox::information(this, "Success", "Command executed successfully.");
    } else {
        QMessageBox::warning(this, "Warning", "Command finished with exit code: " + QString::number(exitCode));
    }
}

QString MainWindow::buildCommand()
{
    QString command = "-o " + outputEdit->text() +
                      " -f " + frequencyEdit->text() +
                      " -s " + sampleRateEdit->text() +
                      " -m " + modeCombo->currentText() +
                      " " + inputFileEdit->text();
    return command;
}
