//
//  PALDecoder.swift
//  HackRFReceiver
//
//  PAL-B/G analog TV signal decoder with AUDIO support
//  625 lines, 25 fps, 16 MHz sample rate
//  Video: AM demodulation
//  Audio: FM demodulation at +5.5 MHz offset
//

import Foundation
import Accelerate

class PALDecoder {
    // PAL-B/G Standards
    private let linesPerFrame = 625
    private let activeLines = 576
    private let frameRate: Float = 25.0
    private let lineFrequency: Float = 15625.0  // Hz
    
    private let sampleRate: Int
    private let samplesPerLine: Int
    
    // Video parameters
    private let imageWidth = 720
    private let imageHeight = 576
    
    // Luminance (Y) demodulation
    private var lineBuffer: [Float] = []
    private var frameBuffer: [[UInt8]] = []
    private var currentLine = 0
    
    // Chrominance for color (simplified)
    private let colorSubcarrier: Float = 4.43361875e6  // Hz (PAL color subcarrier)
    
    // Audio parameters (PAL-B/G audio at +5.5 MHz from video carrier)
    private let audioOffset: Float = 5.5e6  // 5.5 MHz offset
    private let audioRate: Int = 48000      // Audio output rate
    private var audioPhaseAccumulator: Float = 0.0
    private var audioFilterState: [Float] = [0.0]
    private var audioDemphasisB: [Float]
    private var audioDemphasisA: [Float]
    
    // Audio callback
    var audioCallback: (([Float]) -> Void)?
    
    private var accumulatedSamples: [Int8] = []
    
    init(sampleRate: Int) {
        self.sampleRate = sampleRate
        self.samplesPerLine = Int(Float(sampleRate) / lineFrequency)
        
        // Initialize frame buffer
        frameBuffer = Array(repeating: Array(repeating: 0, count: imageWidth), count: imageHeight)
        
        // Create 50µs de-emphasis filter for PAL TV audio (different from 75µs for FM radio)
        let d = Float(audioRate) * 50e-6
        let x = exp(-1.0 / d)
        self.audioDemphasisB = [1.0 - x]
        self.audioDemphasisA = [1.0, -x]
        
        print("PAL Decoder initialized: \(sampleRate) Hz, \(samplesPerLine) samples/line")
        print("PAL Audio: FM demodulation at +5.5 MHz, 50µs de-emphasis")
    }
    
    func processSignal(_ iqSamples: [Int8]) -> [UInt8]? {
        // Accumulate samples
        accumulatedSamples.append(contentsOf: iqSamples)
        
        // Extract and process audio continuously
        extractAndProcessAudio(iqSamples)
        
        // Process complete lines for video
        while accumulatedSamples.count >= samplesPerLine * 2 {
            let lineData = Array(accumulatedSamples.prefix(samplesPerLine * 2))
            accumulatedSamples.removeFirst(samplesPerLine * 2)
            
            processVideoLine(lineData)
            
            currentLine += 1
            if currentLine >= linesPerFrame {
                currentLine = 0
                // Return completed frame
                return flattenFrame()
            }
        }
        
        return nil
    }
    
    private func processVideoLine(_ iqSamples: [Int8]) {
        // Convert IQ to amplitude (AM demodulation for video)
        let sampleCount = iqSamples.count / 2
        var amplitude = [Float](repeating: 0, count: sampleCount)
        
        for i in 0..<sampleCount {
            let iVal = Float(iqSamples[i * 2]) / 127.0
            let qVal = Float(iqSamples[i * 2 + 1]) / 127.0
            amplitude[i] = sqrt(iVal * iVal + qVal * qVal)
        }
        
        // Skip vertical blanking interval (first 49 lines are not visible)
        guard currentLine >= 49 && currentLine < (49 + activeLines) else {
            return
        }
        
        let visibleLine = currentLine - 49
        guard visibleLine < imageHeight else { return }
        
        // Extract active video portion (skip sync and blanking)
        // Typical timing: sync pulse ~5µs, back porch ~6µs, active video ~52µs
        let syncSamples = Int(Float(sampleRate) * 5e-6)
        let backPorchSamples = Int(Float(sampleRate) * 6e-6)
        let startSample = syncSamples + backPorchSamples
        
        guard startSample < amplitude.count else { return }
        
        let activeVideo = Array(amplitude[startSample...])
        
        // Resample to image width
        let resampledLine = resample(activeVideo, toLength: imageWidth)
        
        // Convert to grayscale (0-255)
        for x in 0..<min(resampledLine.count, imageWidth) {
            let normalized = max(0, min(1, resampledLine[x]))
            frameBuffer[visibleLine][x] = UInt8(normalized * 255)
        }
    }
    
