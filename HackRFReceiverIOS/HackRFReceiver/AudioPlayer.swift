//
//  AudioPlayer.swift
//  HackRFReceiver
//
//  Audio output manager for iOS
//

import Foundation
import AVFoundation

class AudioPlayer {
    private var audioEngine: AVAudioEngine?
    private var playerNode: AVAudioPlayerNode?
    private let audioFormat: AVAudioFormat
    private var isPlaying = false
    
    private let sampleRate: Double = 48000
    private let channelCount: AVAudioChannelCount = 1
    
    init() {
        // Initialize audio format (mono, 48kHz, float32)
        guard let format = AVAudioFormat(
            commonFormat: .pcmFormatFloat32,
            sampleRate: sampleRate,
            channels: channelCount,
            interleaved: false
        ) else {
            fatalError("Failed to create audio format")
        }
        self.audioFormat = format
        
        setupAudioSession()
    }
    
    private func setupAudioSession() {
        do {
            let audioSession = AVAudioSession.sharedInstance()
            try audioSession.setCategory(.playback, mode: .default, options: [])
            try audioSession.setActive(true)
            print("Audio session configured")
        } catch {
            print("Failed to setup audio session: \(error)")
        }
    }
    
    func start() {
        audioEngine = AVAudioEngine()
        playerNode = AVAudioPlayerNode()
        
        guard let engine = audioEngine, let player = playerNode else {
            print("Failed to create audio engine or player")
            return
        }
        
        engine.attach(player)
        engine.connect(player, to: engine.mainMixerNode, format: audioFormat)
        
        do {
            try engine.start()
            player.play()
            isPlaying = true
            print("Audio player started")
        } catch {
            print("Failed to start audio engine: \(error)")
        }
    }
    
    func stop() {
        playerNode?.stop()
        audioEngine?.stop()
        isPlaying = false
        
        do {
            try AVAudioSession.sharedInstance().setActive(false)
        } catch {
            print("Failed to deactivate audio session: \(error)")
        }
        
        print("Audio player stopped")
    }
    
    func play(_ samples: [Float]) {
        guard isPlaying, let player = playerNode else { return }
        
        // Create PCM buffer
        guard let buffer = AVAudioPCMBuffer(
            pcmFormat: audioFormat,
            frameCapacity: AVAudioFrameCount(samples.count)
        ) else {
            print("Failed to create audio buffer")
            return
        }
        
        buffer.frameLength = AVAudioFrameCount(samples.count)
        
        // Copy samples to buffer
        guard let channelData = buffer.floatChannelData else { return }
        for i in 0..<samples.count {
            channelData[0][i] = samples[i]
        }
        
        // Schedule buffer for playback
        player.scheduleBuffer(buffer, completionHandler: nil)
    }
}
