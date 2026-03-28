import SwiftUI

struct ContentView: View {
    @StateObject private var decoder = PALDecoderManager()
    @State private var hostIP = "192.168.1.6"

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

    @State private var selectedSampleRate: Int = 12500000
    @State private var radioMode = false

    private var connected: Bool { decoder.isConnected }

    private let tvSampleRates: [(label: String, value: Int)] = [
        ("2 MHz",    2000000),
        ("4 MHz",    4000000),
        ("8 MHz",    8000000),
        ("12.5 MHz", 12500000),
        ("16 MHz",   16000000),
        ("20 MHz",   20000000),
    ]

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 12) {
                    // Display
                    if radioMode {
                        radioDisplay
                    } else {
                        videoDisplay
                    }

                    // Connection - always visible
                    connectionSection

                    // Everything else - only when connected
                    if connected {
                        modeSection
                        statusBar
                        frequencySection
                        rfGainSection
                        if !radioMode {
                            sampleRateSection
                            videoSection
                        }
                        audioSection
                    }
                }
                .padding(.horizontal, 12)
                .padding(.bottom, 20)
            }
            .background(Color.black)
            .navigationTitle(radioMode ? "FM Radio" : "PAL-B TV")
            .navigationBarTitleDisplayMode(.inline)
            .preferredColorScheme(.dark)
        }
    }

    // MARK: - Display

    private var radioDisplay: some View {
        VStack(spacing: 8) {
            Image(systemName: "radio")
                .font(.system(size: 48))
                .foregroundColor(.orange)
            Text(String(format: "%.1f MHz", frequencyMHz))
                .font(.system(size: 28, weight: .bold, design: .monospaced))
                .foregroundColor(.white)
            Text(frequencyLabel)
                .font(.caption)
                .foregroundColor(.gray)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 24)
        .background(Color.black)
        .cornerRadius(12)
    }

    private var videoDisplay: some View {
        Group {
            if let frame = decoder.currentFrame {
                Image(decorative: frame, scale: 1.0)
                    .resizable()
                    .aspectRatio(720.0 / 576.0, contentMode: .fit)
                    .clipped()
                    .cornerRadius(8)
            } else {
                Rectangle()
                    .fill(Color(.darkGray).opacity(0.3))
                    .aspectRatio(720.0 / 576.0, contentMode: .fit)
                    .overlay(
                        VStack {
                            Image(systemName: "tv")
                                .font(.system(size: 48))
                                .foregroundColor(.gray)
                            Text(connected ? "No Signal" : "Not Connected")
                                .foregroundColor(.gray)
                        }
                    )
                    .cornerRadius(8)
            }
        }
    }

    // MARK: - Connection

    private var connectionSection: some View {
        GroupBox("Connection") {
            HStack {
                TextField("Server IP", text: $hostIP)
                    .textFieldStyle(.roundedBorder)
                    .keyboardType(.decimalPad)
                    .autocorrectionDisabled()
                    .disabled(connected)
                Button(connected ? "Disconnect" : "Connect") {
                    if connected {
                        decoder.disconnect()
                        radioMode = false
                    } else {
                        decoder.connect(host: hostIP)
                    }
                }
                .buttonStyle(.borderedProminent)
                .tint(connected ? .red : .green)
            }
        }
    }

    // MARK: - Mode

    private var modeSection: some View {
        GroupBox("Mode") {
            HStack(spacing: 12) {
                Button {
                    guard radioMode else { return }
                    radioMode = false
                    frequencyMHz = 479.3
                    // Defer the heavy work to next runloop
                    DispatchQueue.main.async {
                        decoder.frequency = UInt64(frequencyMHz * 1_000_000)
                        decoder.setRadioMode(false)
                    }
                } label: {
                    Label("TV", systemImage: "tv").frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .tint(!radioMode ? .blue : .gray)

                Button {
                    guard !radioMode else { return }
                    radioMode = true
                    frequencyMHz = 100.0
                    // Defer the heavy work to next runloop
                    DispatchQueue.main.async {
                        decoder.frequency = UInt64(frequencyMHz * 1_000_000)
                        decoder.setRadioMode(true)
                    }
                } label: {
                    Label("Radio", systemImage: "radio").frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .tint(radioMode ? .orange : .gray)
            }
        }
    }

    // MARK: - Status

    private var statusBar: some View {
        VStack(spacing: 4) {
            HStack(spacing: 12) {
                if !radioMode {
                    Label(String(format: "%.1f FPS", decoder.fps), systemImage: "film")
                        .font(.caption)
                    Label(String(format: "Sync: %.0f%%", decoder.syncQuality), systemImage: "waveform")
                        .font(.caption)
                        .foregroundColor(decoder.syncQuality >= 95 ? .green :
                                         decoder.syncQuality >= 85 ? .yellow :
                                         decoder.syncQuality >= 70 ? .orange : .red)
                }
                Label(String(format: "%.1f MB/s", decoder.tcpClient.dataRate), systemImage: "arrow.down.circle")
                    .font(.caption)
                Text(sampleRateLabel)
                    .font(.caption.bold())
                    .foregroundColor(.cyan)
            }
            if !radioMode {
                Text(decoder.bufferStatus)
                    .font(.caption.bold())
                    .foregroundColor(.yellow)
            }
        }
        .padding(.vertical, 4)
    }

    private var sampleRateLabel: String {
        let sr = decoder.sampleRate
        if sr >= 1000000 { return "\(sr / 1000000) MHz" }
        return "\(sr / 1000) kHz"
    }

    // MARK: - Frequency

    private struct BandDef {
        let name: String
        let min: Double
        let max: Double
        let step: Double
        let defaultFreq: Double
    }

    private let radioBands: [BandDef] = [
        BandDef(name: "FM",    min: 87.5,  max: 108.0, step: 0.1,  defaultFreq: 100.0),
        BandDef(name: "AIR",   min: 118.0, max: 137.0, step: 0.025, defaultFreq: 121.5),
        BandDef(name: "2m",    min: 144.0, max: 148.0, step: 0.0125, defaultFreq: 145.0),
        BandDef(name: "70cm",  min: 430.0, max: 440.0, step: 0.0125, defaultFreq: 433.0),
        BandDef(name: "868",   min: 862.0, max: 870.0, step: 0.1,  defaultFreq: 868.0),
        BandDef(name: "ALL",   min: 1.0,   max: 6000.0, step: 0.1, defaultFreq: 100.0),
    ]

    @State private var selectedBandIndex: Int = 0

    private var currentBand: BandDef {
        radioBands[selectedBandIndex]
    }

    private var frequencySection: some View {
        GroupBox("Frequency") {
            VStack(spacing: 6) {
                HStack {
                    Text(String(format: "%.3f MHz", frequencyMHz))
                        .font(.title2.monospacedDigit().bold())
                    Spacer()
                    Text(frequencyLabel)
                        .font(.caption)
                        .padding(.horizontal, 8).padding(.vertical, 4)
                        .background(radioMode ? Color.orange.opacity(0.2) : Color.blue.opacity(0.2))
                        .cornerRadius(4)
                }

                if radioMode {
                    // Band selector buttons
                    HStack(spacing: 6) {
                        ForEach(radioBands.indices, id: \.self) { i in
                            Button(radioBands[i].name) {
                                selectedBandIndex = i
                                frequencyMHz = radioBands[i].defaultFreq
                                applyFrequency()
                            }
                            .font(.caption2.bold())
                            .buttonStyle(.borderedProminent)
                            .tint(selectedBandIndex == i ? .orange : .gray)
                        }
                    }

                    // Slider locked to selected band range
                    Slider(value: $frequencyMHz,
                           in: currentBand.min...currentBand.max,
                           step: currentBand.step) {
                        Text("Freq")
                    } onEditingChanged: { editing in
                        if !editing { applyFrequency() }
                    }

                    HStack {
                        Text(String(format: "%.1f", currentBand.min))
                            .font(.caption2).foregroundColor(.gray)
                        Spacer()
                        Text("\(currentBand.name) Band")
                            .font(.caption2.bold()).foregroundColor(.orange)
                        Spacer()
                        Text(String(format: "%.1f", currentBand.max))
                            .font(.caption2).foregroundColor(.gray)
                    }
                } else {
                    // TV slider
                    Slider(value: $frequencyMHz, in: 47...862, step: 0.1) {
                        Text("Freq")
                    } onEditingChanged: { editing in
                        if !editing { applyFrequency() }
                    }
                }
            }
        }
    }

    private var frequencyLabel: String {
        let f = frequencyMHz
        if radioMode {
            if f >= 87.5 && f <= 108.0 { return "FM Band" }
            if f >= 118.0 && f <= 137.0 { return "AIR Band" }
            if f >= 144.0 && f <= 148.0 { return "2m HAM" }
            if f >= 430.0 && f <= 440.0 { return "70cm HAM" }
            if f >= 862.0 && f <= 870.0 { return "868 ISM" }
            return String(format: "%.1f MHz", f)
        } else {
            if f >= 470, f <= 862 { return "UHF E\(Int((f - 306) / 8))" }
            if f >= 174, f <= 230 { return "VHF-III E\(Int((f - 175) / 8) + 5)" }
            return "Custom"
        }
    }

    // MARK: - Sample Rate (TV only)

    private var sampleRateSection: some View {
        GroupBox("Bandwidth / Sample Rate") {
            Picker("Sample Rate", selection: $selectedSampleRate) {
                ForEach(tvSampleRates, id: \.value) { rate in
                    Text(rate.label).tag(rate.value)
                }
            }
            .pickerStyle(.segmented)
            .onChange(of: selectedSampleRate) { _, newRate in
                decoder.changeSampleRate(newRate)
            }
        }
    }

    // MARK: - RF Gain

    private var rfGainSection: some View {
        GroupBox("RF Gain") {
            VStack(spacing: 6) {
                sliderRow("LNA", value: $lnaGain, range: 0...40, step: 8) { decoder.setLnaGain(UInt(lnaGain)) }
                sliderRow("VGA", value: $vgaGain, range: 0...62, step: 2) { decoder.setVgaGain(UInt(vgaGain)) }
                sliderRow("RX Amp", value: $rxAmpGain, range: 0...14, step: 1) { decoder.setRxAmpGain(UInt(rxAmpGain)) }
            }
        }
    }

    // MARK: - Video (TV only)

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

    // MARK: - Audio

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

    // MARK: - Helpers

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
