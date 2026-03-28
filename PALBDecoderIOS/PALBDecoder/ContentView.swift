import SwiftUI

struct ContentView: View {
    @StateObject private var decoder = PALDecoderManager()

    // Persistent settings via @AppStorage
    @AppStorage("hostIP") private var hostIP = "192.168.1.6"
    @AppStorage("radioMode") private var radioMode = false

    // TV last settings
    @AppStorage("tvFreqMHz") private var tvFreqMHz: Double = 640.0
    @AppStorage("tvSampleRate") private var tvSampleRate: Int = 12500000
    @AppStorage("tvLnaGain") private var tvLnaGain: Double = 40
    @AppStorage("tvVgaGain") private var tvVgaGain: Double = 30
    @AppStorage("tvRxAmpGain") private var tvRxAmpGain: Double = 14
    @AppStorage("tvVideoGain") private var tvVideoGain: Double = 1.5
    @AppStorage("tvVideoOffset") private var tvVideoOffset: Double = 0.0
    @AppStorage("tvInvertVideo") private var tvInvertVideo = true
    @AppStorage("tvColorMode") private var tvColorMode = false
    @AppStorage("tvChromaGain") private var tvChromaGain: Double = 0.75
    @AppStorage("tvSyncThreshold") private var tvSyncThreshold: Double = 0.0

    // Radio last settings
    @AppStorage("radioFreqMHz") private var radioFreqMHz: Double = 100.0
    @AppStorage("radioLnaGain") private var radioLnaGain: Double = 40
    @AppStorage("radioVgaGain") private var radioVgaGain: Double = 30
    @AppStorage("radioRxAmpGain") private var radioRxAmpGain: Double = 14
    @AppStorage("radioBandIndex") private var radioBandIndex: Int = 0

    // Audio (shared)
    @AppStorage("audioEnabled") private var audioEnabled = true
    @AppStorage("audioGain") private var savedAudioGain: Double = 1.0
    @AppStorage("volume") private var savedVolume: Double = 0.1

