import SwiftUI
import CoreGraphics

final class PALDecoderManager: ObservableObject {
    @Published var currentFrame: CGImage?
    @Published var fps: Double = 0
    @Published var syncQuality: Float = 0
    @Published var isConnected: Bool = false
    @Published var sampleRate: Int = 12500000
    @Published var bufferStatus: String = ""

    var radioMode: Bool = false

    let tcpClient = SDRTCPClient()
    let audioEngine = AudioEngine()

    private var palDecoder: PALDecoderRef?
    private var audioDemod: AudioDemodRef?

    private let videoQueue = DispatchQueue(label: "pal.video", qos: .userInteractive)
    private let audioQueue = DispatchQueue(label: "pal.audio", qos: .userInitiated)

    private var switchingMode = false
    private var lastHost: String = ""
    // Bytes to discard after mode/rate switch (flush stale server buffer)
    private var bytesToDiscard = 0

    // Video frame accumulation
    private var videoAccumBuffer: UnsafeMutablePointer<Int8>
    private let videoAccumCapacity = 4 * 1024 * 1024
    private var videoAccumCount = 0
    private var frameSize: Int = 1000000

    private var videoProcessing = false
    private let videoLock: UnsafeMutablePointer<os_unfair_lock_s> = {
        let p = UnsafeMutablePointer<os_unfair_lock_s>.allocate(capacity: 1)
        p.initialize(to: os_unfair_lock_s()); return p
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

        if let d = palDecoder { palDecoder_setSampleRate(d, Int32(sampleRate)); palDecoder_setTuneFrequency(d, frequency); palDecoder_setVideoInvert(d, 1) }
        if let a = audioDemod { audioDemod_setSampleRate(a, Double(sampleRate)) }
        updateAudioCarrier(); setupCallbacks()
        tcpClient.onDataReceived = { [weak self] ptr, len in self?.handleTCPData(ptr, len: len) }
        startFPSTimer(); updateFrameSize()
    }

    deinit {
        fpsTimer?.cancel()
        if let d = palDecoder { palDecoder_destroy(d) }
        if let a = audioDemod { audioDemod_destroy(a) }
        videoAccumBuffer.deallocate(); videoLock.deallocate()
    }

    private func updateFrameSize() { frameSize = sampleRate * 2 * 40 / 1000 }

    func connect(host: String) {
        lastHost = host
        tcpClient.connect(host: host, initialCommands: [
            "SET_SAMPLE_RATE:\(sampleRate)", "SET_FREQ:\(frequency)",
            "SET_LNA_GAIN:40", "SET_VGA_GAIN:30", "SET_RX_AMP_GAIN:14"
        ])
        audioEngine.start()
        DispatchQueue.main.async { self.isConnected = true }
    }

    func disconnect() {
        tcpClient.disconnect(); audioEngine.stop(); videoAccumCount = 0
        DispatchQueue.main.async { self.isConnected = false }
    }

