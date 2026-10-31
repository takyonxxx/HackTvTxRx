#ifndef STUDIOWINDOW_H
#define STUDIOWINDOW_H

#include <QMainWindow>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVector>
#include <vector>

#include "core/DeviceController.h"
#include "core/RxEngine.h"
#include "core/TxEngine.h"
#include "core/BandPlan.h"

class CPlotter;
class CFreqCtrl;
class CMeter;
class AnalogMeter;
class ModePanel;
class QSplitter;

/*
 * Main window: persistent top strip (frequency dial, band/mode label,
 * S-meter, device controls), the spectrum/waterfall, and one tab per mode.
 */
class StudioWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit StudioWindow(QWidget* parent = nullptr);
    ~StudioWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onFrequencyEdited(qint64 hz);
    void onPlotterDemodFreq(qint64 freq, qint64 delta);
    void onPlotterWheel(int direction);
    void onSampleRateSelected(int index);
    void onStartStop();
    void onTabChanged(int index);
    void onSpectrum(const QVector<float>& bins, float levelDbfs);
    void onDeviceState(DeviceController::State state);
    void onDeviceError(const QString& message);
    void onRecordIq();
    void onRecordingFinished(const QString& path, qint64 bytes);
    void updateStatusBar();

private:
    void buildUi();
    QWidget* buildTopStrip();
    QWidget* buildDeviceGroup();
    void     openSettingsDialog();   // Android: advanced controls live in a separate screen
    void addPanel(ModePanel* panel);

    void setFrequency(uint64_t hz, bool fromDial);
    void updateBandLabel();
    void autoSelectMode(const BandInfo& band);
    void startDevice();
    void stopDevice();
    void applyGainsLive();
    RxGains currentGains() const;
    TxGains currentTxGains() const;

    void saveSettings();
    void loadSettings();
    void placeSpectrumOnTop(const QByteArray& state = QByteArray());
    void dockSpectrum(ModePanel* panel);

    // Core
    DeviceController* m_device;
    RxEngine* m_engine;
    TxEngine* m_txEngine;

    // Top strip
    CFreqCtrl* m_freqCtrl = nullptr;
    QLabel* m_bandLabel = nullptr;
    QLabel* m_modeLabel = nullptr;
    QCheckBox* m_autoModeCheck = nullptr;
    CMeter* m_meter = nullptr;
    AnalogMeter* m_analogMeter = nullptr;     // Android: needle S-meter at the right edge of the spectrum
    class QGroupBox* m_meterGroup = nullptr;   // Android: docked above the spectrum, not in the top strip
    QLabel* m_meterInfo = nullptr;
    QLabel* m_overloadLabel = nullptr;
    QPushButton* m_startButton = nullptr;
    QComboBox* m_sampleRateCombo = nullptr;
    QSlider* m_lnaSlider = nullptr;
    QSpinBox* m_lnaSpin = nullptr;
    QSlider* m_vgaSlider = nullptr;
    QSpinBox* m_vgaSpin = nullptr;
    QSlider* m_rxAmpSlider = nullptr;
    QSpinBox* m_rxAmpSpin = nullptr;
    QCheckBox* m_ampCheck = nullptr;
    QSlider* m_txVgaSlider = nullptr;
    QSpinBox* m_txVgaSpin = nullptr;
    QPushButton* m_recButton = nullptr;
    QDialog*     m_settingsDlg = nullptr;    // Android settings screen (rate, RX gains)
    QPushButton* m_settingsBtn = nullptr;
    QPushButton* m_stepUpBtn = nullptr;      // Android: big finger +/- tuning, step follows the channel width
    QPushButton* m_stepDownBtn = nullptr;
    QComboBox*   m_themeCombo = nullptr;
    qint64 fingerStepHz() const;
    void   refreshStepLabels();
    void   applyTheme(const QString& id);

    // Spectrum
    CPlotter* m_plotter = nullptr;
    QSlider* m_plotterRange = nullptr;
    QSplitter* m_splitter = nullptr;
    QWidget* m_specWidget = nullptr;
    bool m_specDocked = false;
    QByteArray m_splitterStateV;

    // Modes
    QTabWidget* m_tabs = nullptr;
    std::vector<ModePanel*> m_panels;
    ModePanel* m_active = nullptr;
    int m_radioRxTab = -1;
    int m_tvRxTab = -1;

    // Status bar
    QLabel* m_stateLabel = nullptr;
    QLabel* m_overflowLabel = nullptr;
    QLabel* m_panelStatusLabel = nullptr;
    QTimer* m_statusTimer = nullptr;

#ifdef Q_OS_ANDROID
    uint64_t m_frequency = 100000000ULL;   // radio-only head unit: FM broadcast
    uint32_t m_sampleRate = 2000000;
#else
    uint64_t m_frequency = 479250000ULL;   // desktop: TV + radio (original defaults)
    uint32_t m_sampleRate = 16000000;
#endif
    bool m_switchingTab = false;
};

#endif // STUDIOWINDOW_H
