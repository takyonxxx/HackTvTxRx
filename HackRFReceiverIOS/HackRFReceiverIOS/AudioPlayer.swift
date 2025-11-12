import Foundation
import AVFoundation

class AudioPlayer {
    private var audioEngine: AVAudioEngine?
    private var playerNode: AVAudioPlayerNode?
    private let audioFormat: AVAudioFormat
    private let sampleRate: Double = 48000
    
    init() {
        guard let format = AVAudioFormat(
            commonFormat: .pcmFormatFloat32,
            sampleRate: sampleRate,
            channels: 1,
            interleaved: false
        ) else {
            fatalError("Audio format oluşturulamadı")
        }
        self.audioFormat = format
        
        setupAudioEngine()
    }
    
    private func setupAudioEngine() {
        audioEngine = AVAudioEngine()
        playerNode = AVAudioPlayerNode()
        
        guard let engine = audioEngine, let player = playerNode else { return }
        
        engine.attach(player)
        engine.connect(player, to: engine.mainMixerNode, format: audioFormat)
        engine.mainMixerNode.outputVolume = 1.0
    }
    
    func start() {
        do {
            #if os(iOS)
            // iOS requires AVAudioSession configuration
            let audioSession = AVAudioSession.sharedInstance()
            try audioSession.setCategory(.playback, mode: .default, options: [])
            try audioSession.setActive(true)
            #elseif os(macOS)
            // macOS doesn't use AVAudioSession - audio works directly
            // No additional configuration needed
            #endif
            
            try audioEngine?.start()
            playerNode?.play()
            
            print("✓ Audio engine başlatıldı (\(sampleRate) Hz)")
        } catch {
            print("❌ Audio engine başlatma hatası: \(error)")
        }
    }
    
    func stop() {
        playerNode?.stop()
        audioEngine?.stop()
        
        #if os(iOS)
        do {
            try AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
        } catch {
            print("⚠️ Audio session kapatma hatası: \(error)")
        }
        #endif
        
        print("✓ Audio engine durduruldu")
    }
    
    func play(samples: [Float]) {
        guard let player = playerNode, !samples.isEmpty else { return }
        
        guard let buffer = AVAudioPCMBuffer(
            pcmFormat: audioFormat,
            frameCapacity: AVAudioFrameCount(samples.count)
        ) else {
            return
        }
        
        buffer.frameLength = buffer.frameCapacity
        
        if let channelData = buffer.floatChannelData {
            let channelDataPointer = channelData[0]
            for i in 0..<samples.count {
                channelDataPointer[i] = samples[i]
            }
        }
        
        player.scheduleBuffer(buffer, completionHandler: nil)
    }
    
    func setVolume(_ volume: Float) {
        audioEngine?.mainMixerNode.outputVolume = volume
    }
    
    var isRunning: Bool {
        return audioEngine?.isRunning ?? false
    }
    
    deinit {
        stop()
    }
}
