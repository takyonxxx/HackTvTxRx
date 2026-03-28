import SwiftUI

struct ContentView: View {
    @StateObject private var decoder = PALDecoderManager()
    @State private var hostIP = "192.168.1.6"
    @State private var isConnected = false

    @State private var videoGain: Float = 1.5
    @State private var videoOffset: Float = 0.0
    @State private var invertVideo = true
    @State private var colorMode = false
    @State private var chromaGain: Float = 0.75
    @State private var syncThreshold: Float = 0.0

    @State private var audioEnabled = true
    @State private var audioGain: Float = 1.0
    @State private var volume: Float = 0.1

    @State private var frequencyMHz: Double = 479.3
    @State private var lnaGain: Double = 40
    @State private var vgaGain: Double = 30
    @State private var rxAmpGain: Double = 14

    @State private var selectedSampleRate: Int = 16000000
    @State private var showControls = true

    private let sampleRates: [(label: String, value: Int)] = [
        ("12.5 MHz", 12500000),
        ("16 MHz",   16000000),
        ("20 MHz",   20000000),
    ]

    var body: some View {
        NavigationStack {
            ZStack {
                Color.black.ignoresSafeArea()
                VStack(spacing: 0) {
                    videoDisplay
                        .onTapGesture { withAnimation { showControls.toggle() } }
                    if showControls {
                        ScrollView {
                            VStack(spacing: 12) {
                                connectionSection
                                statusBar
                                sampleRateSection
                                frequencySection
                                rfGainSection
                                videoSection
                                audioSection
                            }
                            .padding(.horizontal, 12)
                            .padding(.bottom, 20)
                        }
                        .background(Color(.systemGroupedBackground))
                    }
                }
            }
            .navigationTitle("PAL-B Decoder")
            .navigationBarTitleDisplayMode(.inline)
            .preferredColorScheme(.dark)
        }
    }

    private var videoDisplay: some View {
        Group {
            if let frame = decoder.currentFrame {
                Image(decorative: frame, scale: 1.0)
                    .resizable()
                    .aspectRatio(720.0 / 576.0, contentMode: .fit)
                    .clipped()
            } else {
                Rectangle()
                    .fill(Color.black)
                    .aspectRatio(720.0 / 576.0, contentMode: .fit)
                    .overlay(
                        VStack {
                            Image(systemName: "tv")
                                .font(.system(size: 48))
                                .foregroundColor(.gray)
                            Text("No Signal")
                                .foregroundColor(.gray)
                        }
                    )
            }
        }
    }

    private var connectionSection: some View {
        GroupBox("Connection") {
            HStack {
                TextField("Server IP", text: $hostIP)
                    .textFieldStyle(.roundedBorder)
                    .keyboardType(.decimalPad)
                    .autocorrectionDisabled()
                Button(isConnected ? "Disconnect" : "Connect") {
                    if isConnected {
                        decoder.disconnect()
                        isConnected = false
                    } else {
                        decoder.connect(host: hostIP)
                        isConnected = true
                    }
                }
                .buttonStyle(.borderedProminent)
                .tint(isConnected ? .red : .green)
            }
        }
    }

    private var statusBar: some View {
        HStack(spacing: 16) {
            Label(String(format: "%.1f FPS", decoder.fps), systemImage: "film")
                .font(.caption)
            Label(String(format: "%.1f MB/s", decoder.tcpClient.dataRate), systemImage: "arrow.down.circle")
                .font(.caption)
            Label(String(format: "Sync: %.0f%%", decoder.syncQuality), systemImage: "waveform")
                .font(.caption)
                .foregroundColor(decoder.syncQuality >= 95 ? .green :
                                 decoder.syncQuality >= 85 ? .yellow :
                                 decoder.syncQuality >= 70 ? .orange : .red)
            Text("\(decoder.sampleRate / 1000000) MHz")
                .font(.caption.bold())
                .foregroundColor(.cyan)
        }
        .padding(.vertical, 4)
    }

    // MARK: - Sample Rate Picker

    private var sampleRateSection: some View {
        GroupBox("Bandwidth / Sample Rate") {
            Picker("Sample Rate", selection: $selectedSampleRate) {
                ForEach(sampleRates, id: \.value) { rate in
                    Text(rate.label).tag(rate.value)
                }
            }
            .pickerStyle(.segmented)
            .onChange(of: selectedSampleRate) { _, newRate in
                decoder.changeSampleRate(newRate)
            }
        }
    }

