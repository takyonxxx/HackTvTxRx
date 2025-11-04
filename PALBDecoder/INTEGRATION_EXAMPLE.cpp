/*
 * EXAMPLE INTEGRATION WITH YOUR EXISTING HACKRF CODE
 * 
 * This shows exactly how to connect the PAL decoder to your existing
 * HackRF callback system.
 */

#include "MainWindow.h"

// =============================================================================
// OPTION 1: Using your existing callback (EASIEST)
// =============================================================================

void MainWindow::setupHackRF()
{
    // Your existing HackRF initialization code
    // m_hackTvLib = new YourHackRFLibrary();
    
    // Configure for PAL-B reception
    m_hackTvLib->setFrequency(486000000);   // 486 MHz (example channel)
    m_hackTvLib->setSampleRate(16000000);   // 16 MHz - CRITICAL!
    m_hackTvLib->setGain(20, 20, 20);       // RF, IF, BB gains
    
    // Your EXISTING callback code - just call handleReceivedData()!
    m_hackTvLib->setReceivedDataCallback([this](const int8_t* data, size_t len) {
        if (!m_shuttingDown.load() && this && m_hackTvLib && data && len == 262144) {
            // Copy data to avoid dangling pointer (your existing code)
            QByteArray dataCopy(reinterpret_cast<const char*>(data), len);
            
            // Queue to main thread (your existing code)
            QMetaObject::invokeMethod(this, [this, dataCopy]() {
                if (this && !m_shuttingDown.load()) {
                    // *** THIS IS THE ONLY LINE YOU ADD ***
                    handleReceivedData(
                        reinterpret_cast<const int8_t*>(dataCopy.data()), 
                        dataCopy.size()
                    );
                    // handleReceivedData() is already implemented in MainWindow.cpp!
                }
            }, Qt::QueuedConnection);
        }
    });
    
    // Start receiving
    m_hackTvLib->start();
}

// =============================================================================
// OPTION 2: If you already have complex<float> samples
// =============================================================================

void MainWindow::onHackRFComplexData(const std::vector<std::complex<float>>& samples)
{
    // If your HackRF library already converts to complex<float>
    handleSamples(samples);
}

// =============================================================================
// OPTION 3: Processing in chunks
// =============================================================================

void MainWindow::onHackRFDataChunk(const int8_t* data, size_t len)
{
    // Process data in smaller chunks if needed
    const size_t CHUNK_SIZE = 8192; // Adjust as needed
    
    for (size_t offset = 0; offset < len; offset += CHUNK_SIZE) {
        size_t remaining = len - offset;
        size_t chunkLen = std::min(CHUNK_SIZE, remaining);
        
        handleReceivedData(data + offset, chunkLen);
    }
}

// =============================================================================
// OPTION 4: Thread-safe queue (if you need buffering)
// =============================================================================

#include <queue>
#include <mutex>

class MainWindow : public QMainWindow
{
    // ... your existing code ...
    
private:
    std::queue<QByteArray> m_dataQueue;
    std::mutex m_queueMutex;
    QTimer* m_processTimer;
    
    void setupProcessTimer()
    {
        m_processTimer = new QTimer(this);
        connect(m_processTimer, &QTimer::timeout, this, &MainWindow::processQueue);
        m_processTimer->start(10); // Process every 10ms
    }
    
    void enqueueData(const int8_t* data, size_t len)
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_dataQueue.push(QByteArray(reinterpret_cast<const char*>(data), len));
        
        // Limit queue size to prevent memory issues
        while (m_dataQueue.size() > 10) {
            m_dataQueue.pop(); // Drop oldest
        }
    }
    
    void processQueue()
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        
        while (!m_dataQueue.empty()) {
            QByteArray data = m_dataQueue.front();
            m_dataQueue.pop();
            
            handleReceivedData(
                reinterpret_cast<const int8_t*>(data.data()), 
                data.size()
            );
        }
    }
};

// Then in your HackRF callback:
m_hackTvLib->setReceivedDataCallback([this](const int8_t* data, size_t len) {
    if (!m_shuttingDown.load() && data && len > 0) {
        enqueueData(data, len);
    }
});

// =============================================================================
// OPTION 5: Direct integration without callback wrapper
// =============================================================================

void MainWindow::hackrfDirectCallback(hackrf_transfer* transfer)
{
    if (!transfer || !transfer->buffer) return;
    
    // HackRF buffer contains int8_t IQ pairs
    const int8_t* data = reinterpret_cast<const int8_t*>(transfer->buffer);
    size_t len = transfer->valid_length;
    
    // Process directly
    if (m_palDecoder) {
        m_palDecoder->processSamples(data, len);
    }
}

// =============================================================================
// COMPLETE EXAMPLE: Full MainWindow integration
// =============================================================================

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr)
        : QMainWindow(parent)
        , m_shuttingDown(false)
    {
        // Create PAL decoder
        m_palDecoder = std::make_unique<PALDecoder>(this);
        
        // Connect signals
        connect(m_palDecoder.get(), &PALDecoder::frameReady,
                this, &MainWindow::onFrameReady, Qt::QueuedConnection);
        
        // Setup UI (already implemented)
        setupUI();
        
        // Initialize HackRF
        initHackRF();
    }
    
    ~MainWindow()
    {
        m_shuttingDown = true;
        
        // Stop HackRF
        if (m_hackTvLib) {
            m_hackTvLib->stop();
        }
    }

