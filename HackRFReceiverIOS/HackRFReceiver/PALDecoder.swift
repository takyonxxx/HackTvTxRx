//
//  PALDecoder.swift
//  HackRFReceiver
//
//  PAL-B/G analog TV signal decoder
//  625 lines, 25 fps, 16 MHz sample rate
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
    
    private var accumulatedSamples: [Int8] = []
    
    init(sampleRate: Int) {
        self.sampleRate = sampleRate
        self.samplesPerLine = Int(Float(sampleRate) / lineFrequency)
        
        // Initialize frame buffer
        frameBuffer = Array(repeating: Array(repeating: 0, count: imageWidth), count: imageHeight)
        
        print("PAL Decoder initialized: \(sampleRate) Hz, \(samplesPerLine) samples/line")
    }
    
    func processSignal(_ iqSamples: [Int8]) -> [UInt8]? {
        // Accumulate samples
        accumulatedSamples.append(contentsOf: iqSamples)
        
        // Process complete lines
        while accumulatedSamples.count >= samplesPerLine * 2 {
            let lineData = Array(accumulatedSamples.prefix(samplesPerLine * 2))
            accumulatedSamples.removeFirst(samplesPerLine * 2)
            
            processLine(lineData)
            
            currentLine += 1
            if currentLine >= linesPerFrame {
                currentLine = 0
                // Return completed frame
                return flattenFrame()
            }
        }
        
        return nil
    }
    
    private func processLine(_ iqSamples: [Int8]) {
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
}
