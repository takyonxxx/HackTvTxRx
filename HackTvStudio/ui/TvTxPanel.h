#ifndef TVTXPANEL_H
#define TVTXPANEL_H

#include "ModePanel.h"
#include "tv/TvTxEncoder.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>
#include <QMediaDevices>

class VideoView;

/*
 * Analogue TV transmitter: hacktv's encoder fed by the built-in test pattern
 * or a video file (decoded by Qt Multimedia). The main START button transmits.
 */
class TvTxPanel : public ModePanel
{
    Q_OBJECT
public:
    explicit TvTxPanel(QWidget* parent = nullptr);

    QString    title() const override { return "TV TX"; }
    bool       isTransmit() const override { return true; }
    ITxSource* txSource() override;
    uint32_t   preferredSampleRate() const override { return 16000000; }
    QLayout*   spectrumSlot() override { return m_specSlot; }

    void onDeviceStarted() override;
    void onDeviceStopped() override;
    void saveSettings(QSettings& s) override;
    void loadSettings(QSettings& s) override;

private slots:
    void updateStatus();

private:
    void buildUi();
    void browseFile();
    void refreshDevices();
    QByteArray cameraAudioId() const;
    void updateSourceWidgets();
    TvTxConfig collectConfig() const;

    TvTxEncoder* m_enc;

    QComboBox* m_modeCombo = nullptr;
    QRadioButton* m_testRadio = nullptr;
    QRadioButton* m_fileRadio = nullptr;
    QRadioButton* m_cameraRadio = nullptr;
    QComboBox* m_cameraCombo = nullptr;
    QComboBox* m_camAudioCombo = nullptr;
    QMediaDevices* m_devices = nullptr;
    QLineEdit* m_fileEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QCheckBox* m_loopCheck = nullptr;

    QCheckBox* m_filterCheck = nullptr;
    QCheckBox* m_colourCheck = nullptr;
    QCheckBox* m_audioCheck = nullptr;
    QCheckBox* m_a2Check = nullptr;
    QCheckBox* m_nicamCheck = nullptr;
    QCheckBox* m_acpCheck = nullptr;
    QCheckBox* m_vitsCheck = nullptr;
    QCheckBox* m_vitcCheck = nullptr;
    QCheckBox* m_invertCheck = nullptr;
    QComboBox* m_wssCombo = nullptr;
    QComboBox* m_scrambleCombo = nullptr;
    QLineEdit* m_teletextEdit = nullptr;
    QDoubleSpinBox* m_levelSpin = nullptr;

    VideoView* m_preview = nullptr;
    QVBoxLayout* m_specSlot = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTimer* m_statusTimer = nullptr;
    bool m_running = false;
};

#endif // TVTXPANEL_H
