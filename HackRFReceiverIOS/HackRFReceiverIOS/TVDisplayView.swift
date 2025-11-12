import SwiftUI

struct TVDisplayView: View {
    let videoFrame: [UInt8]
    
    private let width = 720
    private let height = 576
    
    var body: some View {
        GeometryReader { geometry in
            if !videoFrame.isEmpty {
                Canvas { context, size in
                    drawVideoFrame(context: context, size: size)
                }
                .background(Color.black)
            } else {
                VStack {
                    Image(systemName: "tv")
                        .font(.system(size: 60))
                        .foregroundColor(.gray)
                    Text("TV sinyali bekleniyor...")
                        .foregroundColor(.gray)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Color.black)
            }
        }
    }
    
    private func drawVideoFrame(context: GraphicsContext, size: CGSize) {
        let frameWidth = min(videoFrame.count / height, width)
        
        guard frameWidth > 0, height > 0 else { return }
        
        let pixelWidth = size.width / CGFloat(frameWidth)
        let pixelHeight = size.height / CGFloat(height)
        
        for y in 0..<height {
            for x in 0..<frameWidth {
                let index = y * frameWidth + x
                
                guard index < videoFrame.count else { continue }
                
                let gray = videoFrame[index]
                let color = Color(
                    red: Double(gray) / 255.0,
                    green: Double(gray) / 255.0,
                    blue: Double(gray) / 255.0
                )
                
                let rect = CGRect(
                    x: CGFloat(x) * pixelWidth,
                    y: CGFloat(y) * pixelHeight,
                    width: pixelWidth + 0.5,  // Slight overlap to avoid gaps
                    height: pixelHeight + 0.5
                )
                
                context.fill(Path(rect), with: .color(color))
            }
        }
    }
}
