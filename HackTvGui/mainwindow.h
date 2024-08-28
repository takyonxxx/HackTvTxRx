#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextBrowser>
#include <QTimer>
#include "hacktvlib.h"

class QLineEdit;
class QComboBox;
class QPushButton;
class QFileDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);

private:
    QLineEdit *outputEdit;
    QLineEdit *frequencyEdit;
    QLineEdit *sampleRateEdit;
    QComboBox *modeCombo;
    QLineEdit *inputFileEdit;
    QPushButton *chooseFileButton;
    QPushButton *executeButton;
    QFileDialog *fileDialog;
    std::unique_ptr<HackTvLib> m_hackTvLib;

    QTextBrowser *logBrowser;
    QTimer *logTimer;
    QStringList pendingLogs;

    void setupUi();
    QStringList buildCommand();
    void handleLog(const std::string& logMessage);

private slots:
    void executeCommand();
    void chooseFile();
    void updateLogDisplay();
};

#endif // MAINWINDOW_H
