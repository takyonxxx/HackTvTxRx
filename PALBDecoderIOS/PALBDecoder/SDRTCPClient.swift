import Foundation
import Network

final class SDRTCPClient: ObservableObject {
    @Published var isConnected = false
    @Published var isControlConnected = false
    @Published var dataRate: Double = 0

    var onDataReceived: ((_ ptr: UnsafePointer<Int8>, _ len: Int) -> Void)?

    private var dataConnection: NWConnection?
    private var controlConnection: NWConnection?
    private let dataQueue = DispatchQueue(label: "sdr.tcp.data", qos: .userInteractive)
    private let controlQueue = DispatchQueue(label: "sdr.tcp.control", qos: .utility)
    private var bytesThisSecond: UInt64 = 0
    private var rateTimer: DispatchSourceTimer?

    // Pending initial config to send before data starts
    private var pendingHost: String = ""
    private var pendingDataPort: UInt16 = 5000
    private var initialConfig: [String] = []

    /// Connect: control port first, send initial config, then data port
    func connect(host: String, dataPort: UInt16 = 5000, controlPort: UInt16 = 5001,
                 initialCommands: [String] = []) {
        disconnect()

        pendingHost = host
        pendingDataPort = dataPort
        initialConfig = initialCommands

        // 1. Control connection FIRST
        let ctrlEndpoint = NWEndpoint.hostPort(host: .init(host), port: .init(rawValue: controlPort)!)
        controlConnection = NWConnection(to: ctrlEndpoint, using: .tcp)
        controlConnection?.stateUpdateHandler = { [weak self] state in
            switch state {
            case .ready:
                DispatchQueue.main.async { self?.isControlConnected = true }
                print("[TCP] Control connected")
                // Send initial config, then connect data
                self?.sendInitialConfig()
            case .failed(let error):
                DispatchQueue.main.async { self?.isControlConnected = false }
                print("[TCP] Control failed: \(error)")
            case .cancelled:
                DispatchQueue.main.async { self?.isControlConnected = false }
            default: break
            }
        }
        controlConnection?.start(queue: controlQueue)
    }

    private func sendInitialConfig() {
        // Send all initial commands immediately (don't wait for welcome)
        for cmd in self.initialConfig {
            self.sendCommand(cmd)
            print("[TCP] Sent initial: \(cmd)")
        }

        // Wait for server to apply settings, then connect data port
        controlQueue.asyncAfter(deadline: .now() + 0.5) { [weak self] in
            self?.connectDataPort()
        }
    }

    private func connectDataPort() {
        let dataTCP = NWProtocolTCP.Options()
        dataTCP.noDelay = true
        let dataParams = NWParameters(tls: nil, tcp: dataTCP)
        let dataEndpoint = NWEndpoint.hostPort(host: .init(pendingHost), port: .init(rawValue: pendingDataPort)!)
        dataConnection = NWConnection(to: dataEndpoint, using: dataParams)

        dataConnection?.stateUpdateHandler = { [weak self] state in
            switch state {
            case .ready:
                DispatchQueue.main.async { self?.isConnected = true }
                self?.startReceiving()
                self?.startRateTimer()
                print("[TCP] Data connected")
            case .failed(let error):
                DispatchQueue.main.async { self?.isConnected = false }
                print("[TCP] Data failed: \(error)")
            case .cancelled:
                DispatchQueue.main.async { self?.isConnected = false }
            default: break
            }
        }
        dataConnection?.start(queue: dataQueue)
    }

    func disconnect() {
        rateTimer?.cancel()
        rateTimer = nil
        dataConnection?.cancel()
        controlConnection?.cancel()
        dataConnection = nil
        controlConnection = nil
        DispatchQueue.main.async {
            self.isConnected = false
            self.isControlConnected = false
        }
    }

    func sendCommand(_ command: String) {
        guard let conn = controlConnection else { return }
        let data = Data((command + "\n").utf8)
        conn.send(content: data, completion: .contentProcessed { _ in })
    }

    func setFrequency(_ hz: UInt64) { sendCommand("SET_FREQ:\(hz)") }
    func setSampleRate(_ hz: UInt32) { sendCommand("SET_SAMPLE_RATE:\(hz)") }
    func setVgaGain(_ gain: UInt) { sendCommand("SET_VGA_GAIN:\(gain)") }
    func setLnaGain(_ gain: UInt) { sendCommand("SET_LNA_GAIN:\(gain)") }
    func setRxAmpGain(_ gain: UInt) { sendCommand("SET_RX_AMP_GAIN:\(gain)") }

    private func startReceiving() {
        guard let conn = dataConnection else { return }
        conn.receive(minimumIncompleteLength: 1, maximumLength: 262144) { [weak self] content, _, isComplete, error in
            if let data = content, !data.isEmpty {
                self?.bytesThisSecond += UInt64(data.count)
                data.withUnsafeBytes { rawBuf in
                    if let baseAddr = rawBuf.baseAddress {
                        self?.onDataReceived?(baseAddr.assumingMemoryBound(to: Int8.self), data.count)
                    }
                }
            }
            if error != nil { return }
            if !isComplete { self?.startReceiving() }
        }
    }

    private func startRateTimer() {
        rateTimer = DispatchSource.makeTimerSource(queue: dataQueue)
        rateTimer?.schedule(deadline: .now() + 1, repeating: 1)
        rateTimer?.setEventHandler { [weak self] in
            guard let self = self else { return }
            let rate = Double(self.bytesThisSecond) / (1024.0 * 1024.0)
            self.bytesThisSecond = 0
            DispatchQueue.main.async { self.dataRate = rate }
        }
        rateTimer?.resume()
    }
}
