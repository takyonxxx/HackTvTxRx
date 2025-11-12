import SwiftUI

enum ReceiverMode: String, CaseIterable {
    case fm = "FM Radyo"
    case am = "AM Radyo"
    case nfm = "NFM Telsiz"
    case tv = "PAL-B/G TV"
    
    var sampleRate: Int {
        switch self {
        case .fm, .am, .nfm:
            return 2_000_000  // 2 MHz
        case .tv:
            return 16_000_000  // 16 MHz (TV için kritik!)
        }
    }
    
    var description: String {
        switch self {
        case .fm:
            return "88-108 MHz, Geniş bant FM"
        case .am:
            return "540-1700 kHz, Havacılık 118-137 MHz"
        case .nfm:
            return "VHF/UHF Telsiz (136-174, 400-470 MHz)"
        case .tv:
            return "PAL-B/G TV (Video + Ses)"
        }
    }
}

struct ContentView: View {
    @StateObject private var receiver = HackRFReceiver()
    
    @State private var serverIP = "192.168.1.2"
    @State private var serverPort = "5000"
    @State private var controlPort = "5001"
    @State private var frequency = "100.0"
    @State private var selectedMode: ReceiverMode = .fm
    @State private var showSettings = false
    
    // HackRF parametreleri
    @State private var vgaGain = 30
    @State private var lnaGain = 32
    @State private var rxAmpGain = 14
    