private:
    void initHackRF()
    {
        // Your HackRF library initialization
        m_hackTvLib = new YourHackRFClass();
        
        // CRITICAL: Set to 16 MHz sample rate
        m_hackTvLib->setSampleRate(16000000);
        
        // Set frequency (PAL-B channel)
        m_hackTvLib->setFrequency(486000000); // 486 MHz
        
        // Set gains
        m_hackTvLib->setRFGain(20);
        m_hackTvLib->setIFGain(20);
        m_hackTvLib->setBBGain(20);
        
        // Set callback
        m_hackTvLib->setReceivedDataCallback([this](const int8_t* data, size_t len) {
            if (!m_shuttingDown.load() && this && data && len > 0) {
                QByteArray copy(reinterpret_cast<const char*>(data), len);
                QMetaObject::invokeMethod(this, [this, copy]() {
                    if (!m_shuttingDown.load()) {
                        handleReceivedData(
                            reinterpret_cast<const int8_t*>(copy.data()),
                            copy.size()
                        );
                    }
                }, Qt::QueuedConnection);
            }
        });
        
        // Start
        m_hackTvLib->start();
        
        qDebug() << "HackRF initialized for PAL-B reception at 486 MHz, 16 MHz sample rate";
    }
    
    std::unique_ptr<PALDecoder> m_palDecoder;
    YourHackRFClass* m_hackTvLib;
    std::atomic<bool> m_shuttingDown;
};

// =============================================================================
// DATA FORMAT NOTES
// =============================================================================

/*
HackRF Output Format:
---------------------
- Type: int8_t (signed 8-bit)
- Format: Interleaved IQ pairs: [I0, Q0, I1, Q1, I2, Q2, ...]
- Range: -128 to +127
- Sample rate: 16 MHz (for this project)
- Buffer size: Typically 262144 bytes (131072 IQ pairs)

Conversion to complex<float>:
-----------------------------
The PALDecoder automatically converts:
    I_float = int8_value / 128.0f
    Q_float = int8_value / 128.0f
    complex<float>(I_float, Q_float)

You don't need to do any conversion - just pass the raw int8_t* buffer!
*/

// =============================================================================
// FREQUENCY SETTINGS FOR PAL-B CHANNELS
// =============================================================================

/*
Common PAL-B/G UHF TV channels:
-------------------------------
Channel 21: 474 MHz
Channel 22: 482 MHz
Channel 23: 490 MHz
Channel 24: 498 MHz
...
Channel 69: 858 MHz

Formula: Frequency = 474 MHz + (channel - 21) × 8 MHz

Example for your frequency (486 MHz):
    486 = 474 + (channel - 21) × 8
    12 = (channel - 21) × 8
    1.5 = channel - 21
    channel ≈ 22.5

So 486 MHz is between channels 22 and 23.
Adjust to exact channel: 482 MHz (CH22) or 490 MHz (CH23)
*/

// =============================================================================
// TESTING WITHOUT REAL SIGNAL
// =============================================================================

void MainWindow::generateTestSignal()
{
    // Generate test pattern for debugging
    const size_t BUFFER_SIZE = 262144;
    std::vector<int8_t> testData(BUFFER_SIZE);
    
    // Generate sine wave (simulated video carrier)
    for (size_t i = 0; i < BUFFER_SIZE; i += 2) {
        float t = i / (2.0f * 16000000.0f); // Time in seconds
        float signal = std::sin(2.0f * M_PI * 1e6 * t); // 1 MHz test signal
        
        testData[i] = static_cast<int8_t>(signal * 64);     // I
        testData[i+1] = static_cast<int8_t>(signal * 64);   // Q
    }
    
    // Process test data
    handleReceivedData(testData.data(), testData.size());
}

// =============================================================================
// PERFORMANCE MONITORING
// =============================================================================

void MainWindow::monitorPerformance()
{
    static int sampleCount = 0;
    static QElapsedTimer timer;
    static bool timerStarted = false;
    
    if (!timerStarted) {
        timer.start();
        timerStarted = true;
    }
    
    sampleCount += 262144 / 2; // IQ pairs
    
    if (timer.elapsed() > 1000) {
        float samplesPerSec = sampleCount / (timer.elapsed() / 1000.0f);
        float expectedRate = 16000000.0f;
        float dropRate = (1.0f - samplesPerSec / expectedRate) * 100.0f;
        
        qDebug() << "Sample rate:" << samplesPerSec / 1e6 << "MS/s"
                 << "Expected:" << expectedRate / 1e6 << "MS/s"
                 << "Drop rate:" << dropRate << "%";
        
        sampleCount = 0;
        timer.restart();
    }
}

/*
=============================================================================
INTEGRATION CHECKLIST
=============================================================================

✓ Qt 6.9.3 installed and configured
✓ Project builds successfully
✓ HackRF library integrated
✓ Sample rate set to 16 MHz (CRITICAL!)
✓ Frequency set to PAL-B channel (e.g., 486 MHz)
✓ Gains configured (start with 20,20,20)
✓ Callback calls handleReceivedData()
✓ Video display window appears
✓ Video gain/offset controls work
✓ FPS counter shows ~25 fps

TROUBLESHOOTING:
- No video: Check HackRF connection, frequency, gains
- Low FPS: Check CPU usage, use Release build
- Garbled: Wrong frequency or weak signal
- Crashes: Check thread safety, Qt::QueuedConnection

NEXT STEPS:
1. Test with real PAL-B signal
2. Adjust gain/offset for best picture
3. Monitor FPS and sample rate
4. Add features (color, audio, sync)

=============================================================================
*/
