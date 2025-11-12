import Foundation
import Network

class TCPClient {
    let host: String
    private let port: Int
    private var connection: NWConnection?
    private let queue = DispatchQueue(label: "com.hackrf.tcp")
    
    var onDataReceived: ((Data) -> Void)?
    var onStatusChanged: ((Bool, String) -> Void)?
    
    init(host: String, port: Int) {
        self.host = host
        self.port = port
    }
    
    func connect() {
        let nwHost = NWEndpoint.Host(host)
        let nwPort = NWEndpoint.Port(rawValue: UInt16(port))!
        
        connection = NWConnection(host: nwHost, port: nwPort, using: .tcp)
        
        connection?.stateUpdateHandler = { [weak self] state in
            switch state {
            case .ready:
                self?.onStatusChanged?(true, "Bağlandı")
                self?.receiveData()
                
            case .failed(let error):
                self?.onStatusChanged?(false, "Bağlantı hatası: \(error.localizedDescription)")
                
            case .waiting(let error):
                self?.onStatusChanged?(false, "Bekleniyor: \(error.localizedDescription)")
                
            default:
                break
            }
        }
        
        connection?.start(queue: queue)
    }
    
    func disconnect() {
        connection?.cancel()
        connection = nil
        onStatusChanged?(false, "Bağlantı kesildi")
    }
    
    func send(data: Data) {
        connection?.send(content: data, completion: .contentProcessed { error in
            if let error = error {
                print("Send error: \(error)")
            }
        })
    }
    
    private func receiveData() {
        connection?.receive(minimumIncompleteLength: 1, maximumLength: 262144) { [weak self] data, _, isComplete, error in
            if let data = data, !data.isEmpty {
                self?.onDataReceived?(data)
            }
            
            if let error = error {
                print("Receive error: \(error)")
                return
            }
            
            if !isComplete {
                self?.receiveData()
            }
        }
    }
}
