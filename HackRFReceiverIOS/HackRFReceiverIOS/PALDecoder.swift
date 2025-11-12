import Foundation
import Accelerate

class PALDecoder {
    private let sampleRate: Int
    private let videoWidth = 720
    private let videoHeight = 576
    private let audioRate = 48000
    
    // PAL-B/G: Audio carrier is +5.5 MHz from video carrier
    private let audioOffset = 5_500_000
    
    var onVideoFrameReady: (([UInt8]) -> Void)?
    var onAudioReady: (([Float]) -> Void)?
    
    // Video processing
    private var videoBuffer: [Double] = []
    private var currentLine: [UInt8] = []
    private var frameBuffer: [UInt8] = []
    
    // Audio processing
    private var audioDemodulator: FMDemodulator?
    
    init(sampleRate: Int) {
        self.sampleRate = sampleRate
        
        // Create FM demodulator for audio (50µs de-emphasis for PAL)
        audioDemodulator = FMDemodulator(sampleRate: sampleRate, audioRate: audioRate)
    }
    
    func decode(iqSamples: [Int8]) {
        guard iqSamples.count >= 2 else { return }
        
        // Split into video and audio processing
        processVideo(iqSamples: iqSamples)
        processAudio(iqSamples: iqSamples)
    }
    
    // MARK: - Video Processing (AM Demodulation)
    
    private func processVideo(iqSamples: [Int8]) {
        // Convert to I/Q
        var i = [Double]()
        var q = [Double]()
        
        for idx in stride(from: 0, to: iqSamples.count - 1, by: 2) {
            i.append(Double(iqSamples[idx]) / 127.0)
            q.append(Double(iqSamples[idx + 1]) / 127.0)
        }
        
        // Envelope detection for video (AM)
        var video = [Double](repeating: 0.0, count: i.count)
        for idx in 0..<i.count {
            video[idx] = sqrt(i[idx] * i[idx] + q[idx] * q[idx])
        }
        
        // Add to video buffer
        videoBuffer.append(contentsOf: video)
        
        // Process lines when we have enough samples
        // PAL line duration: ~64 µs = ~1024 samples at 16 MHz
        let samplesPerLine = 1024
        
        while videoBuffer.count >= samplesPerLine {
            let line = Array(videoBuffer.prefix(samplesPerLine))
            videoBuffer.removeFirst(samplesPerLine)
            
            processVideoLine(line)
        }
    }
    
    private func processVideoLine(_ line: [Double]) {
        // Detect sync pulse (first ~10% of line)
        let syncLength = line.count / 10
        let syncLevel = line.prefix(syncLength).reduce(0.0, +) / Double(syncLength)
        
        // If sync detected (low level), start new line
        if syncLevel < 0.3 {
            if !currentLine.isEmpty {
                // Add completed line to frame
                frameBuffer.append(contentsOf: currentLine)
                currentLine = []
                
                // If frame is complete, emit it
                if frameBuffer.count >= videoWidth * videoHeight {
                    let frame = Array(frameBuffer.prefix(videoWidth * videoHeight))
                    onVideoFrameReady?(frame)
                    frameBuffer.removeAll(keepingCapacity: true)
                }
            }
        }
        
        // Extract active video (skip sync and blanking)
        let activeStart = syncLength
        let activeEnd = line.count
        let activeLine = Array(line[activeStart..<activeEnd])
        
        // Resample to video width
        let resampled = resample(activeLine, to: videoWidth)
        
        // Convert to 8-bit grayscale
        let grayscale = resampled.map { value -> UInt8 in
            let clamped = max(0.0, min(1.0, value))
            return UInt8(clamped * 255.0)
        }
        
        currentLine.append(contentsOf: grayscale)
    }
    
    // MARK: - Audio Processing (FM +5.5 MHz)
    
    private func processAudio(iqSamples: [Int8]) {
        // Frequency shift by -5.5 MHz to bring audio carrier to baseband
        let shiftedSamples = frequencyShift(iqSamples: iqSamples, shift: -audioOffset)
        
        // FM demodulate
        if let audioSamples = audioDemodulator?.demodulate(iqSamples: shiftedSamples) {
            onAudioReady?(audioSamples.map { Float($0) })
        }
    }
    
    private func frequencyShift(iqSamples: [Int8], shift: Int) -> [Int8] {
        let phaseIncrement = 2.0 * .pi * Double(shift) / Double(sampleRate)
        var phase = 0.0
        
        var shifted = [Int8]()
        
        for idx in stride(from: 0, to: iqSamples.count - 1, by: 2) {
            let i = Double(iqSamples[idx]) / 127.0
            let q = Double(iqSamples[idx + 1]) / 127.0
            
            // Complex multiplication for frequency shift
            let cosPhase = cos(phase)
            let sinPhase = sin(phase)
            
            let newI = i * cosPhase - q * sinPhase
            let newQ = i * sinPhase + q * cosPhase
            
            shifted.append(Int8(newI * 127.0))
            shifted.append(Int8(newQ * 127.0))
            
            phase += phaseIncrement
            if phase > 2.0 * .pi {
                phase -= 2.0 * .pi
            }
        }
        
        return shifted
    }
    
    // MARK: - Helper Functions
    
    private func resample(_ signal: [Double], to targetLength: Int) -> [Double] {
        guard signal.count > 1 else { return signal }
        
        var resampled = [Double](repeating: 0.0, count: targetLength)
        let ratio = Double(signal.count - 1) / Double(targetLength - 1)
        
        for i in 0..<targetLength {
            let pos = Double(i) * ratio
            let idx1 = Int(floor(pos))
            let idx2 = min(idx1 + 1, signal.count - 1)
            let frac = pos - Double(idx1)
            
            // Linear interpolation
            resampled[i] = signal[idx1] * (1.0 - frac) + signal[idx2] * frac
        }
        
        return resampled
    }
}