    // Active UI state (driven from persisted values)
    @State private var frequencyMHz: Double = 640.0
    @State private var lnaGain: Double = 40
    @State private var vgaGain: Double = 30
    @State private var rxAmpGain: Double = 14
    @State private var videoGain: Float = 1.5
    @State private var videoOffset: Float = 0.0
    @State private var invertVideo = true
    @State private var colorMode = false
    @State private var chromaGain: Float = 0.75
    @State private var syncThreshold: Float = 0.0
    @State private var audioGainUI: Float = 1.0
    @State private var volumeUI: Float = 0.1
    @State private var selectedSampleRate: Int = 12500000
    @State private var selectedBandIndex: Int = 0
    @State private var didInitialize = false

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
                    if radioMode { radioDisplay } else { videoDisplay }
                    connectionSection
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
            .onAppear { restoreSettings() }
        }
    }

    // MARK: - Restore persisted settings on launch

    private func restoreSettings() {
        guard !didInitialize else { return }
        didInitialize = true

        if radioMode {
            frequencyMHz = radioFreqMHz
            lnaGain = radioLnaGain
            vgaGain = radioVgaGain
            rxAmpGain = radioRxAmpGain
            selectedBandIndex = radioBandIndex
        } else {
            frequencyMHz = tvFreqMHz
            lnaGain = tvLnaGain
            vgaGain = tvVgaGain
            rxAmpGain = tvRxAmpGain
            selectedSampleRate = tvSampleRate
        }
        videoGain = Float(tvVideoGain)
        videoOffset = Float(tvVideoOffset)
        invertVideo = tvInvertVideo
        colorMode = tvColorMode
        chromaGain = Float(tvChromaGain)
        syncThreshold = Float(tvSyncThreshold)
        audioGainUI = Float(savedAudioGain)
        volumeUI = Float(savedVolume)

        // Apply to decoder
        decoder.radioMode = radioMode
        decoder.sampleRate = radioMode ? 2000000 : selectedSampleRate
        decoder.frequency = UInt64(frequencyMHz * 1_000_000)
    }

    // MARK: - Save current mode settings

    private func saveTVSettings() {
        tvFreqMHz = frequencyMHz
        tvSampleRate = selectedSampleRate
        tvLnaGain = lnaGain
        tvVgaGain = vgaGain
        tvRxAmpGain = rxAmpGain
        tvVideoGain = Double(videoGain)
        tvVideoOffset = Double(videoOffset)
        tvInvertVideo = invertVideo
        tvColorMode = colorMode
        tvChromaGain = Double(chromaGain)
        tvSyncThreshold = Double(syncThreshold)
    }

    private func saveRadioSettings() {
        radioFreqMHz = frequencyMHz
        radioLnaGain = lnaGain
        radioVgaGain = vgaGain
        radioRxAmpGain = rxAmpGain
        radioBandIndex = selectedBandIndex
    }

    private func saveAudioSettings() {
        savedAudioGain = Double(audioGainUI)
        savedVolume = Double(volumeUI)
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
                    // Save radio settings before switching
                    saveRadioSettings()
                    radioMode = false
                    // Restore TV settings
                    frequencyMHz = tvFreqMHz
                    lnaGain = tvLnaGain
                    vgaGain = tvVgaGain
                    rxAmpGain = tvRxAmpGain
                    selectedSampleRate = tvSampleRate
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
                    // Save TV settings before switching
                    saveTVSettings()
                    radioMode = true
                    // Restore radio settings
                    frequencyMHz = radioFreqMHz
                    lnaGain = radioLnaGain
                    vgaGain = radioVgaGain
                    rxAmpGain = radioRxAmpGain
                    selectedBandIndex = radioBandIndex
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

                // Fine-tune buttons
                HStack(spacing: 8) {
                    Button { nudgeFreq(-0.5) } label: { Text("-0.5").font(.caption2.bold()).frame(width: 36, height: 26) }
                        .buttonStyle(.bordered).tint(.red)
                    Button { nudgeFreq(-0.05) } label: { Text("-0.05").font(.caption2.bold()).frame(width: 40, height: 26) }
                        .buttonStyle(.bordered).tint(.red)
                    Spacer()
                    Button { nudgeFreq(+0.05) } label: { Text("+0.05").font(.caption2.bold()).frame(width: 40, height: 26) }
                        .buttonStyle(.bordered).tint(.green)
                    Button { nudgeFreq(+0.5) } label: { Text("+0.5").font(.caption2.bold()).frame(width: 36, height: 26) }
                        .buttonStyle(.bordered).tint(.green)
                }

                if radioMode {
                    HStack(spacing: 6) {
                        ForEach(radioBands.indices, id: \.self) { i in
                            Button(radioBands[i].name) {
                                selectedBandIndex = i
                                frequencyMHz = radioBands[i].defaultFreq
                                applyFrequency()
                                saveRadioSettings()
                            }
                            .font(.caption2.bold())
                            .buttonStyle(.borderedProminent)
                            .tint(selectedBandIndex == i ? .orange : .gray)
                        }
                    }

                    Slider(value: $frequencyMHz,
                           in: currentBand.min...currentBand.max,
                           step: currentBand.step) {
                        Text("Freq")
                    } onEditingChanged: { editing in
                        if !editing { applyFrequency(); saveRadioSettings() }
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
                    Slider(value: $frequencyMHz, in: 47...862, step: 0.1) {
                        Text("Freq")
                    } onEditingChanged: { editing in
                        if !editing { applyFrequency(); saveTVSettings() }
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
                saveTVSettings()
            }
        }
    }

    // MARK: - RF Gain

    private var rfGainSection: some View {
        GroupBox("RF Gain") {
            VStack(spacing: 6) {
                sliderRow("LNA", value: $lnaGain, range: 0...40, step: 8) {
                    decoder.setLnaGain(UInt(lnaGain))
                    if radioMode { saveRadioSettings() } else { saveTVSettings() }
                }
                sliderRow("VGA", value: $vgaGain, range: 0...62, step: 2) {
                    decoder.setVgaGain(UInt(vgaGain))
                    if radioMode { saveRadioSettings() } else { saveTVSettings() }
                }
                sliderRow("RX Amp", value: $rxAmpGain, range: 0...14, step: 1) {
                    decoder.setRxAmpGain(UInt(rxAmpGain))
                    if radioMode { saveRadioSettings() } else { saveTVSettings() }
                }
            }
        }
    }

    // MARK: - Video (TV only)

    private var videoSection: some View {
        GroupBox("Video") {
            VStack(spacing: 6) {
                floatSliderRow("Gain", value: $videoGain, range: 0.1...5.0) { decoder.setVideoGain(videoGain); saveTVSettings() }
                floatSliderRow("Offset", value: $videoOffset, range: -1.0...1.0) { decoder.setVideoOffset(videoOffset); saveTVSettings() }
                floatSliderRow("Sync Thr", value: $syncThreshold, range: 0...0.5) { decoder.setSyncThreshold(syncThreshold); saveTVSettings() }
                HStack {
                    Toggle("Invert", isOn: $invertVideo)
                        .onChange(of: invertVideo) { _, v in decoder.setVideoInvert(v); saveTVSettings() }
                    Toggle("Color", isOn: $colorMode)
                        .onChange(of: colorMode) { _, v in decoder.setColorMode(v); saveTVSettings() }
                }
                .toggleStyle(.button).font(.caption)
                if colorMode {
                    floatSliderRow("Chroma", value: $chromaGain, range: 0...2.0) { decoder.setChromaGain(chromaGain); saveTVSettings() }
                }
            }
        }
    }

    // MARK: - Audio

    private var audioSection: some View {
        GroupBox("Audio") {
            VStack(spacing: 6) {
                Toggle("Enable Audio", isOn: $audioEnabled)
                    .onChange(of: audioEnabled) { _, v in decoder.setAudioEnabled(v); saveAudioSettings() }
                floatSliderRow("Gain", value: $audioGainUI, range: 0...5.0) { decoder.setAudioGain(audioGainUI); saveAudioSettings() }
                floatSliderRow("Volume", value: $volumeUI, range: 0...1.0) { decoder.setVolume(volumeUI); saveAudioSettings() }
            }
        }
    }

    // MARK: - Helpers

    private func applyFrequency() {
        decoder.frequency = UInt64(frequencyMHz * 1_000_000)
        decoder.updateFrequency()
    }

    private func nudgeFreq(_ deltaMHz: Double) {
        frequencyMHz = max(1.0, min(6000.0, frequencyMHz + deltaMHz))
        applyFrequency()
        if radioMode { saveRadioSettings() } else { saveTVSettings() }
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