    func setRadioMode(_ enabled: Bool) {
        guard !switchingMode else { return }
        switchingMode = true; radioMode = enabled; tcpClient.disconnect()
        audioEngine.stop()

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self else { return }
            Thread.sleep(forTimeInterval: 0.3)

            let nr = enabled ? 2000000 : 12500000
            if let a = self.audioDemod { audioDemod_setRadioMode(a, enabled ? 1 : 0); audioDemod_setSampleRate(a, Double(nr)) }
            if let d = self.palDecoder { palDecoder_setSampleRate(d, Int32(nr)) }

            DispatchQueue.main.async {
                self.sampleRate = nr; self.videoAccumCount = 0; self.updateFrameSize()
                if enabled { self.currentFrame = nil }
                self.updateAudioCarrier()
                self.audioEngine.flush()
                self.audioEngine.start()

                // Server stops/restarts HackRF on rate change - minimal stale data
                self.bytesToDiscard = 2 * 1024 * 1024  // 2 MB safety margin
                self.switchingMode = false

                self.tcpClient.connect(host: self.lastHost, initialCommands: [
                    "SET_SAMPLE_RATE:\(nr)", "SET_FREQ:\(self.frequency)",
                    "SET_LNA_GAIN:40", "SET_VGA_GAIN:30", "SET_RX_AMP_GAIN:14"
                ])
                if !enabled { self.bufferStatus = "" }
                else { self.bufferStatus = "Radio - tuning..." }
            }
        }
    }

    func changeSampleRate(_ nr: Int) {
        switchingMode = true; tcpClient.disconnect()
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self else { return }
            if let d = self.palDecoder { palDecoder_setSampleRate(d, Int32(nr)) }
            if let a = self.audioDemod { audioDemod_setSampleRate(a, Double(nr)) }
            DispatchQueue.main.async {
                self.sampleRate = nr; self.videoAccumCount = 0; self.updateFrameSize()
                self.updateAudioCarrier()
                self.bytesToDiscard = 2 * 1024 * 1024
                self.switchingMode = false
                self.tcpClient.connect(host: self.lastHost, initialCommands: [
                    "SET_SAMPLE_RATE:\(nr)", "SET_FREQ:\(self.frequency)",
                    "SET_LNA_GAIN:40", "SET_VGA_GAIN:30", "SET_RX_AMP_GAIN:14"
                ])
            }
        }
    }

    func updateFrequency() {
        if let d = palDecoder { palDecoder_setTuneFrequency(d, frequency) }
        tcpClient.setFrequency(frequency); updateAudioCarrier()
    }

    private func handleTCPData(_ ptr: UnsafePointer<Int8>, len: Int) {
        if switchingMode { return }

        // Discard stale server data after mode/rate switch
        if bytesToDiscard > 0 {
            bytesToDiscard -= len
            if bytesToDiscard <= 0 {
                bytesToDiscard = 0
                audioEngine.flush()
                DispatchQueue.main.async { self.bufferStatus = self.radioMode ? "Radio" : "" }
                print("[MODE] Stale data flushed, processing enabled")
            }
            return
        }

        if radioMode {
            let c = UnsafeMutablePointer<Int8>.allocate(capacity: len)
            memcpy(c, ptr, len)
            audioQueue.async { [weak self] in
                defer { c.deallocate() }
                guard let self = self, let a = self.audioDemod else { return }
                audioDemod_processSamples(a, c, len)
            }
            return
        }

        // VIDEO: accumulate full PAL frame, then decode
        let space = videoAccumCapacity - videoAccumCount
        let n = min(len, space)
        if n > 0 { memcpy(videoAccumBuffer + videoAccumCount, ptr, n); videoAccumCount += n }

        while videoAccumCount >= frameSize {
            os_unfair_lock_lock(videoLock)
            let canV = !videoProcessing
            if canV { videoProcessing = true }
            os_unfair_lock_unlock(videoLock)

            if canV {
                let vc = UnsafeMutablePointer<Int8>.allocate(capacity: frameSize)
                memcpy(vc, videoAccumBuffer, frameSize)
                let fs = frameSize
                videoQueue.async { [weak self] in
                    defer { vc.deallocate() }
                    guard let self = self, let d = self.palDecoder else {
                        if let self = self { os_unfair_lock_lock(self.videoLock); self.videoProcessing = false; os_unfair_lock_unlock(self.videoLock) }
                        return
                    }
                    palDecoder_processSamples(d, vc, fs)
                    os_unfair_lock_lock(self.videoLock); self.videoProcessing = false; os_unfair_lock_unlock(self.videoLock)
                }
            }

            let rem = videoAccumCount - frameSize
            if rem > 0 { memmove(videoAccumBuffer, videoAccumBuffer + frameSize, rem) }
            videoAccumCount = rem
        }

        // AUDIO: feed every packet (separate from video)
        let ac = UnsafeMutablePointer<Int8>.allocate(capacity: len)
        memcpy(ac, ptr, len)
        audioQueue.async { [weak self] in
            defer { ac.deallocate() }
            guard let self = self, let a = self.audioDemod else { return }
            audioDemod_processSamples(a, ac, len)
        }
    }

    private func setupCallbacks() {
        guard let d = palDecoder, let a = audioDemod else { return }
        let sp = Unmanaged.passUnretained(self).toOpaque()
        palDecoder_setFrameCallback(d, { rgba, w, h, ctx in
            guard let ctx = ctx, let rgba = rgba else { return }
            Unmanaged<PALDecoderManager>.fromOpaque(ctx).takeUnretainedValue().handleFrame(rgba, width: Int(w), height: Int(h))
        }, sp)
        palDecoder_setSyncStatsCallback(d, { sr, _, _, ctx in
            guard let ctx = ctx else { return }
            let m = Unmanaged<PALDecoderManager>.fromOpaque(ctx).takeUnretainedValue()
            DispatchQueue.main.async { m.syncQuality = sr }
        }, sp)
        audioDemod_setAudioCallback(a, { s, c, ctx in
            guard let ctx = ctx, let s = s else { return }
            Unmanaged<PALDecoderManager>.fromOpaque(ctx).takeUnretainedValue().audioEngine.enqueueAudio(s, count: Int(c))
        }, sp)
    }

    private func handleFrame(_ rgba: UnsafePointer<UInt8>, width: Int, height: Int) {
        if radioMode { return }
        frameCount += 1
        let bpr = width*4, ds = bpr*height
        guard let d = CFDataCreate(nil, rgba, ds), let p = CGDataProvider(data: d),
              let img = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                                bytesPerRow: bpr, space: colorSpace, bitmapInfo: bitmapInfo,
                                provider: p, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
        else { return }
        DispatchQueue.main.async { self.currentFrame = img }
    }

    private func startFPSTimer() {
        fpsTimer = DispatchSource.makeTimerSource(queue: .main)
        fpsTimer?.schedule(deadline: .now()+1, repeating: 1)
        fpsTimer?.setEventHandler { [weak self] in guard let self = self else { return }; self.fps = Double(self.frameCount); self.frameCount = 0 }
        fpsTimer?.resume()
    }

    private func updateAudioCarrier() {
        guard let a = audioDemod else { return }; if radioMode { return }
        let t = Double(frequency)/1e6; var vc: Double
        if t>=470&&t<=862{let ch=Int(floor((t-470-0.001)/8));vc=470+Double(max(ch,0))*8+1.25}
        else if t>=174&&t<=230{let ch=Int(floor((t-174-0.001)/8));vc=174+Double(max(ch,0))*8+1.25}
        else{vc=t}
        audioDemod_setAudioCarrierFreq(a,(vc+5.5-t)*1e6)
    }

    func setVideoGain(_ g: Float){if let d=palDecoder{palDecoder_setVideoGain(d,g)}}
    func setVideoOffset(_ o: Float){if let d=palDecoder{palDecoder_setVideoOffset(d,o)}}
    func setVideoInvert(_ i: Bool){if let d=palDecoder{palDecoder_setVideoInvert(d,i ?1:0)}}
    func setColorMode(_ c: Bool){if let d=palDecoder{palDecoder_setColorMode(d,c ?1:0)}}
    func setChromaGain(_ g: Float){if let d=palDecoder{palDecoder_setChromaGain(d,g)}}
    func setSyncThreshold(_ t: Float){if let d=palDecoder{palDecoder_setSyncThreshold(d,t)}}
    func setAudioGain(_ g: Float){if let a=audioDemod{audioDemod_setAudioGain(a,g)}}
    func setAudioEnabled(_ e: Bool){if let a=audioDemod{audioDemod_setAudioEnabled(a,e ?1:0)}}
    func setVolume(_ v: Float){audioEngine.volume=v}
    func setLnaGain(_ g: UInt){tcpClient.setLnaGain(g)}
    func setVgaGain(_ g: UInt){tcpClient.setVgaGain(g)}
    func setRxAmpGain(_ g: UInt){tcpClient.setRxAmpGain(g)}
}
