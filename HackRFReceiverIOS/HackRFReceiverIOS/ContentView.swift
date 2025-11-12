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
    @StateObject private var settings = SettingsManager.shared
    
    @State private var frequency = "100.0"
    @State private var selectedMode: ReceiverMode = .fm
    @State private var showSettings = false
    
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
                SettingsView(receiver: receiver)
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
                Text(settings.serverIP.isEmpty ? "Ayarlanmadı" : settings.serverIP)
                    .foregroundColor(settings.serverIP.isEmpty ? .red : .primary)
                Spacer()
                Button("Ayarla") {
                    showSettings = true
                }
                .buttonStyle(.bordered)
            }
            
            HStack {
                Text("Data Portu:")
                    .frame(width: 100, alignment: .leading)
                Text("\(settings.dataPort)")
                Spacer()
                Button("Ayarla") {
                    showSettings = true
                }
                .buttonStyle(.bordered)
            }
            
            if settings.serverIP.isEmpty {
                HStack {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundColor(.orange)
                    Text("Lütfen ayarlardan IP adresini girin")
                        .font(.caption)
                        .foregroundColor(.orange)
                }
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
                Text("HackRF Parametrelerini Gönder (Port \(settings.controlPort))")
                    .font(.subheadline)
            }
            .frame(maxWidth: .infinity)
            .padding()
            .background(Color.orange)
            .foregroundColor(.white)
            .cornerRadius(12)
        }
        .disabled(settings.serverIP.isEmpty)
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
            .background(receiver.isConnected ? Color.red : (settings.serverIP.isEmpty ? Color.gray : Color.green))
            .foregroundColor(.white)
            .cornerRadius(12)
        }
        .disabled(settings.serverIP.isEmpty && !receiver.isConnected)
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
        receiver.sendControlCommand("SET_FREQ:\(freqHz)", port: settings.controlPort, host: settings.serverIP)
    }
    
    private func sendControlParameters() {
        guard let freq = Double(frequency) else { return }
        
        let freqHz = Int(freq * 1_000_000)
        let sampleRate = selectedMode.sampleRate
        
        receiver.sendAllControlParameters(
            frequency: freqHz,
            sampleRate: sampleRate,
            vgaGain: settings.vgaGain,
            lnaGain: settings.lnaGain,
            rxAmpGain: settings.rxAmpGain,
            port: settings.controlPort,
            host: settings.serverIP
        )
    }
    
    private func connectToServer() {
        guard let freq = Double(frequency) else { return }
        
        let freqHz = Int(freq * 1_000_000)
        
        receiver.connect(
            to: settings.serverIP,
            port: settings.dataPort,
            mode: selectedMode,
            frequency: freqHz
        )
    }
}

// MARK: - Settings Manager (UserDefaults)

class SettingsManager: ObservableObject {
    static let shared = SettingsManager()
    
    @Published var serverIP: String {
        didSet {
            UserDefaults.standard.set(serverIP, forKey: "serverIP")
        }
    }
    
    @Published var dataPort: Int {
        didSet {
            UserDefaults.standard.set(dataPort, forKey: "dataPort")
        }
    }
    
    @Published var controlPort: Int {
        didSet {
            UserDefaults.standard.set(controlPort, forKey: "controlPort")
        }
    }
    
    @Published var vgaGain: Int {
        didSet {
            UserDefaults.standard.set(vgaGain, forKey: "vgaGain")
        }
    }
    
    @Published var lnaGain: Int {
        didSet {
            UserDefaults.standard.set(lnaGain, forKey: "lnaGain")
        }
    }
    
    @Published var rxAmpGain: Int {
        didSet {
            UserDefaults.standard.set(rxAmpGain, forKey: "rxAmpGain")
        }
    }
    
