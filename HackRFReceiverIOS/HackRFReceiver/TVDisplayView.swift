//
//  TVDisplayView.swift
//  HackRFReceiver
//
//  SwiftUI view for displaying decoded PAL TV signals
//

import SwiftUI

struct TVDisplayView: View {
    @ObservedObject var receiver: HackRFReceiver
    
    var body: some View {
        ZStack {
            Color.black
            
            if let imageData = receiver.tvImage {
                TVImageView(imageData: imageData)
            } else {
                VStack {
                    Image(systemName: "tv")
                        .font(.system(size: 60))
                        .foregroundColor(.gray)
                    Text("Waiting for video signal...")
                        .foregroundColor(.gray)
                        .padding()
                }
            }
        }
    }
}

struct TVImageView: View {
    let imageData: [UInt8]
    
    var body: some View {
        GeometryReader { geometry in
            if let cgImage = createCGImage(from: imageData) {
                Image(decorative: cgImage, scale: 1.0)
                    .resizable()
                    .aspectRatio(contentMode: .fit)
            }
        }
    }
    
    private func createCGImage(from data: [UInt8]) -> CGImage? {
        let width = 720
        let height = 576
        let bytesPerPixel = 4
        let bytesPerRow = width * bytesPerPixel
        
        guard data.count == width * height * bytesPerPixel else {
            return nil
        }
        
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        let bitmapInfo = CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue)
        
        guard let providerRef = CGDataProvider(data: Data(data) as CFData) else {
            return nil
        }
        
        return CGImage(
            width: width,
            height: height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: bytesPerRow,
            space: colorSpace,
            bitmapInfo: bitmapInfo,
            provider: providerRef,
            decode: nil,
            shouldInterpolate: true,
            intent: .defaultIntent
        )
    }
}
