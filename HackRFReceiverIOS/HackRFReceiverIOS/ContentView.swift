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
        #if os(macOS)
        macOSLayout
        #else
        iOSLayout
        #endif
    }
    
    // MARK: - iOS Layout
    
    #if os(iOS)
    private var iOSLayout: some View {
        NavigationView {
            ScrollView {
                VStack(spacing: 20) {
                    statusBar
                    modeSelector
                    connectionSettings
                    frequencyControl
                    controlButton
                    connectButton
                    
                    if selectedMode == .tv && receiver.isConnected {
                        TVDisplayView(videoFrame: receiver.currentVideoFrame)
                            .frame(height: 300)
                    }
                }
                .padding()
            }
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
        .navigationViewStyle(.stack)
    }
    #endif
    
    // MARK: - macOS Layout
    
    #if os(macOS)
    private var macOSLayout: some View {
        NavigationView {
            // Sidebar (optional - can be hidden)
            List {
                Label("Ana Ekran", systemImage: "antenna.radiowaves.left.and.right")
                Label("Ayarlar", systemImage: "gearshape")
                    .onTapGesture {
                        showSettings.toggle()
                    }
            }
            .listStyle(.sidebar)
            .frame(minWidth: 200)
            
            // Main content
            ScrollView {
                VStack(spacing: 20) {
                    statusBar
                    modeSelector
                    connectionSettings
                    frequencyControl
                    
                    HStack(spacing: 15) {
                        controlButton
                        connectButton
                    }
                    
                    if selectedMode == .tv && receiver.isConnected {
                        TVDisplayView(videoFrame: receiver.currentVideoFrame)
                            .frame(minHeight: 400)
                    }
                }
                .padding()
            }
            .frame(minWidth: 600, minHeight: 500)
            .navigationTitle("HackRF Alıcı")
            .toolbar {
                ToolbarItem(placement: .automatic) {
                    Button {
                        showSettings.toggle()
                    } label: {
                        Image(systemName: "gearshape")
                    }
                }
            }
        }
        .sheet(isPresented: $showSettings) {
            SettingsView(receiver: receiver)
                .frame(minWidth: 500, minHeight: 600)
        }
    }
    #endif
    
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
            
            #if os(macOS)
            Picker("Mod", selection: $selectedMode) {
                ForEach(ReceiverMode.allCases, id: \.self) { mode in
                    Text(mode.rawValue).tag(mode)
                }
            }
            .pickerStyle(.radioGroup)
            #else
            Picker("Mod", selection: $selectedMode) {
                ForEach(ReceiverMode.allCases, id: \.self) { mode in
                    Text(mode.rawValue).tag(mode)
                }
            }
            .pickerStyle(.segmented)
            #endif
            
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
                #if os(macOS)
                .buttonStyle(.automatic)
                #else
                .buttonStyle(.bordered)
                #endif
            }
            
            HStack {
                Text("Data Portu:")
                    .frame(width: 100, alignment: .leading)
                Text("\(settings.dataPort)")
                Spacer()
                Button("Ayarla") {
                    showSettings = true
                }
                #if os(macOS)
                .buttonStyle(.automatic)
                #else
                .buttonStyle(.bordered)
                #endif
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
                    #if os(iOS)
                    .keyboardType(.decimalPad)
                    #endif
                    .frame(maxWidth: 200)
            }
            
            #if os(macOS)
            HStack(spacing: 10) {
                frequencyButtons
            }
            #else
            HStack(spacing: 15) {
                frequencyButtons
            }
            #endif
            
            presetFrequencies
        }
    }
    
    private var frequencyButtons: some View {
        Group {
            Button("-1 MHz") {
                adjustFrequency(by: -1.0)
            }
            .frame(minWidth: 80)
            
            Button("-100k") {
                adjustFrequency(by: -0.1)
            }
            .frame(minWidth: 80)
            
            Button("+100k") {
                adjustFrequency(by: 0.1)
            }
            .frame(minWidth: 80)
            
            Button("+1 MHz") {
                adjustFrequency(by: 1.0)
            }
            .frame(minWidth: 80)
        }
        #if os(iOS)
        .padding(.vertical, 10)
        .background(Color.blue)
        .foregroundColor(.white)
        .cornerRadius(8)
        #endif
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
                        #if os(macOS)
                        .buttonStyle(.plain)
                        #endif
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
                Text("Parametreleri Gönder")
                    #if os(iOS)
                    .font(.subheadline)
                    #else
                    .font(.body)
                    #endif
            }
            .frame(maxWidth: .infinity)
            .padding()
            .background(Color.orange)
            .foregroundColor(.white)
            .cornerRadius(12)
        }
        .disabled(settings.serverIP.isEmpty)
        #if os(macOS)
        .buttonStyle(.plain)
        #endif
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
        #if os(macOS)
        .buttonStyle(.plain)
        #endif
    }
    
    // MARK: - Helper Functions
    
    private func adjustFrequency(by delta: Double) {
        if let freq = Double(frequency) {
            let newFreq = freq + delta
            frequency = String(format: "%.1f", newFreq)
            
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
                connectionSection
                hackRFParametersSection
                audioControlSection
                commandsSection
                statusSection
                resetSection
                aboutSection
            }
            #if os(macOS)
            .formStyle(.grouped)
            .frame(minWidth: 500, minHeight: 600)
            #endif
            .navigationTitle("Ayarlar")
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: toolbarPlacement) {
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
    
    private var toolbarPlacement: ToolbarItemPlacement {
        #if os(macOS)
        return .automatic
        #else
        return .navigationBarTrailing
        #endif
    }
    
    // MARK: - Sections
    
    private var connectionSection: some View {
        Section(header: Text("Bağlantı Ayarları")) {
            VStack(alignment: .leading, spacing: 5) {
                Text("HackRF Sunucu IP Adresi")
                    .font(.caption)
                    .foregroundColor(.secondary)
                HStack {
                    TextField("Örn: 192.168.1.100", text: $tempIP)
                        #if os(iOS)
                        .textFieldStyle(RoundedBorderTextFieldStyle())
                        .keyboardType(.numbersAndPunctuation)
                        .autocapitalization(.none)
                        #else
                        .textFieldStyle(.roundedBorder)
                        #endif
                    
                    if !tempIP.isEmpty {
                        Button {
                            settings.serverIP = tempIP
                        } label: {
                            Image(systemName: "checkmark.circle.fill")
                                .foregroundColor(.green)
                        }
                        #if os(macOS)
                        .buttonStyle(.borderless)
                        #endif
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
            
            portField(
                title: "Data Portu (IQ Stream)",
                placeholder: "5000",
                binding: $tempDataPort,
                currentValue: settings.dataPort
            ) { port in
                settings.dataPort = port
                tempDataPort = ""
            }
            
            portField(
                title: "Kontrol Portu (Komutlar)",
                placeholder: "5001",
                binding: $tempControlPort,
                currentValue: settings.controlPort
            ) { port in
                settings.controlPort = port
                tempControlPort = ""
            }
        }
    }
    
    private func portField(
        title: String,
        placeholder: String,
        binding: Binding<String>,
        currentValue: Int,
        onSave: @escaping (Int) -> Void
    ) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(title)
                .font(.caption)
                .foregroundColor(.secondary)
            HStack {
                TextField(placeholder, text: binding)
                    #if os(iOS)
                    .textFieldStyle(RoundedBorderTextFieldStyle())
                    .keyboardType(.numberPad)
                    #else
                    .textFieldStyle(.roundedBorder)
                    #endif
                
                if !binding.wrappedValue.isEmpty, let port = Int(binding.wrappedValue) {
                    Button {
                        onSave(port)
                    } label: {
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundColor(.green)
                    }
                    #if os(macOS)
                    .buttonStyle(.borderless)
                    #endif
                }
            }
            Text("Mevcut: \(currentValue)")
                .font(.caption)
                .foregroundColor(.secondary)
        }
    }
    
    private var hackRFParametersSection: some View {
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
                #if os(iOS)
                .pickerStyle(.segmented)
                #else
                .pickerStyle(.radioGroup)
                #endif
                Text("Düşük gürültülü amplifikatör kazancı")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            
            VStack(alignment: .leading) {
                Picker("RX Amplifier", selection: $settings.rxAmpGain) {
                    Text("Kapalı (0 dB)").tag(0)
                    Text("Açık (14 dB)").tag(14)
                }
                #if os(iOS)
                .pickerStyle(.segmented)
                #else
                .pickerStyle(.radioGroup)
                #endif
                Text("RX amplifikatör kazancı")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
    }
    
    private var audioControlSection: some View {
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
    }
    
    private var commandsSection: some View {
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
    }
    
    private var statusSection: some View {
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
    }
    
    private var resetSection: some View {
        Section {
            Button("Ayarları Sıfırla") {
                showResetAlert = true
            }
            .foregroundColor(.red)
            .frame(maxWidth: .infinity)
        }
    }
    
    private var aboutSection: some View {
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
            
            HStack {
                Text("Platform")
                Spacer()
                #if os(macOS)
                Text("macOS")
                    .foregroundColor(.secondary)
                #else
                Text("iOS")
                    .foregroundColor(.secondary)
                #endif
            }
        }
    }
}
