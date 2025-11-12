//
//  AMDemodulator.swift
//  HackRFReceiver
//
//  AM (Amplitude Modulation) demodulation for radio signals
//

import Foundation
import Accelerate

class AMDemodulator {
    private let sampleRate: Int
    private let audioRate: Int
    private let decimation: Int
    
    private var processingBuffer: [Float] = []
    
    init(sampleRate: Int, audioRate: Int) {
        self.sampleRate = sampleRate
        self.audioRate = audioRate
        self.decimation = sampleRate / audioRate
        
        print("AM Demodulator initialized: SR=\(sampleRate), AR=\(audioRate), Dec=\(decimation)")
    }
    
    func demodulate(_ iqSamples: [Int8]) -> [Float]? {
        guard iqSamples.count >= 2 else { return nil }
        
        // Convert IQ samples to complex numbers and calculate amplitude (envelope detection)
        let sampleCount = iqSamples.count / 2
        var amplitude = [Float](repeating: 0, count: sampleCount)
        
        for idx in 0..<sampleCount {
            let i = Float(iqSamples[idx * 2]) / 127.0
            let q = Float(iqSamples[idx * 2 + 1]) / 127.0
            
            // Envelope detection: amplitude = sqrt(I² + Q²)
            amplitude[idx] = sqrt(i * i + q * q)
        }
        
        // Remove DC component (high-pass filter)
        let mean = amplitude.reduce(0, +) / Float(amplitude.count)
        for idx in 0..<amplitude.count {
            amplitude[idx] -= mean
        }
        
        // Decimate to audio rate
        var audio = decimateSignal(amplitude, factor: decimation)
        
        // Normalize
        if let maxVal = audio.max(by: { abs($0) < abs($1) }), abs(maxVal) > 0 {
            let normFactor = 0.5 / abs(maxVal)
            for idx in 0..<audio.count {
                audio[idx] *= normFactor
            }
        }
        
        return audio
    }
    
    private func decimateSignal(_ signal: [Float], factor: Int) -> [Float] {
        guard factor > 1 else { return signal }
        
        // Simple decimation with averaging (low-pass filter effect)
        var decimated = [Float]()
        decimated.reserveCapacity(signal.count / factor)
        
        for i in stride(from: 0, to: signal.count, by: factor) {
            let endIdx = min(i + factor, signal.count)
            let chunk = signal[i..<endIdx]
            let avg = chunk.reduce(0, +) / Float(chunk.count)
            decimated.append(avg)
        }
        
        return decimated
    }
}