    var body: some View {
        NavigationView {
            VStack(spacing: 20) {
                // Status bar
                statusBar
                
                // Mode selection
                modeSelector
                
                // Connection settings
                connectionSettings
                
                // Frequency control
                frequencyControl
                
                // HackRF control button
                controlButton
                
                // Connect/Disconnect button
                connectButton
                
                // TV Display (only for TV mode)
                if selectedMode == .tv && receiver.isConnected {
                    TVDisplayView(videoFrame: receiver.currentVideoFrame)
                        .frame(height: 300)
                }
                
                Spacer()
            }
            .padding()
            .navigationTitle("HackRF Alıcı")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button {
                        showSettings.toggle()
                    } label: {
                        Image(systemName: "gearshape")
                    }
                }
            }
            .sheet(isPresented: $showSettings) {
                SettingsView(
                    vgaGain: $vgaGain,
                    lnaGain: $lnaGain,
                    rxAmpGain: $rxAmpGain,
                    receiver: receiver,
                    controlPort: $controlPort
                )
            }
        }
    }
    
    // MARK: - Status Bar
    
    private var statusBar: some View {
        HStack {
            Circle()
                .fill(receiver.isConnected ? Color.green : Color.red)
                .frame(width: 12, height: 12)
            
            Text(receiver.statusMessage)
                .font(.caption)
                .foregroundColor(.secondary)
            
            Spacer()
            
            if receiver.isConnected {
                Text(selectedMode.rawValue)
                    .font(.caption)
                    .bold()
                    .foregroundColor(.blue)
            }
        }
        .padding()
        .background(Color.gray.opacity(0.1))
        .cornerRadius(10)
    }
    
    // MARK: - Mode Selector
    
    private var modeSelector: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Alıcı Modu")
                .font(.headline)
            
            Picker("Mod", selection: $selectedMode) {
                ForEach(ReceiverMode.allCases, id: \.self) { mode in
                    Text(mode.rawValue).tag(mode)
                }
            }
            .pickerStyle(.segmented)
            
            Text(selectedMode.description)
                .font(.caption)
                .foregroundColor(.secondary)
            
            if selectedMode == .tv {
                Text("⚠️ TV için 16 MHz sample rate gereklidir!")
                    .font(.caption)
                    .foregroundColor(.orange)
                    .bold()
            }
        }
    }
    
    // MARK: - Connection Settings
    
    private var connectionSettings: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Bağlantı Ayarları")
                .font(.headline)
            
            HStack {
                Text("Sunucu IP:")
                    .frame(width: 100, alignment: .leading)
                TextField("192.168.1.2", text: $serverIP)
                    .textFieldStyle(RoundedBorderTextFieldStyle())
                    .keyboardType(.numbersAndPunctuation)
            }
            
            HStack {
                Text("Data Portu:")
                    .frame(width: 100, alignment: .leading)
                TextField("5000", text: $serverPort)
                    .textFieldStyle(RoundedBorderTextFieldStyle())
                    .keyboardType(.numberPad)
            }
            
            HStack {
                Text("Kontrol Portu:")
                    .frame(width: 100, alignment: .leading)
                TextField("5001", text: $controlPort)
                    .textFieldStyle(RoundedBorderTextFieldStyle())
                    .keyboardType(.numberPad)
            }
        }
    }
    
    // MARK: - Frequency Control
    
    private var frequencyControl: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Frekans Kontrolü")
                .font(.headline)
            
            HStack {
                Text("Frekans (MHz):")
                    .frame(width: 120, alignment: .leading)
                TextField("100.0", text: $frequency)
                    .textFieldStyle(RoundedBorderTextFieldStyle())
                    .keyboardType(.decimalPad)
            }
            
            HStack(spacing: 15) {
                Button {
                    adjustFrequency(by: -1.0)
                } label: {
                    Text("-1 MHz")
                        .frame(width: 80)
                        .padding(.vertical, 10)
                        .background(Color.blue)
                        .foregroundColor(.white)
                        .cornerRadius(8)
                }
                
                Button {
                    adjustFrequency(by: -0.1)
                } label: {
                    Text("-100k")
                        .frame(width: 80)
                        .padding(.vertical, 10)
                        .background(Color.blue)
                        .foregroundColor(.white)
                        .cornerRadius(8)
                }
                
                Button {
                    adjustFrequency(by: 0.1)
                } label: {
                    Text("+100k")
                        .frame(width: 80)
                        .padding(.vertical, 10)
                        .background(Color.blue)
                        .foregroundColor(.white)
                        .cornerRadius(8)
                }
                
                Button {
                    adjustFrequency(by: 1.0)
                } label: {
                    Text("+1 MHz")
                        .frame(width: 80)
                        .padding(.vertical, 10)
                        .background(Color.blue)
                        .foregroundColor(.white)
                        .cornerRadius(8)
                }
            }
            
            // Preset frequencies based on mode
            presetFrequencies
        }
    }
    
    private var presetFrequencies: some View {
        VStack(alignment: .leading, spacing: 5) {
            Text("Hızlı Seçim:")
                .font(.caption)
                .foregroundColor(.secondary)
            
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 10) {
                    ForEach(getPresetFrequencies(), id: \.0) { freq, name in
                        Button {
                            frequency = freq
                        } label: {
                            VStack(spacing: 2) {
                                Text(name)
                                    .font(.caption2)
                                Text(freq)
                                    .font(.caption)
                                    .bold()
                            }
                            .padding(.horizontal, 10)
                            .padding(.vertical, 5)
                            .background(Color.gray.opacity(0.2))
                            .cornerRadius(5)
                        }
                    }
                }
            }
        }
    }
    
    // MARK: - Control Button
    
    private var controlButton: some View {
        Button {
            sendControlParameters()
        } label: {
            HStack {
                Image(systemName: "slider.horizontal.3")
                Text("HackRF Parametrelerini Gönder (5001)")
                    .font(.subheadline)
            }
            .frame(maxWidth: .infinity)
            .padding()
            .background(Color.orange)
            .foregroundColor(.white)
            .cornerRadius(12)
        }
    }
    
    // MARK: - Connect Button
    
    private var connectButton: some View {
        Button {
            if receiver.isConnected {
                receiver.disconnect()
            } else {
                connectToServer()
            }
        } label: {
            HStack {
                Image(systemName: receiver.isConnected ? "stop.fill" : "play.fill")
                Text(receiver.isConnected ? "Bağlantıyı Kes" : "Bağlan")
                    .font(.headline)
            }
            .frame(maxWidth: .infinity)
            .padding()
            .background(receiver.isConnected ? Color.red : Color.green)
            .foregroundColor(.white)
            .cornerRadius(12)
        }
    }
    
    // MARK: - Helper Functions
    
    private func adjustFrequency(by delta: Double) {
        if let freq = Double(frequency) {
            let newFreq = freq + delta
            frequency = String(format: "%.1f", newFreq)
            
            // Çalışırken frekans değişirse otomatik gönder
            if receiver.isConnected {
                sendFrequencyCommand()
            }
        }
    }
    
    private func getPresetFrequencies() -> [(String, String)] {
        switch selectedMode {
        case .fm:
            return [
                ("88.0", "FM Başlangıç"),
                ("95.0", "95.0"),
                ("100.0", "100.0"),
                ("105.0", "105.0"),
                ("108.0", "FM Bitiş")
            ]
        case .am:
            return [
                ("0.594", "594 kHz"),
                ("1.0", "1000 kHz"),
                ("118.1", "İstanbul Kule"),
                ("121.5", "Acil Durum"),
                ("125.0", "Havacılık")
            ]
        case .nfm:
            return [
                ("144.0", "Amatör VHF"),
                ("146.0", "2m Band"),
                ("156.8", "Denizcilik 16"),
                ("433.0", "UHF"),
                ("446.0", "PMR446")
            ]
        case .tv:
            return [
                ("48.25", "Kanal E2"),
                ("55.25", "Kanal E3"),
                ("62.25", "Kanal E4"),
                ("175.25", "VHF III"),
                ("471.25", "UHF")
            ]
        }
    }
    
    private func sendFrequencyCommand() {
        guard let freq = Double(frequency) else { return }
        let freqHz = Int(freq * 1_000_000)
        receiver.sendControlCommand("SET_FREQ:\(freqHz)", port: Int(controlPort) ?? 5001)
    }
    
    private func sendControlParameters() {
        guard let port = Int(controlPort) else { return }
        guard let freq = Double(frequency) else { return }
        
        let freqHz = Int(freq * 1_000_000)
        let sampleRate = selectedMode.sampleRate
        
        receiver.sendAllControlParameters(
            frequency: freqHz,
            sampleRate: sampleRate,
            vgaGain: vgaGain,
            lnaGain: lnaGain,
            rxAmpGain: rxAmpGain,
            port: port
        )
    }
    
    private func connectToServer() {
        guard let port = Int(serverPort) else { return }
        guard let freq = Double(frequency) else { return }
        
        let freqHz = Int(freq * 1_000_000)
        
        receiver.connect(
            to: serverIP,
            port: port,
            mode: selectedMode,
            frequency: freqHz
        )
    }
}

