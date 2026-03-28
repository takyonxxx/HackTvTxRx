import SwiftUI
import CoreGraphics

final class PALDecoderManager: ObservableObject {
    @Published var currentFrame: CGImage?
    @Published var fps: Double = 0
    @Published var syncQuality: Float = 0
    @Published var isProcessing = false

    let tcpClient = SDRTCPClient()
    let audioEngine = AudioEngine()

    private var palDecoder: PALDecoderRef?
    private var audioDemod: AudioDemodRef?
    private let processingQueue = DispatchQueue(label: "pal.processing", qos: .userInteractive)
    private let audioProcessingQueue = DispatchQueue(label: "pal.audio", qos: .userInitiated)

    private var frameCount = 0
    private var fpsTimer: DispatchSourceTimer?
    private var lastFrameTime = CFAbsoluteTimeGetCurrent()

    // Frame buffer accumulation
    private var iqBuffer = Data()
    private var frameSize: Int = 0  // bytes per PAL frame (40ms worth)
    private let bufferLock = NSLock()

    // Settings
    var sampleRate: Int = 16000000 {
        didSet { updateSampleRate() }
    }
    var frequency: UInt64 = 479300000 {
        didSet { updateFrequency() }
    }

    init() {
        palDecoder = palDecoder_create()
        audioDemod = audioDemod_create()

        updateSampleRate()
        updateFrequency()
        setupCallbacks()

        tcpClient.onDataReceived = { [weak self] data in
            self?.handleData(data)
        }

        startFPSTimer()
    }

    deinit {
        fpsTimer?.cancel()
        if let dec = palDecoder { palDecoder_destroy(dec) }
        if let aud = audioDemod { audioDemod_destroy(aud) }
    }

    // MARK: - Connection

    func connect(host: String) {
        tcpClient.connect(host: host)
        audioEngine.start()
        DispatchQueue.main.async { self.isProcessing = true }
    }

    func disconnect() {
        tcpClient.disconnect()
        audioEngine.stop()
        DispatchQueue.main.async { self.isProcessing = false }
    }

    // MARK: - Control

    func updateFrequency() {
        if let dec = palDecoder { palDecoder_setTuneFrequency(dec, frequency) }
        tcpClient.setFrequency(frequency)
        updateAudioCarrier()
    }

    func updateSampleRate() {
        // Frame size = samples_per_second * 2 bytes * 0.04s (40ms = 1 PAL frame)
        frameSize = sampleRate * 2 * 40 / 1000  // e.g. 16M * 2 * 0.04 = 1,280,000
        if let dec = palDecoder { palDecoder_setSampleRate(dec, Int32(sampleRate)) }
        if let aud = audioDemod { audioDemod_setSampleRate(aud, Double(sampleRate)) }
        tcpClient.setSampleRate(UInt32(sampleRate))
        updateAudioCarrier()
    }

    private func updateAudioCarrier() {
        guard let aud = audioDemod else { return }
        let tuneMHz = Double(frequency) / 1.0e6
        var videoCarrierMHz: Double
        if tuneMHz >= 470.0 && tuneMHz <= 862.0 {
            let ch = Int(floor((tuneMHz - 470.0 - 0.001) / 8.0))
            videoCarrierMHz = 470.0 + Double(max(ch, 0)) * 8.0 + 1.25
        } else if tuneMHz >= 174.0 && tuneMHz <= 230.0 {
            let ch = Int(floor((tuneMHz - 174.0 - 0.001) / 8.0))
            videoCarrierMHz = 174.0 + Double(max(ch, 0)) * 8.0 + 1.25
        } else {
            videoCarrierMHz = tuneMHz
        }
        let audioCarrierHz = (videoCarrierMHz + 5.5 - tuneMHz) * 1.0e6
        audioDemod_setAudioCarrierFreq(aud, audioCarrierHz)
    }

    // MARK: - Callbacks

    private func setupCallbacks() {
        guard let dec = palDecoder, let aud = audioDemod else { return }

        let selfPtr = Unmanaged.passUnretained(self).toOpaque()

        palDecoder_setFrameCallback(dec, { rgba, width, height, context in
            guard let context = context else { return }
            let manager = Unmanaged<PALDecoderManager>.fromOpaque(context).takeUnretainedValue()
            manager.handleFrame(rgba!, width: Int(width), height: Int(height))
        }, selfPtr)

        palDecoder_setSyncStatsCallback(dec, { syncRate, _, _, context in
            guard let context = context else { return }
            let manager = Unmanaged<PALDecoderManager>.fromOpaque(context).takeUnretainedValue()
            DispatchQueue.main.async { manager.syncQuality = syncRate }
        }, selfPtr)

        audioDemod_setAudioCallback(aud, { samples, count, context in
            guard let context = context, let samples = samples else { return }
            let manager = Unmanaged<PALDecoderManager>.fromOpaque(context).takeUnretainedValue()
            manager.audioEngine.enqueueAudio(samples, count: Int(count))
        }, selfPtr)
    }

