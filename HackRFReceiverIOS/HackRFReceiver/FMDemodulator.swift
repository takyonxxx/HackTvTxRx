//
//  FMDemodulator.swift
//  HackRFReceiver
//
//  FM demodulation and audio processing
//

import Foundation
import Accelerate

class FMDemodulator {
    private let sampleRate: Int
    private let audioRate: Int
    private let decimation: Int
    
    // De-emphasis filter state
    private var deemphasisB: [Float]
    private var deemphasisA: [Float]
    private var filterState: [Float] = [0.0]
    
    private var processingBuffer: [Float] = []
    
    init(sampleRate: Int, audioRate: Int) {
        self.sampleRate = sampleRate
        self.audioRate = audioRate
        self.decimation = sampleRate / audioRate
        
        // Create 75µs de-emphasis filter for broadcast FM
        let d = Float(audioRate) * 75e-6
        let x = exp(-1.0 / d)
        self.deemphasisB = [1.0 - x]
        self.deemphasisA = [1.0, -x]
        
        print("FM Demodulator initialized: SR=\(sampleRate), AR=\(audioRate), Dec=\(decimation)")
    }
    
    func demodulate(_ iqSamples: [Int8]) -> [Float]? {
        guard iqSamples.count >= 2 else { return nil }
        
        // Convert IQ samples to complex numbers
        let sampleCount = iqSamples.count / 2
        var i = [Float](repeating: 0, count: sampleCount)
        var q = [Float](repeating: 0, count: sampleCount)
        
        for idx in 0..<sampleCount {
            i[idx] = Float(iqSamples[idx * 2]) / 127.0
            q[idx] = Float(iqSamples[idx * 2 + 1]) / 127.0
        }
        
        // Calculate phase (atan2)
        var phase = [Float](repeating: 0, count: sampleCount)
        for idx in 0..<sampleCount {
            phase[idx] = atan2(q[idx], i[idx])
        }
        
        // Unwrap phase
        var unwrappedPhase = [Float](repeating: 0, count: sampleCount)
        unwrappedPhase[0] = phase[0]
        for idx in 1..<sampleCount {
            var delta = phase[idx] - phase[idx - 1]
            if delta > .pi {
                delta -= 2 * .pi
            } else if delta < -.pi {
                delta += 2 * .pi
            }
            unwrappedPhase[idx] = unwrappedPhase[idx - 1] + delta
        }
        
        // Differentiate (FM demodulation)
        var demod = [Float](repeating: 0, count: sampleCount - 1)
        for idx in 0..<(sampleCount - 1) {
            demod[idx] = unwrappedPhase[idx + 1] - unwrappedPhase[idx]
        }
        
        // Decimate to audio rate
        var audio = decimateSignal(demod, factor: decimation)
        
        // Apply de-emphasis filter
        audio = applyDeemphasis(audio)
        
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
    
    private func applyDeemphasis(_ signal: [Float]) -> [Float] {
        var output = [Float](repeating: 0, count: signal.count)
        
        // Simple IIR filter implementation
        // y[n] = b0*x[n] - a1*y[n-1]
        let b0 = deemphasisB[0]
        let a1 = deemphasisA[1]
        
        output[0] = b0 * signal[0] - a1 * filterState[0]
        for i in 1..<signal.count {
            output[i] = b0 * signal[i] - a1 * output[i - 1]
        }
        
        // Update filter state for next call
        filterState[0] = output[output.count - 1]
        
        return output
    }
}