// MARK: - Settings View

struct SettingsView: View {
    @Binding var vgaGain: Int
    @Binding var lnaGain: Int
    @Binding var rxAmpGain: Int
    @ObservedObject var receiver: HackRFReceiver
    @Binding var controlPort: String
    @Environment(\.dismiss) var dismiss
    
    var body: some View {
        NavigationView {
            Form {
                Section(header: Text("HackRF Parametreleri")) {
                    VStack(alignment: .leading) {
                        Text("VGA Gain: \(vgaGain) dB")
                        Slider(value: Binding(
                            get: { Double(vgaGain) },
                            set: { vgaGain = Int($0) }
                        ), in: 0...62, step: 2)
                    }
                    
                    Picker("LNA Gain", selection: $lnaGain) {
                        Text("0 dB").tag(0)
                        Text("8 dB").tag(8)
                        Text("16 dB").tag(16)
                        Text("24 dB").tag(24)
                        Text("32 dB").tag(32)
                        Text("40 dB").tag(40)
                    }
                    
                    Picker("RX Amp", selection: $rxAmpGain) {
                        Text("Kapalı (0 dB)").tag(0)
                        Text("Açık (14 dB)").tag(14)
                    }
                }
                
                Section(header: Text("Kontrol Portu (5001)")) {
                    HStack {
                        Text("Port:")
                        TextField("5001", text: $controlPort)
                            .keyboardType(.numberPad)
                    }
                }
                
                Section(header: Text("Ses Kontrolü")) {
                    VStack(alignment: .leading) {
                        Text("Ses Seviyesi: \(Int(receiver.audioVolume * 100))%")
                        Slider(value: $receiver.audioVolume, in: 0...1)
                    }
                }
                
                Section(header: Text("Durum")) {
                    HStack {
                        Text("Bağlantı:")
                        Spacer()
                        Text(receiver.isConnected ? "Bağlı" : "Bağlı Değil")
                            .foregroundColor(receiver.isConnected ? .green : .red)
                    }
                    
                    Text(receiver.statusMessage)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
            .navigationTitle("Ayarlar")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Kapat") {
                        dismiss()
                    }
                }
            }
        }
    }
}