    private init() {
        self.serverIP = UserDefaults.standard.string(forKey: "serverIP") ?? ""
        self.dataPort = UserDefaults.standard.integer(forKey: "dataPort") == 0 ? 5000 : UserDefaults.standard.integer(forKey: "dataPort")
        self.controlPort = UserDefaults.standard.integer(forKey: "controlPort") == 0 ? 5001 : UserDefaults.standard.integer(forKey: "controlPort")
        self.vgaGain = UserDefaults.standard.integer(forKey: "vgaGain") == 0 ? 30 : UserDefaults.standard.integer(forKey: "vgaGain")
        self.lnaGain = UserDefaults.standard.integer(forKey: "lnaGain") == 0 ? 32 : UserDefaults.standard.integer(forKey: "lnaGain")
        self.rxAmpGain = UserDefaults.standard.integer(forKey: "rxAmpGain") == 0 ? 14 : UserDefaults.standard.integer(forKey: "rxAmpGain")
    }
    
    func resetToDefaults() {
        serverIP = ""
        dataPort = 5000
        controlPort = 5001
        vgaGain = 30
        lnaGain = 32
        rxAmpGain = 14
    }
}

// MARK: - Settings View

struct SettingsView: View {
    @ObservedObject var settings = SettingsManager.shared
    @ObservedObject var receiver: HackRFReceiver
    @Environment(\.dismiss) var dismiss
    
    @State private var tempIP: String = ""
    @State private var tempDataPort: String = ""
    @State private var tempControlPort: String = ""
    @State private var showResetAlert = false
    