    // MARK: - Data Processing

    private func handleData(_ data: Data) {
        bufferLock.lock()
        iqBuffer.append(data)
        bufferLock.unlock()

        // Process accumulated frames
        processBufferedData()
    }

    private func processBufferedData() {
        guard frameSize > 0 else { return }

        bufferLock.lock()
        guard iqBuffer.count >= frameSize else {
            bufferLock.unlock()
            return
        }
        // Take one frame worth of data
        let frameData = iqBuffer.prefix(frameSize)
        iqBuffer.removeFirst(frameSize)
        bufferLock.unlock()

        // Video processing
        processingQueue.async { [weak self] in
            guard let self = self, let dec = self.palDecoder else { return }
            frameData.withUnsafeBytes { rawBuffer in
                guard let ptr = rawBuffer.baseAddress?.assumingMemoryBound(to: Int8.self) else { return }
                palDecoder_processSamples(dec, ptr, frameData.count)
            }
        }

        // Audio processing (parallel)
        audioProcessingQueue.async { [weak self] in
            guard let self = self, let aud = self.audioDemod else { return }
            frameData.withUnsafeBytes { rawBuffer in
                guard let ptr = rawBuffer.baseAddress?.assumingMemoryBound(to: Int8.self) else { return }
                audioDemod_processSamples(aud, ptr, frameData.count)
            }
        }

        // Process remaining frames if any
        bufferLock.lock()
        let hasMore = iqBuffer.count >= frameSize
        bufferLock.unlock()
        if hasMore { processBufferedData() }
    }

    // MARK: - Frame Rendering

    private func handleFrame(_ rgba: UnsafePointer<UInt8>, width: Int, height: Int) {
        frameCount += 1
        let dataSize = width * height * 4
        let data = Data(bytes: rgba, count: dataSize)

        guard let provider = CGDataProvider(data: data as CFData),
              let cgImage = CGImage(
                  width: width, height: height,
                  bitsPerComponent: 8, bitsPerPixel: 32,
                  bytesPerRow: width * 4,
                  space: CGColorSpaceCreateDeviceRGB(),
                  bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                  provider: provider,
                  decode: nil, shouldInterpolate: false,
                  intent: .defaultIntent
              ) else { return }

        DispatchQueue.main.async {
            self.currentFrame = cgImage
        }
    }

    // MARK: - FPS Timer

    private func startFPSTimer() {
        fpsTimer = DispatchSource.makeTimerSource(queue: .main)
        fpsTimer?.schedule(deadline: .now() + 1, repeating: 1)
        fpsTimer?.setEventHandler { [weak self] in
            guard let self = self else { return }
            self.fps = Double(self.frameCount)
            self.frameCount = 0
        }
        fpsTimer?.resume()
    }

    // MARK: - Settings Accessors

    func setVideoGain(_ gain: Float) {
        if let dec = palDecoder { palDecoder_setVideoGain(dec, gain) }
    }
    func setVideoOffset(_ offset: Float) {
        if let dec = palDecoder { palDecoder_setVideoOffset(dec, offset) }
    }
    func setVideoInvert(_ invert: Bool) {
        if let dec = palDecoder { palDecoder_setVideoInvert(dec, invert ? 1 : 0) }
    }
    func setColorMode(_ color: Bool) {
        if let dec = palDecoder { palDecoder_setColorMode(dec, color ? 1 : 0) }
    }
    func setChromaGain(_ gain: Float) {
        if let dec = palDecoder { palDecoder_setChromaGain(dec, gain) }
    }
    func setSyncThreshold(_ threshold: Float) {
        if let dec = palDecoder { palDecoder_setSyncThreshold(dec, threshold) }
    }
    func setAudioGain(_ gain: Float) {
        if let aud = audioDemod { audioDemod_setAudioGain(aud, gain) }
    }
    func setAudioEnabled(_ enabled: Bool) {
        if let aud = audioDemod { audioDemod_setAudioEnabled(aud, enabled ? 1 : 0) }
    }
    func setVolume(_ volume: Float) {
        audioEngine.volume = volume
    }
    func setLnaGain(_ gain: UInt) { tcpClient.setLnaGain(gain) }
    func setVgaGain(_ gain: UInt) { tcpClient.setVgaGain(gain) }
    func setRxAmpGain(_ gain: UInt) { tcpClient.setRxAmpGain(gain) }
}
