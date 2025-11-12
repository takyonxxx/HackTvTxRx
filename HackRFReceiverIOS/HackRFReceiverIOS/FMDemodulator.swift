import Foundation
import Accelerate

class FMDemodulator {
    private let sampleRate: Int
    private let audioRate: Int
    private let decimation: Int
    
    // De-emphasis filter coefficients (75µs for broadcast FM)
    private var filterB: [Double] = []
    private var filterA: [Double] = []
    
    init(sampleRate: Int, audioRate: Int = 48000) {
        self.sampleRate = sampleRate
        self.audioRate = audioRate
        self.decimation = max(1, sampleRate / audioRate)
        
        setupDeemphasisFilter()
    }
    
    private func setupDeemphasisFilter() {
        // 75µs de-emphasis filter for broadcast FM
        let d = Double(audioRate) * 75e-6
        let x = exp(-1.0 / d)
        
        filterB = [1.0 - x]
        filterA = [1.0, -x]
    }
    
    func demodulate(iqSamples: [Int8]) -> [Double]? {
        guard iqSamples.count >= 4 else { return nil }
        
        // Convert to I/Q
        var i = [Double]()
        var q = [Double]()
        
        for idx in stride(from: 0, to: iqSamples.count - 1, by: 2) {
            i.append(Double(iqSamples[idx]) / 127.0)
            q.append(Double(iqSamples[idx + 1]) / 127.0)
        }
        
        guard i.count > 1 else { return nil }
        
        // Calculate phase
        var phases = [Double](repeating: 0.0, count: i.count)
        for idx in 0..<i.count {
            phases[idx] = atan2(q[idx], i[idx])
        }
        
        // Unwrap phase
        phases = unwrapPhase(phases)
        
        // Phase differentiation (FM demod)
        var demod = [Double](repeating: 0.0, count: phases.count - 1)
        for idx in 0..<demod.count {
            demod[idx] = phases[idx + 1] - phases[idx]
        }
        
        guard !demod.isEmpty else { return nil }
        
        // Decimate to audio rate
        if decimation > 1 {
            demod = decimate(demod, factor: decimation)
        }
        
        // Apply de-emphasis
        demod = applyDeemphasis(demod)
        
        // Normalize
        if let maxVal = demod.max(by: { abs($0) < abs($1) }), abs(maxVal) > 0.01 {
            let scale = 0.5 / abs(maxVal)
            demod = demod.map { $0 * scale }
        }
        
        return demod
    }
    
    private func unwrapPhase(_ phases: [Double]) -> [Double] {
        var unwrapped = [Double](repeating: 0.0, count: phases.count)
        var correction = 0.0
        
        unwrapped[0] = phases[0]
        
        for i in 1..<phases.count {
            var diff = phases[i] - phases[i-1]
            
            while diff > .pi {
                diff -= 2.0 * .pi
                correction -= 2.0 * .pi
            }
            while diff < -.pi {
                diff += 2.0 * .pi
                correction += 2.0 * .pi
            }
            
            unwrapped[i] = phases[i] + correction
        }
        
        return unwrapped
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
    
    private func applyDeemphasis(_ signal: [Double]) -> [Double] {
        guard !signal.isEmpty else { return [] }
        
        var output = [Double](repeating: 0.0, count: signal.count)
        
        for i in 0..<signal.count {
            var y = filterB[0] * signal[i]
            
            if i > 0 {
                y -= filterA[1] * output[i-1]
            }
            
            output[i] = y
        }
        
        return output
    }
}
