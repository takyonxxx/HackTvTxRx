import Foundation
import Accelerate

class AMDemodulator {
    private let sampleRate: Int
    private let audioRate: Int
    private let decimation: Int
    
    init(sampleRate: Int, audioRate: Int = 48000) {
        self.sampleRate = sampleRate
        self.audioRate = audioRate
        self.decimation = max(1, sampleRate / audioRate)
    }
    
    func demodulate(iqSamples: [Int8]) -> [Double]? {
        guard iqSamples.count >= 2 else { return nil }
        
        // Convert to I/Q
        var i = [Double]()
        var q = [Double]()
        
        for idx in stride(from: 0, to: iqSamples.count - 1, by: 2) {
            i.append(Double(iqSamples[idx]) / 127.0)
            q.append(Double(iqSamples[idx + 1]) / 127.0)
        }
        
        guard !i.isEmpty else { return nil }
        
        // Envelope detection: magnitude = sqrt(I^2 + Q^2)
        var envelope = [Double](repeating: 0.0, count: i.count)
        for idx in 0..<i.count {
            envelope[idx] = sqrt(i[idx] * i[idx] + q[idx] * q[idx])
        }
        
        // DC removal (highpass filter)
        envelope = removeDC(envelope)
        
        // Decimate to audio rate
        if decimation > 1 {
            envelope = decimate(envelope, factor: decimation)
        }
        
        // Normalize
        if let maxVal = envelope.max(by: { abs($0) < abs($1) }), abs(maxVal) > 0.01 {
            let scale = 0.5 / abs(maxVal)
            envelope = envelope.map { $0 * scale }
        }
        
        return envelope
    }
    
    private func removeDC(_ signal: [Double]) -> [Double] {
        guard !signal.isEmpty else { return [] }
        
        // Simple DC blocking filter
        let alpha = 0.95
        var output = [Double](repeating: 0.0, count: signal.count)
        var prevInput = 0.0
        var prevOutput = 0.0
        
        for i in 0..<signal.count {
            output[i] = alpha * (prevOutput + signal[i] - prevInput)
            prevInput = signal[i]
            prevOutput = output[i]
        }
        
        return output
    }
    
    private func decimate(_ signal: [Double], factor: Int) -> [Double] {
        guard factor > 1 else { return signal }
        
        let filterLength = min(31, signal.count / 4)
        let cutoff = 0.4 / Double(factor)
        let firCoeffs = designLowpassFIR(length: filterLength, cutoff: cutoff)
        
        var filtered = [Double](repeating: 0.0, count: signal.count)
        
        for i in 0..<signal.count {
            var sum = 0.0
            for j in 0..<filterLength {
                let idx = i - j + filterLength / 2
                if idx >= 0 && idx < signal.count {
                    sum += signal[idx] * firCoeffs[j]
                }
            }
            filtered[i] = sum
        }
        
        var decimated = [Double]()
        for i in stride(from: 0, to: filtered.count, by: factor) {
            decimated.append(filtered[i])
        }
        
        return decimated
    }
    
    private func designLowpassFIR(length: Int, cutoff: Double) -> [Double] {
        var coeffs = [Double](repeating: 0.0, count: length)
        let center = Double(length - 1) / 2.0
        
        for i in 0..<length {
            let x = Double(i) - center
            if abs(x) < 0.001 {
                coeffs[i] = 2.0 * cutoff
            } else {
                coeffs[i] = sin(2.0 * .pi * cutoff * x) / (.pi * x)
            }
            
            // Hamming window
            coeffs[i] *= 0.54 - 0.46 * cos(2.0 * .pi * Double(i) / Double(length - 1))
        }
        
        let sum = coeffs.reduce(0, +)
        if abs(sum) > 0.001 {
            coeffs = coeffs.map { $0 / sum }
        }
        
        return coeffs
    }
}