    private var frequencySection: some View {
        GroupBox("Frequency") {
            VStack(spacing: 6) {
                HStack {
                    Text(String(format: "%.3f MHz", frequencyMHz))
                        .font(.title2.monospacedDigit().bold())
                    Spacer()
                    Text(channelLabel)
                        .font(.caption)
                        .padding(.horizontal, 8).padding(.vertical, 4)
                        .background(Color.blue.opacity(0.2))
                        .cornerRadius(4)
                }
                Slider(value: $frequencyMHz, in: 47...862, step: 0.1) {
                    Text("Freq")
                } onEditingChanged: { editing in
                    if !editing { applyFrequency() }
                }
            }
        }
    }

    private var channelLabel: String {
        let f = frequencyMHz
        if f >= 470, f <= 862 { return "UHF E\(Int((f - 306) / 8))" }
        if f >= 174, f <= 230 { return "VHF-III E\(Int((f - 175) / 8) + 5)" }
        return "Custom"
    }

    private var rfGainSection: some View {
        GroupBox("RF Gain") {
            VStack(spacing: 6) {
                sliderRow("LNA", value: $lnaGain, range: 0...40, step: 8) { decoder.setLnaGain(UInt(lnaGain)) }
                sliderRow("VGA", value: $vgaGain, range: 0...62, step: 2) { decoder.setVgaGain(UInt(vgaGain)) }
                sliderRow("RX Amp", value: $rxAmpGain, range: 0...14, step: 1) { decoder.setRxAmpGain(UInt(rxAmpGain)) }
            }
        }
    }

    private var videoSection: some View {
        GroupBox("Video") {
            VStack(spacing: 6) {
                floatSliderRow("Gain", value: $videoGain, range: 0.1...5.0) { decoder.setVideoGain(videoGain) }
                floatSliderRow("Offset", value: $videoOffset, range: -1.0...1.0) { decoder.setVideoOffset(videoOffset) }
                floatSliderRow("Sync Thr", value: $syncThreshold, range: 0...0.5) { decoder.setSyncThreshold(syncThreshold) }
                HStack {
                    Toggle("Invert", isOn: $invertVideo)
                        .onChange(of: invertVideo) { _, v in decoder.setVideoInvert(v) }
                    Toggle("Color", isOn: $colorMode)
                        .onChange(of: colorMode) { _, v in decoder.setColorMode(v) }
                }
                .toggleStyle(.button).font(.caption)
                if colorMode {
                    floatSliderRow("Chroma", value: $chromaGain, range: 0...2.0) { decoder.setChromaGain(chromaGain) }
                }
            }
        }
    }

    private var audioSection: some View {
        GroupBox("Audio") {
            VStack(spacing: 6) {
                Toggle("Enable Audio", isOn: $audioEnabled)
                    .onChange(of: audioEnabled) { _, v in decoder.setAudioEnabled(v) }
                floatSliderRow("Gain", value: $audioGain, range: 0...5.0) { decoder.setAudioGain(audioGain) }
                floatSliderRow("Volume", value: $volume, range: 0...1.0) { decoder.setVolume(volume) }
            }
        }
    }

    private func applyFrequency() {
        decoder.frequency = UInt64(frequencyMHz * 1_000_000)
        decoder.updateFrequency()
    }

    private func sliderRow(_ label: String, value: Binding<Double>, range: ClosedRange<Double>,
                           step: Double, onChanged: @escaping () -> Void) -> some View {
        HStack {
            Text(label).frame(width: 55, alignment: .leading).font(.caption)
            Slider(value: value, in: range, step: step) { editing in
                if !editing { onChanged() }
            }
            Text("\(Int(value.wrappedValue))").frame(width: 30).font(.caption.monospacedDigit())
        }
    }

    private func floatSliderRow(_ label: String, value: Binding<Float>, range: ClosedRange<Float>,
                                onChanged: @escaping () -> Void) -> some View {
        HStack {
            Text(label).frame(width: 55, alignment: .leading).font(.caption)
            Slider(value: value, in: range) { editing in
                if !editing { onChanged() }
            }
            Text(String(format: "%.2f", value.wrappedValue)).frame(width: 40).font(.caption.monospacedDigit())
        }
    }
}

#Preview { ContentView() }
