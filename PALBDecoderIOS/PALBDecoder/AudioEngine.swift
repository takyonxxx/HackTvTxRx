import AVFoundation

final class AudioEngine {
    private let engine = AVAudioEngine()
    private let playerNode = AVAudioPlayerNode()
    private let format: AVAudioFormat
    private var isRunning = false

    // Ring buffer for audio samples
    private let bufferLock = NSLock()
    private var sampleBuffer: [Float] = []
    private let maxBufferSize = 480000 // 10s @ 48kHz
    private let chunkSize = 480 // 10ms @ 48kHz

    var volume: Float = 0.1 {
        didSet { playerNode.volume = volume }
    }

    init() {
        format = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 2)!
        setupAudioSession()
        setupEngine()
    }

    deinit {
        stop()
    }

    private func setupAudioSession() {
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .default, options: [])
            try session.setPreferredSampleRate(48000)
            try session.setPreferredIOBufferDuration(0.01)
            try session.setActive(true)
        } catch {
            print("[Audio] Session setup error: \(error)")
        }
    }

    private func setupEngine() {
        engine.attach(playerNode)
        engine.connect(playerNode, to: engine.mainMixerNode, format: format)
        playerNode.volume = volume
    }

    func start() {
        guard !isRunning else { return }
        do {
            try engine.start()
            playerNode.play()
            isRunning = true
            scheduleBuffers()
            print("[Audio] Engine started")
        } catch {
            print("[Audio] Engine start error: \(error)")
        }
    }

    func stop() {
        guard isRunning else { return }
        playerNode.stop()
        engine.stop()
        isRunning = false
    }

    func enqueueAudio(_ samples: UnsafePointer<Float>, count: Int) {
        bufferLock.lock()
        defer { bufferLock.unlock() }

        // Append mono samples
        let newSamples = Array(UnsafeBufferPointer(start: samples, count: count))
        sampleBuffer.append(contentsOf: newSamples)

        // Overflow protection
        if sampleBuffer.count > maxBufferSize {
            sampleBuffer.removeFirst(sampleBuffer.count - maxBufferSize)
        }
    }

    private func scheduleBuffers() {
        guard isRunning else { return }

        // Schedule next buffer
        let framesToSchedule = AVAudioFrameCount(chunkSize)
        guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: framesToSchedule) else { return }
        buffer.frameLength = framesToSchedule

        bufferLock.lock()
        let available = sampleBuffer.count
        let samplesToRead = min(available, chunkSize)
        bufferLock.unlock()

        guard let leftChannel = buffer.floatChannelData?[0],
              let rightChannel = buffer.floatChannelData?[1] else { return }

        if samplesToRead >= chunkSize {
            bufferLock.lock()
            for i in 0..<chunkSize {
                let sample = sampleBuffer[i]
                leftChannel[i] = sample
                rightChannel[i] = sample
            }
            sampleBuffer.removeFirst(chunkSize)
            bufferLock.unlock()
        } else {
            // Silence when no data
            memset(leftChannel, 0, chunkSize * MemoryLayout<Float>.size)
            memset(rightChannel, 0, chunkSize * MemoryLayout<Float>.size)
        }

        playerNode.scheduleBuffer(buffer, completionCallbackType: .dataConsumed) { [weak self] _ in
            self?.scheduleBuffers()
        }
    }
}
