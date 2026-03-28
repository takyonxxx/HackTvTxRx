import Foundation
import Network

final class SDRTCPClient: ObservableObject {
    @Published var isConnected = false
    @Published var isControlConnected = false
    @Published var dataRate: Double = 0
    @Published var totalBytesReceived: UInt64 = 0

    var onDataReceived: ((Data) -> Void)?

    private var dataConnection: NWConnection?
    private var controlConnection: NWConnection?
    private let dataQueue = DispatchQueue(label: "sdr.tcp.data", qos: .userInteractive)
    private let controlQueue = DispatchQueue(label: "sdr.tcp.control", qos: .utility)
    private var bytesThisSecond: UInt64 = 0
    private var rateTimer: DispatchSourceTimer?

    func connect(host: String, dataPort: UInt16 = 5000, controlPort: UInt16 = 5001) {
        disconnect()

        // Data connection
        let dataEndpoint = NWEndpoint.hostPort(host: .init(host), port: .init(rawValue: dataPort)!)
        let dataTCP = NWProtocolTCP.Options()
        dataTCP.noDelay = true
        let dataParams = NWParameters(tls: nil, tcp: dataTCP)
        dataConnection = NWConnection(to: dataEndpoint, using: dataParams)

        dataConnection?.stateUpdateHandler = { [weak self] state in
            DispatchQueue.main.async {
                switch state {
                case .ready:
                    self?.isConnected = true
                    self?.startReceiving()
                    self?.startRateTimer()
                    print("[TCP] Data connected to \(host):\(dataPort)")
                case .failed(let error):
                    self?.isConnected = false
                    print("[TCP] Data connection failed: \(error)")
                case .cancelled:
                    self?.isConnected = false
                default: break
                }
            }
        }
        dataConnection?.start(queue: dataQueue)

        // Control connection
        let ctrlEndpoint = NWEndpoint.hostPort(host: .init(host), port: .init(rawValue: controlPort)!)
        controlConnection = NWConnection(to: ctrlEndpoint, using: .tcp)
        controlConnection?.stateUpdateHandler = { [weak self] state in
            DispatchQueue.main.async {
                switch state {
                case .ready:
                    self?.isControlConnected = true
                    print("[TCP] Control connected to \(host):\(controlPort)")
                case .failed(let error):
                    self?.isControlConnected = false
                    print("[TCP] Control connection failed: \(error)")
                case .cancelled:
                    self?.isControlConnected = false
                default: break
                }
            }
        }
        controlConnection?.start(queue: controlQueue)
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
        conn.send(content: data, completion: .contentProcessed { error in
            if let error = error {
                print("[TCP] Control send error: \(error)")
            }
        })
    }

    func setFrequency(_ hz: UInt64) { sendCommand("SET_FREQ:\(hz)") }
    func setSampleRate(_ hz: UInt32) { sendCommand("SET_SAMPLE_RATE:\(hz)") }
    func setVgaGain(_ gain: UInt) { sendCommand("SET_VGA_GAIN:\(gain)") }
    func setLnaGain(_ gain: UInt) { sendCommand("SET_LNA_GAIN:\(gain)") }
    func setRxAmpGain(_ gain: UInt) { sendCommand("SET_RX_AMP_GAIN:\(gain)") }

    // MARK: - Private

    private func startReceiving() {
        guard let conn = dataConnection else { return }
        conn.receive(minimumIncompleteLength: 1, maximumLength: 262144) { [weak self] data, _, isComplete, error in
            if let data = data, !data.isEmpty {
                self?.bytesThisSecond += UInt64(data.count)
                self?.totalBytesReceived += UInt64(data.count)
                self?.onDataReceived?(data)
            }
            if let error = error {
                print("[TCP] Receive error: \(error)")
                return
            }
            if !isComplete {
                self?.startReceiving()
            }
        }
    }

    private func startRateTimer() {
        rateTimer = DispatchSource.makeTimerSource(queue: dataQueue)
        rateTimer?.schedule(deadline: .now() + 1, repeating: 1)
        rateTimer?.setEventHandler { [weak self] in
            guard let self = self else { return }
            let rate = Double(self.bytesThisSecond) / (1024.0 * 1024.0)
            self.bytesThisSecond = 0
            DispatchQueue.main.async {
                self.dataRate = rate
            }
        }
        rateTimer?.resume()
    }
}