    private func resample(_ signal: [Float], toLength: Int) -> [Float] {
        guard signal.count > 0, toLength > 0 else { return [] }
        
        var output = [Float](repeating: 0, count: toLength)
        let ratio = Float(signal.count) / Float(toLength)
        
        for i in 0..<toLength {
            let srcIndex = Float(i) * ratio
            let idx = Int(srcIndex)
            let frac = srcIndex - Float(idx)
            
            if idx + 1 < signal.count {
                // Linear interpolation
                output[i] = signal[idx] * (1 - frac) + signal[idx + 1] * frac
            } else if idx < signal.count {
                output[i] = signal[idx]
            }
        }
        
        return output
    }
    
    private func flattenFrame() -> [UInt8] {
        var flatFrame = [UInt8]()
        flatFrame.reserveCapacity(imageWidth * imageHeight * 4)
        
        // Convert to RGBA format for display
        for y in 0..<imageHeight {
            for x in 0..<imageWidth {
                let gray = frameBuffer[y][x]
                flatFrame.append(gray)  // R
                flatFrame.append(gray)  // G
                flatFrame.append(gray)  // B
                flatFrame.append(255)   // A
            }
        }
        
        return flatFrame
    }
    
    // MARK: - Audio Processing
    
    private func extractAndProcessAudio(_ iqSamples: [Int8]) {
        guard iqSamples.count >= 2 else { return }
        
        let sampleCount = iqSamples.count / 2
        
        // Convert IQ samples to complex numbers
        var i = [Float](repeating: 0, count: sampleCount)
        var q = [Float](repeating: 0, count: sampleCount)
        
        for idx in 0..<sampleCount {
            i[idx] = Float(iqSamples[idx * 2]) / 127.0
            q[idx] = Float(iqSamples[idx * 2 + 1]) / 127.0
        }
        
        // Frequency shift to move audio carrier to baseband
        // Audio is at +5.5 MHz from video carrier
        let shiftedIQ = frequencyShift(i: i, q: q, shift: audioOffset)
        
        // FM demodulation for audio
        var audioSamples = fmDemodulate(i: shiftedIQ.i, q: shiftedIQ.q)
        
        // Decimate to audio rate
        let decimation = sampleRate / audioRate
        if decimation > 1 {
            audioSamples = decimateAudio(audioSamples, factor: decimation)
        }
        
        // Apply 50µs de-emphasis (PAL TV standard)
        audioSamples = applyDeemphasis(audioSamples)
        
        // Normalize
        if let maxVal = audioSamples.max(by: { abs($0) < abs($1) }), abs(maxVal) > 0 {
            let normFactor = 0.5 / abs(maxVal)
            for idx in 0..<audioSamples.count {
                audioSamples[idx] *= normFactor
            }
        }
        
        // Send to audio callback
        audioCallback?(audioSamples)
    }
    
    private func frequencyShift(i: [Float], q: [Float], shift: Float) -> (i: [Float], q: [Float]) {
        let sampleCount = i.count
        var shiftedI = [Float](repeating: 0, count: sampleCount)
        var shiftedQ = [Float](repeating: 0, count: sampleCount)
        
        let phaseIncrement = -2.0 * Float.pi * shift / Float(sampleRate)
        
        for idx in 0..<sampleCount {
            let phase = audioPhaseAccumulator + Float(idx) * phaseIncrement
            let cosVal = cos(phase)
            let sinVal = sin(phase)
            
            // Complex multiplication: (i + jq) * (cos - j*sin)
            shiftedI[idx] = i[idx] * cosVal + q[idx] * sinVal
            shiftedQ[idx] = q[idx] * cosVal - i[idx] * sinVal
        }
        
        // Update phase accumulator for next call
        audioPhaseAccumulator += Float(sampleCount) * phaseIncrement
        audioPhaseAccumulator = audioPhaseAccumulator.truncatingRemainder(dividingBy: 2.0 * Float.pi)
        
        return (shiftedI, shiftedQ)
    }
    
    private func fmDemodulate(i: [Float], q: [Float]) -> [Float] {
        let sampleCount = i.count
        guard sampleCount > 1 else { return [] }
        
        // Calculate phase
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
        
        return demod
    }
    
    private func decimateAudio(_ signal: [Float], factor: Int) -> [Float] {
        guard factor > 1 else { return signal }
        
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
        guard signal.count > 0 else { return [] }
        
        var output = [Float](repeating: 0, count: signal.count)
        
        // Simple IIR filter implementation
        // y[n] = b0*x[n] - a1*y[n-1]
        let b0 = audioDemphasisB[0]
        let a1 = audioDemphasisA[1]
        
        output[0] = b0 * signal[0] - a1 * audioFilterState[0]
        for i in 1..<signal.count {
            output[i] = b0 * signal[i] - a1 * output[i - 1]
        }
        
        // Update filter state for next call
        audioFilterState[0] = output[output.count - 1]
        
        return output
    }
}