    var body: some View {
        NavigationView {
            Form {
                // Bağlantı Ayarları
                Section(header: Text("Bağlantı Ayarları")) {
                    VStack(alignment: .leading, spacing: 5) {
                        Text("HackRF Sunucu IP Adresi")
                            .font(.caption)
                            .foregroundColor(.secondary)
                        HStack {
                            TextField("Örn: 192.168.1.100", text: $tempIP)
                                .textFieldStyle(RoundedBorderTextFieldStyle())
                                .keyboardType(.numbersAndPunctuation)
                                .autocapitalization(.none)
                            
                            if !tempIP.isEmpty {
                                Button {
                                    settings.serverIP = tempIP
                                } label: {
                                    Image(systemName: "checkmark.circle.fill")
                                        .foregroundColor(.green)
                                }
                            }
                        }
                        
                        if settings.serverIP.isEmpty {
                            Text("⚠️ IP adresi girilmedi - Bağlantı kurulamaz")
                                .font(.caption)
                                .foregroundColor(.orange)
                        } else {
                            Text("✓ Kaydedildi: \(settings.serverIP)")
                                .font(.caption)
                                .foregroundColor(.green)
                        }
                    }
                    
                    VStack(alignment: .leading, spacing: 5) {
                        Text("Data Portu (IQ Stream)")
                            .font(.caption)
                            .foregroundColor(.secondary)
                        HStack {
                            TextField("5000", text: $tempDataPort)
                                .textFieldStyle(RoundedBorderTextFieldStyle())
                                .keyboardType(.numberPad)
                            
                            if !tempDataPort.isEmpty, let port = Int(tempDataPort) {
                                Button {
                                    settings.dataPort = port
                                    tempDataPort = ""
                                } label: {
                                    Image(systemName: "checkmark.circle.fill")
                                        .foregroundColor(.green)
                                }
                            }
                        }
                        Text("Mevcut: \(settings.dataPort)")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    
                    VStack(alignment: .leading, spacing: 5) {
                        Text("Kontrol Portu (Komutlar)")
                            .font(.caption)
                            .foregroundColor(.secondary)
                        HStack {
                            TextField("5001", text: $tempControlPort)
                                .textFieldStyle(RoundedBorderTextFieldStyle())
                                .keyboardType(.numberPad)
                            
                            if !tempControlPort.isEmpty, let port = Int(tempControlPort) {
                                Button {
                                    settings.controlPort = port
                                    tempControlPort = ""
                                } label: {
                                    Image(systemName: "checkmark.circle.fill")
                                        .foregroundColor(.green)
                                }
                            }
                        }
                        Text("Mevcut: \(settings.controlPort)")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }
                
                // HackRF Parametreleri
                Section(header: Text("HackRF Parametreleri")) {
                    VStack(alignment: .leading) {
                        HStack {
                            Text("VGA Gain")
                            Spacer()
                            Text("\(settings.vgaGain) dB")
                                .foregroundColor(.blue)
                                .bold()
                        }
                        Slider(value: Binding(
                            get: { Double(settings.vgaGain) },
                            set: { settings.vgaGain = Int($0) }
                        ), in: 0...62, step: 2)
                        Text("Aralık: 0-62 dB (2 dB adımlarla)")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    
                    VStack(alignment: .leading) {
                        Picker("LNA Gain", selection: $settings.lnaGain) {
                            Text("0 dB").tag(0)
                            Text("8 dB").tag(8)
                            Text("16 dB").tag(16)
                            Text("24 dB").tag(24)
                            Text("32 dB").tag(32)
                            Text("40 dB").tag(40)
                        }
                        .pickerStyle(.segmented)
                        Text("Düşük gürültülü amplifikatör kazancı")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    
                    VStack(alignment: .leading) {
                        Picker("RX Amplifier", selection: $settings.rxAmpGain) {
                            Text("Kapalı (0 dB)").tag(0)
                            Text("Açık (14 dB)").tag(14)
                        }
                        .pickerStyle(.segmented)
                        Text("RX amplifikatör kazancı")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }
                
                // Ses Kontrolü
                Section(header: Text("Ses Kontrolü")) {
                    VStack(alignment: .leading) {
                        HStack {
                            Text("Ses Seviyesi")
                            Spacer()
                            Text("\(Int(receiver.audioVolume * 100))%")
                                .foregroundColor(.blue)
                                .bold()
                        }
                        Slider(value: $receiver.audioVolume, in: 0...1)
                    }
                }
                
                // Port 5001 Komutları
                Section(header: Text("Port \(settings.controlPort) Komutları")) {
                    VStack(alignment: .leading, spacing: 5) {
                        Text("SET_FREQ:<frekans>")
                            .font(.system(.caption, design: .monospaced))
                        Text("SET_SAMPLE_RATE:<rate>")
                            .font(.system(.caption, design: .monospaced))
                        Text("SET_VGA_GAIN:<gain>")
                            .font(.system(.caption, design: .monospaced))
                        Text("SET_LNA_GAIN:<gain>")
                            .font(.system(.caption, design: .monospaced))
                        Text("SET_RX_AMP_GAIN:<gain>")
                            .font(.system(.caption, design: .monospaced))
                    }
                    .foregroundColor(.secondary)
                }
                
                // Durum
                Section(header: Text("Durum")) {
                    HStack {
                        Text("Bağlantı:")
                        Spacer()
                        HStack {
                            Circle()
                                .fill(receiver.isConnected ? Color.green : Color.red)
                                .frame(width: 10, height: 10)
                            Text(receiver.isConnected ? "Bağlı" : "Bağlı Değil")
                                .foregroundColor(receiver.isConnected ? .green : .red)
                        }
                    }
                    
                    if !receiver.statusMessage.isEmpty {
                        VStack(alignment: .leading) {
                            Text("Mesaj:")
                                .font(.caption)
                                .foregroundColor(.secondary)
                            Text(receiver.statusMessage)
                                .font(.caption)
                        }
                    }
                }
                
                // Sıfırlama
                Section {
                    Button("Ayarları Sıfırla") {
                        showResetAlert = true
                    }
                    .foregroundColor(.red)
                    .frame(maxWidth: .infinity)
                }
                
                // Hakkında
                Section(header: Text("Hakkında")) {
                    HStack {
                        Text("Versiyon")
                        Spacer()
                        Text("1.3")
                            .foregroundColor(.secondary)
                    }
                    
                    HStack {
                        Text("Desteklenen Modlar")
                        Spacer()
                        Text("FM, AM, NFM, TV")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
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
            .alert("Ayarları Sıfırla", isPresented: $showResetAlert) {
                Button("İptal", role: .cancel) { }
                Button("Sıfırla", role: .destructive) {
                    settings.resetToDefaults()
                    tempIP = ""
                    tempDataPort = ""
                    tempControlPort = ""
                }
            } message: {
                Text("Tüm ayarlar varsayılan değerlere sıfırlanacak. Devam etmek istiyor musunuz?")
            }
            .onAppear {
                tempIP = settings.serverIP
            }
        }
    }
}
