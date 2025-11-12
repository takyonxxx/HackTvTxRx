import Foundation
import Combine
import SwiftUI

class HackRFReceiver: ObservableObject {
    @Published var isConnected = false
    @Published var statusMessage = "Hazır"
    @Published var audioVolume: Double = 0.5
    @Published var currentVideoFrame: [UInt8] = []
    
    private var tcpClient: TCPClient?
    private var currentMode: ReceiverMode = .fm
    
    // Demodulators
    private var fmDemodulator: FMDemodulator?
    private var amDemodulator: AMDemodulator?
    private var nfmDemodulator: NFMDemodulator?
    private var palDecoder: PALDecoder?
    
    // Audio player
    private var audioPlayer: AudioPlayer?
    
    // MARK: - Connection
    
    func connect(to host: String, port: Int, mode: ReceiverMode, frequency: Int) {
        statusMessage = "Bağlanılıyor..."
        currentMode = mode
        
        // Create appropriate demodulator
        setupDemodulator(for: mode, frequency: frequency)
        
        // Create audio player
        audioPlayer = AudioPlayer()
        audioPlayer?.start()
        
        // Create TCP client
        tcpClient = TCPClient(host: host, port: port)
        tcpClient?.onDataReceived = { [weak self] data in
            self?.processData(data)
        }
        
        tcpClient?.onStatusChanged = { [weak self] connected, message in
            DispatchQueue.main.async {
                self?.isConnected = connected
                self?.statusMessage = message
            }
        }
        
        tcpClient?.connect()
    }
    
    func disconnect() {
        tcpClient?.disconnect()
        audioPlayer?.stop()
        audioPlayer = nil
        
        fmDemodulator = nil
        amDemodulator = nil
        nfmDemodulator = nil
        palDecoder = nil
        
        isConnected = false
        statusMessage = "Bağlantı kesildi"
    }
    
    // MARK: - Port 5001 Control Commands
    
    func sendControlCommand(_ command: String, port: Int, host: String) {
        let controlClient = TCPClient(host: host, port: port)
        
        controlClient.onStatusChanged = { connected, message in
            if connected {
                // Send command
                let data = (command + "\n").data(using: .utf8)!
                controlClient.send(data: data)
                print("✓ Port \(port): \(command)")
                
                // Close after sending
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                    controlClient.disconnect()
                }
            }
        }
        
        controlClient.connect()
    }
    
    func sendAllControlParameters(frequency: Int, sampleRate: Int, vgaGain: Int, lnaGain: Int, rxAmpGain: Int, port: Int, host: String) {
        DispatchQueue.main.async {
            self.statusMessage = "Parametreler gönderiliyor..."
        }
        
        let commands = [
            "SET_FREQ:\(frequency)",
            "SET_SAMPLE_RATE:\(sampleRate)",
            "SET_VGA_GAIN:\(vgaGain)",
            "SET_LNA_GAIN:\(lnaGain)",
            "SET_RX_AMP_GAIN:\(rxAmpGain)"
        ]
        
        for (index, command) in commands.enumerated() {
            DispatchQueue.main.asyncAfter(deadline: .now() + Double(index) * 0.2) {
                self.sendControlCommand(command, port: port, host: host)
                
                if index == commands.count - 1 {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
                        self.statusMessage = "✓ Tüm parametreler gönderildi"
                    }
                }
            }
        }
    }
    
    // MARK: - Demodulator Setup
    
    private func setupDemodulator(for mode: ReceiverMode, frequency: Int) {
        switch mode {
        case .fm:
            fmDemodulator = FMDemodulator(sampleRate: 2_000_000, audioRate: 48000)
            
        case .am:
            amDemodulator = AMDemodulator(sampleRate: 2_000_000, audioRate: 48000)
            
        case .nfm:
            nfmDemodulator = NFMDemodulator(sampleRate: 2_000_000, audioRate: 48000)
            
        case .tv:
            palDecoder = PALDecoder(sampleRate: 16_000_000)
            palDecoder?.onVideoFrameReady = { [weak self] frame in
                DispatchQueue.main.async {
                    self?.currentVideoFrame = frame
                }
            }
            palDecoder?.onAudioReady = { [weak self] samples in
                self?.playAudio(samples: samples)
            }
        }
    }
    
    // MARK: - Data Processing
    
    private func processData(_ data: Data) {
        let iqSamples = data.map { Int8(bitPattern: $0) }
        
        switch currentMode {
        case .fm:
            if let audio = fmDemodulator?.demodulate(iqSamples: iqSamples) {
                playAudio(samples: audio.map { Float($0) })
            }
            
        case .am:
            if let audio = amDemodulator?.demodulate(iqSamples: iqSamples) {
                playAudio(samples: audio.map { Float($0) })
            }
            
        case .nfm:
            if let audio = nfmDemodulator?.demodulate(iqSamples: iqSamples) {
                playAudio(samples: audio.map { Float($0) })
            }
            
        case .tv:
            palDecoder?.decode(iqSamples: iqSamples)
        }
    }
    
    private func playAudio(samples: [Float]) {
        let scaledSamples = samples.map { $0 * Float(audioVolume) }
        audioPlayer?.play(samples: scaledSamples)
    }
}
