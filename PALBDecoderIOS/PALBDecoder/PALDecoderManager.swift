import SwiftUI
import CoreGraphics

final class PALDecoderManager: ObservableObject {
    @Published var currentFrame: CGImage?
    @Published var fps: Double = 0
    @Published var syncQuality: Float = 0
    @Published var isConnected: Bool = false
    @Published var sampleRate: Int = 16000000

    var radioMode: Bool = false

    let tcpClient = SDRTCPClient()
    let audioEngine = AudioEngine()

    private var palDecoder: PALDecoderRef?
    private var audioDemod: AudioDemodRef?

    private let videoQueue = DispatchQueue(label: "pal.video", qos: .userInteractive)
    private let audioQueue = DispatchQueue(label: "pal.audio", qos: .userInitiated)

    private var switchingMode = false
    private var lastHost: String = ""

    // Video frame accumulation
    private var videoAccumBuffer: UnsafeMutablePointer<Int8>
    private let videoAccumCapacity = 4 * 1024 * 1024
    private var videoAccumCount = 0
    private var frameSize: Int = 1280000

    private var videoProcessing = false
    private let videoLock: UnsafeMutablePointer<os_unfair_lock_s> = {
        let p = UnsafeMutablePointer<os_unfair_lock_s>.allocate(capacity: 1)
        p.initialize(to: os_unfair_lock_s())
        return p
    }()

    private var frameCount = 0
    private var fpsTimer: DispatchSourceTimer?

