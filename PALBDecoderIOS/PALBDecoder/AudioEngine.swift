import AVFoundation

final class AudioEngine {
    private let engine = AVAudioEngine()
    private let playerNode = AVAudioPlayerNode()
    private let format: AVAudioFormat
    private var isRunning = false

    // Pre-allocated ring buffer
    private let ringCapacity = 96000  // 2 seconds @ 48kHz
    private var ring: UnsafeMutablePointer<Float>
    private var ringWritePos = 0
    private var ringReadPos = 0
    private var ringCount = 0
    private let ringLock: UnsafeMutablePointer<os_unfair_lock_s> = {
        let p = UnsafeMutablePointer<os_unfair_lock_s>.allocate(capacity: 1)
        p.initialize(to: os_unfair_lock_s())
        return p
    }()

    private let chunkSize = 480  // 10ms @ 48kHz

    var volume: Float = 0.1 {
        didSet { playerNode.volume = volume }
    }

    init() {
        format = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 2)!
        ring = .allocate(capacity: ringCapacity)
        ring.initialize(repeating: 0, count: ringCapacity)
        setupAudioSession()
        setupEngine()
    }

    deinit {
        stop()
        ring.deallocate()
        ringLock.deallocate()
    }

    private func setupAudioSession() {
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .default)
            try session.setPreferredSampleRate(48000)
            try session.setPreferredIOBufferDuration(0.01)
            try session.setActive(true)
        } catch {
            print("[Audio] Session error: \(error)")
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
            scheduleNextBuffer()
            print("[Audio] Engine started")
        } catch {
            print("[Audio] Start error: \(error)")
        }
    }

    func stop() {
        isRunning = false
        playerNode.stop()
        engine.stop()
        flush()
    }

    func flush() {
        os_unfair_lock_lock(ringLock)
        ringWritePos = 0; ringReadPos = 0; ringCount = 0
        os_unfair_lock_unlock(ringLock)
    }

    func enqueueAudio(_ samples: UnsafePointer<Float>, count: Int) {
        os_unfair_lock_lock(ringLock)
        defer { os_unfair_lock_unlock(ringLock) }

        let toWrite = min(count, ringCapacity - 1)
        // Drop oldest if full
        if ringCount + toWrite > ringCapacity {
            let drop = ringCount + toWrite - ringCapacity
            ringReadPos = (ringReadPos + drop) % ringCapacity
            ringCount -= drop
        }
        for i in 0..<toWrite {
            ring[ringWritePos] = samples[i]
            ringWritePos = (ringWritePos + 1) % ringCapacity
        }
        ringCount += toWrite
    }

    private func scheduleNextBuffer() {
        guard isRunning else { return }

        guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(chunkSize)) else { return }
        buffer.frameLength = AVAudioFrameCount(chunkSize)

        guard let left = buffer.floatChannelData?[0],
              let right = buffer.floatChannelData?[1] else { return }

        os_unfair_lock_lock(ringLock)
        let available = ringCount
        if available >= chunkSize {
            for i in 0..<chunkSize {
                let s = ring[ringReadPos]
                left[i] = s
                right[i] = s
                ringReadPos = (ringReadPos + 1) % ringCapacity
            }
            ringCount -= chunkSize
        } else {
            // Silence
            memset(left, 0, chunkSize * 4)
            memset(right, 0, chunkSize * 4)
        }
        os_unfair_lock_unlock(ringLock)

        playerNode.scheduleBuffer(buffer, completionCallbackType: .dataConsumed) { [weak self] _ in
            self?.scheduleNextBuffer()
        }
    }
}
