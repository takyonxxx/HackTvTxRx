#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QTimer>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QMutex>
#include <QElapsedTimer>
#include <atomic>
#include <memory>
#include "PALDecoder.h"

// Forward declaration for your HackTvLib
class HackTvLib;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // Your HackRF callback integration
    void handleSamples(const std::vector<std::complex<float>>& samples);
    void handleReceivedData(const int8_t* data, size_t len);

private slots:
    void onFrameReady(const QImage& frame);
    void onVideoGainChanged(int value);
    void onVideoOffsetChanged(int value);
    void onLnaGainChanged(int value);
    void onVgaGainChanged(int value);
    void onRxAmpGainChanged(int value);
    void updateStatus();
    void toggleHackRF();

private:
    void setupUI();
    void setupControls();
    void initHackRF();
    
    // UI Components
    QLabel* m_videoLabel;
    QLabel* m_statusLabel;
    QLabel* m_fpsLabel;
    QSlider* m_videoGainSlider;
    QSlider* m_videoOffsetSlider;
    QDoubleSpinBox* m_videoGainSpinBox;
    QDoubleSpinBox* m_videoOffsetSpinBox;
    
    // HackRF Gain Controls
    QSpinBox* m_lnaGainSpinBox;
    QSpinBox* m_vgaGainSpinBox;
    QSpinBox* m_rxAmpGainSpinBox;
    QSlider* m_lnaGainSlider;
    QSlider* m_vgaGainSlider;
    QSlider* m_rxAmpGainSlider;
    
    QPushButton* m_startStopButton;
    
    // PAL Decoder
    std::unique_ptr<PALDecoder> m_palDecoder;
    
    // HackRF Library (your library)
    std::unique_ptr<HackTvLib> m_hackTvLib;
    
    // Status tracking
    QTimer* m_statusTimer;
    int m_frameCount;
    QElapsedTimer m_fpsTimer;
    
    // Thread safety
    std::atomic<bool> m_shuttingDown;
    std::atomic<bool> m_hackRfRunning;
    
    // Current frame
    QImage m_currentFrame;
    QMutex m_frameMutex;
    
    // Settings
    static constexpr uint64_t FREQ = 478000000;  // 478 MHz (from your code)
    static constexpr uint32_t SAMP_RATE = 16000000;  // 16 MHz
};

#endif // MAINWINDOW_H
