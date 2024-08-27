#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>

class QLineEdit;
class QComboBox;
class QPushButton;

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
    QPushButton *executeButton;
    QProcess *process;

    void setupUi();
    QString buildCommand();

private slots:
    void executeCommand();
    void handleError(QProcess::ProcessError error);
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
};

#endif // MAINWINDOW_H
