//
//  HackRFReceiver.swift
//  HackRFReceiver
//
//  Main coordinator for HackRF signal reception and processing
//

import Foundation
import Combine

class HackRFReceiver: ObservableObject {
    @Published var isConnected = false
    @Published var samplesReceived: Int = 0
    @Published var currentFrequency: Int = 100_000_000
    @Published var tvImage: [UInt8]? = nil
    
    private var tcpClient: TCPClient?
    private var fmDemodulator: FMDemodulator?
    private var palDecoder: PALDecoder?
    private var audioPlayer: AudioPlayer?
    private var currentMode: ReceiverMode = .fm
    
    private var receiveQueue = DispatchQueue(label: "com.hackrf.receive", qos: .userInitiated)
    private var isReceiving = false
    
    func connect(to host: String, port: Int, frequency: Int, mode: ReceiverMode) {
        currentFrequency = frequency
        currentMode = mode
        
        tcpClient = TCPClient(host: host, port: port)
        
        receiveQueue.async { [weak self] in
            guard let self = self else { return }
            
            if self.tcpClient?.connect() == true {
                DispatchQueue.main.async {
                    self.isConnected = true
                }
                
                // Initialize appropriate processor
                if mode == .fmRadio {
                    self.audioPlayer = AudioPlayer()
                    self.fmDemodulator = FMDemodulator(sampleRate: 2_000_000, audioRate: 48_000)
                    self.audioPlayer?.start()
                    print("FM Radio mode initialized")
                } else if mode == .amRadio {
                    self.audioPlayer = AudioPlayer()
                    self.amDemodulator = AMDemodulator(sampleRate: 2_000_000, audioRate: 48_000)
                    self.audioPlayer?.start()
                    print("AM Radio mode initialized")
                } else if mode == .tvPAL {
                    self.palDecoder = PALDecoder(sampleRate: 16_000_000)
                    self.audioPlayer = AudioPlayer()
                    self.audioPlayer?.start()
                    
                    // Connect PAL decoder audio output to audio player
                    self.palDecoder?.audioCallback = { [weak self] audioSamples in
                        self?.audioPlayer?.play(audioSamples)
                    }
                    
                    print("PAL-B/G TV mode initialized (AM for video, FM for audio at +5.5 MHz)")
                }
                
                self.isReceiving = true
                self.receiveLoop()
            } else {
                print("Failed to connect")
            }
        }
    }
    
    func disconnect() {
        isReceiving = false
        
        receiveQueue.async { [weak self] in
            guard let self = self else { return }
            
            self.audioPlayer?.stop()
            self.audioPlayer = nil
            self.fmDemodulator = nil
            self.palDecoder = nil
            self.tcpClient?.disconnect()
            self.tcpClient = nil
            
            DispatchQueue.main.async {
                self.isConnected = false
                self.samplesReceived = 0
            }
        }
    }
    
    func setFrequency(_ frequency: Int) {
        currentFrequency = frequency
        // In a real implementation, you would send a command to the HackRF server
        // to change the frequency
        print("Frequency set to: \(frequency) Hz")
    }
    
    private func receiveLoop() {
        let bufferSize = 262144 // Match Python code buffer size
        
        while isReceiving {
            guard let data = tcpClient?.receive(maxBytes: bufferSize), !data.isEmpty else {
                print("Connection closed or no data")
                DispatchQueue.main.async {
                    self.disconnect()
                }
                break
            }
            
            DispatchQueue.main.async {
                self.samplesReceived += data.count
            }
            
            if currentMode == .fm {
                processFMData(data)
            } else {
                processTVData(data)
            }
        }
    }
    
    private func processFMData(_ data: Data) {
        guard let demodulator = fmDemodulator, let player = audioPlayer else { return }
        
        // Convert Data to [Int8]
        let iqSamples = data.withUnsafeBytes { buffer in
            Array(buffer.bindMemory(to: Int8.self))
        }
        
        // Demodulate
        if let audioSamples = demodulator.demodulate(iqSamples) {
            // Play audio
            player.play(audioSamples)
        }
    }
    
    private func processTVData(_ data: Data) {
        guard let decoder = palDecoder else { return }
        
        // Convert Data to [Int8]
        let iqSamples = data.withUnsafeBytes { buffer in
            Array(buffer.bindMemory(to: Int8.self))
        }
        
        // Decode PAL signal
        if let frameData = decoder.processSignal(iqSamples) {
            DispatchQueue.main.async {
                self.tvImage = frameData
            }
        }
    }
}