    private let colorSpace = CGColorSpaceCreateDeviceRGB()
    private let bitmapInfo = CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue)

    var frequency: UInt64 = 479300000

    init() {
        videoAccumBuffer = .allocate(capacity: videoAccumCapacity)
        palDecoder = palDecoder_create()
        audioDemod = audioDemod_create()

        if let dec = palDecoder {
            palDecoder_setSampleRate(dec, Int32(sampleRate))
            palDecoder_setTuneFrequency(dec, frequency)
            palDecoder_setVideoInvert(dec, 1)
        }
        if let aud = audioDemod {
            audioDemod_setSampleRate(aud, Double(sampleRate))
        }
        updateAudioCarrier()
        setupCallbacks()

        tcpClient.onDataReceived = { [weak self] ptr, len in
            self?.handleTCPData(ptr, len: len)
        }

        startFPSTimer()
        updateFrameSize()
    }

    deinit {
        fpsTimer?.cancel()
        if let dec = palDecoder { palDecoder_destroy(dec) }
        if let aud = audioDemod { audioDemod_destroy(aud) }
        videoAccumBuffer.deallocate()
        videoLock.deallocate()
    }

    private func updateFrameSize() {
        frameSize = sampleRate * 2 * 40 / 1000
    }

    // MARK: - Connection

    func connect(host: String) {
        lastHost = host
        let initialCommands = [
            "SET_SAMPLE_RATE:\(sampleRate)",
            "SET_FREQ:\(frequency)",
            "SET_LNA_GAIN:40",
            "SET_VGA_GAIN:30",
            "SET_RX_AMP_GAIN:14"
        ]
        tcpClient.connect(host: host, initialCommands: initialCommands)
        audioEngine.start()
        DispatchQueue.main.async { self.isConnected = true }
    }

    func disconnect() {
        tcpClient.disconnect()
        audioEngine.stop()
        videoAccumCount = 0
        DispatchQueue.main.async { self.isConnected = false }
    }

    // MARK: - Radio Mode Toggle (disconnect + reconnect)

    func setRadioMode(_ enabled: Bool) {
        print("[MODE] Switching to \(enabled ? "RADIO" : "TV")...")
        switchingMode = true
        radioMode = enabled

        // 1. Disconnect current TCP
        tcpClient.disconnect()

        // 2. Configure decoders on background (no data flowing now)
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self else { return }

            let newRate = enabled ? 2000000 : 16000000

            if let aud = self.audioDemod {
                audioDemod_setRadioMode(aud, enabled ? 1 : 0)
                audioDemod_setSampleRate(aud, Double(newRate))
            }
            if let dec = self.palDecoder {
                palDecoder_setSampleRate(dec, Int32(newRate))
            }

            DispatchQueue.main.async {
                self.sampleRate = newRate
                self.videoAccumCount = 0
                self.updateFrameSize()
                if enabled { self.currentFrame = nil }

                // 3. Reconnect with new sample rate
                let cmds = [
                    "SET_SAMPLE_RATE:\(newRate)",
                    "SET_FREQ:\(self.frequency)",
                    "SET_LNA_GAIN:40",
                    "SET_VGA_GAIN:30",
                    "SET_RX_AMP_GAIN:14"
                ]
                self.tcpClient.connect(host: self.lastHost, initialCommands: cmds)

                if !enabled {
                    self.updateAudioCarrier()
                }

                self.switchingMode = false
                print("[MODE] Switch complete, reconnected at \(newRate/1000000) MHz")
            }
        }
    }

    // MARK: - Sample Rate Change

    func changeSampleRate(_ newRate: Int) {
        switchingMode = true
        tcpClient.disconnect()

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self else { return }

            if let dec = self.palDecoder { palDecoder_setSampleRate(dec, Int32(newRate)) }
            if let aud = self.audioDemod { audioDemod_setSampleRate(aud, Double(newRate)) }

            DispatchQueue.main.async {
                self.sampleRate = newRate
                self.videoAccumCount = 0
                self.updateFrameSize()
                self.updateAudioCarrier()

                let cmds = [
                    "SET_SAMPLE_RATE:\(newRate)",
                    "SET_FREQ:\(self.frequency)",
                    "SET_LNA_GAIN:40",
                    "SET_VGA_GAIN:30",
                    "SET_RX_AMP_GAIN:14"
                ]
                self.tcpClient.connect(host: self.lastHost, initialCommands: cmds)
                self.switchingMode = false
                print("[RATE] Changed to \(newRate/1000000) MHz, reconnected")
            }
        }
    }

    func updateFrequency() {
        if let dec = palDecoder { palDecoder_setTuneFrequency(dec, frequency) }
        tcpClient.setFrequency(frequency)
        updateAudioCarrier()
    }

    // MARK: - Data Handling

    private func handleTCPData(_ ptr: UnsafePointer<Int8>, len: Int) {
        if switchingMode { return }

        // AUDIO: always process (no skip - radio needs continuous audio)
        let aCopy = UnsafeMutablePointer<Int8>.allocate(capacity: len)
        memcpy(aCopy, ptr, len)
        audioQueue.async { [weak self] in
            defer { aCopy.deallocate() }
            guard let self = self, !self.switchingMode, let aud = self.audioDemod else { return }
            audioDemod_processSamples(aud, aCopy, len)
        }

        // VIDEO: only in TV mode
        if radioMode { return }

        let spaceLeft = videoAccumCapacity - videoAccumCount
        let toCopy = min(len, spaceLeft)
        if toCopy > 0 {
            memcpy(videoAccumBuffer + videoAccumCount, ptr, toCopy)
            videoAccumCount += toCopy
        }

        while videoAccumCount >= frameSize {
            os_unfair_lock_lock(videoLock)
            let canVideo = !videoProcessing
            if canVideo { videoProcessing = true }
            os_unfair_lock_unlock(videoLock)

            if canVideo {
                let vCopy = UnsafeMutablePointer<Int8>.allocate(capacity: frameSize)
                memcpy(vCopy, videoAccumBuffer, frameSize)
                let fSize = frameSize
                videoQueue.async { [weak self] in
                    defer { vCopy.deallocate() }
                    guard let self = self, !self.switchingMode, let dec = self.palDecoder else {
                        if let self = self {
                            os_unfair_lock_lock(self.videoLock)
                            self.videoProcessing = false
                            os_unfair_lock_unlock(self.videoLock)
                        }
                        return
                    }
                    palDecoder_processSamples(dec, vCopy, fSize)
                    os_unfair_lock_lock(self.videoLock)
                    self.videoProcessing = false
                    os_unfair_lock_unlock(self.videoLock)
                }
            }

            let remaining = videoAccumCount - frameSize
            if remaining > 0 {
                memmove(videoAccumBuffer, videoAccumBuffer + frameSize, remaining)
            }
            videoAccumCount = remaining
        }
    }

    // MARK: - Callbacks

    private func setupCallbacks() {
        guard let dec = palDecoder, let aud = audioDemod else { return }
        let selfPtr = Unmanaged.passUnretained(self).toOpaque()

        palDecoder_setFrameCallback(dec, { rgba, width, height, ctx in
            guard let ctx = ctx, let rgba = rgba else { return }
            let mgr = Unmanaged<PALDecoderManager>.fromOpaque(ctx).takeUnretainedValue()
            mgr.handleFrame(rgba, width: Int(width), height: Int(height))
        }, selfPtr)

        palDecoder_setSyncStatsCallback(dec, { syncRate, _, _, ctx in
            guard let ctx = ctx else { return }
            let mgr = Unmanaged<PALDecoderManager>.fromOpaque(ctx).takeUnretainedValue()
            DispatchQueue.main.async { mgr.syncQuality = syncRate }
        }, selfPtr)

        audioDemod_setAudioCallback(aud, { samples, count, ctx in
            guard let ctx = ctx, let samples = samples else { return }
            let mgr = Unmanaged<PALDecoderManager>.fromOpaque(ctx).takeUnretainedValue()
            mgr.audioEngine.enqueueAudio(samples, count: Int(count))
        }, selfPtr)
    }

    private func handleFrame(_ rgba: UnsafePointer<UInt8>, width: Int, height: Int) {
        if radioMode { return }
        frameCount += 1
        let bytesPerRow = width * 4
        let dataSize = bytesPerRow * height
        guard let cfData = CFDataCreate(nil, rgba, dataSize),
              let provider = CGDataProvider(data: cfData),
              let cgImage = CGImage(
                  width: width, height: height,
                  bitsPerComponent: 8, bitsPerPixel: 32,
                  bytesPerRow: bytesPerRow,
                  space: colorSpace,
                  bitmapInfo: bitmapInfo,
                  provider: provider,
                  decode: nil, shouldInterpolate: false,
                  intent: .defaultIntent
              ) else { return }
        DispatchQueue.main.async { self.currentFrame = cgImage }
    }

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

    private func updateAudioCarrier() {
        guard let aud = audioDemod else { return }
        if radioMode { return }
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
        audioDemod_setAudioCarrierFreq(aud, (videoCarrierMHz + 5.5 - tuneMHz) * 1.0e6)
    }

    // MARK: - Settings

    func setVideoGain(_ gain: Float) { if let d = palDecoder { palDecoder_setVideoGain(d, gain) } }
    func setVideoOffset(_ offset: Float) { if let d = palDecoder { palDecoder_setVideoOffset(d, offset) } }
    func setVideoInvert(_ invert: Bool) { if let d = palDecoder { palDecoder_setVideoInvert(d, invert ? 1 : 0) } }
    func setColorMode(_ color: Bool) { if let d = palDecoder { palDecoder_setColorMode(d, color ? 1 : 0) } }
    func setChromaGain(_ gain: Float) { if let d = palDecoder { palDecoder_setChromaGain(d, gain) } }
    func setSyncThreshold(_ t: Float) { if let d = palDecoder { palDecoder_setSyncThreshold(d, t) } }
    func setAudioGain(_ gain: Float) { if let a = audioDemod { audioDemod_setAudioGain(a, gain) } }
    func setAudioEnabled(_ e: Bool) { if let a = audioDemod { audioDemod_setAudioEnabled(a, e ? 1 : 0) } }
    func setVolume(_ v: Float) { audioEngine.volume = v }
    func setLnaGain(_ g: UInt) { tcpClient.setLnaGain(g) }
    func setVgaGain(_ g: UInt) { tcpClient.setVgaGain(g) }
    func setRxAmpGain(_ g: UInt) { tcpClient.setRxAmpGain(g) }
}
