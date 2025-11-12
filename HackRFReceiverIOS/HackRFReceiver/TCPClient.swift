//
//  TCPClient.swift
//  HackRFReceiver
//
//  TCP client for receiving HackRF IQ data stream
//

import Foundation
import Network

class TCPClient {
    private let host: String
    private let port: Int
    private var connection: NWConnection?
    private var isConnected = false
    
    init(host: String, port: Int) {
        self.host = host
        self.port = port
    }
    
    func connect() -> Bool {
        let endpoint = NWEndpoint.hostPort(
            host: NWEndpoint.Host(host),
            port: NWEndpoint.Port(integerLiteral: UInt16(port))
        )
        
        connection = NWConnection(to: endpoint, using: .tcp)
        
        let semaphore = DispatchSemaphore(value: 0)
        var success = false
        
        connection?.stateUpdateHandler = { [weak self] state in
            switch state {
            case .ready:
                print("TCP connection established")
                self?.isConnected = true
                success = true
                semaphore.signal()
            case .failed(let error):
                print("TCP connection failed: \(error)")
                self?.isConnected = false
                semaphore.signal()
            case .waiting(let error):
                print("TCP connection waiting: \(error)")
            default:
                break
            }
        }
        
        connection?.start(queue: .global())
        
        // Wait for connection with timeout
        _ = semaphore.wait(timeout: .now() + 5.0)
        return success
    }
    
    func disconnect() {
        connection?.cancel()
        connection = nil
        isConnected = false
        print("TCP connection closed")
    }
    
    func receive(maxBytes: Int) -> Data? {
        guard isConnected, let connection = connection else {
            return nil
        }
        
        var receivedData = Data()
        let semaphore = DispatchSemaphore(value: 0)
        var hasError = false
        
        connection.receive(minimumIncompleteLength: 1, maximumLength: maxBytes) { data, _, isComplete, error in
            if let error = error {
                print("Receive error: \(error)")
                hasError = true
                semaphore.signal()
                return
            }
            
            if let data = data {
                receivedData = data
            }
            
            if isComplete {
                hasError = true // Connection closed
            }
            
            semaphore.signal()
        }
        
        _ = semaphore.wait(timeout: .now() + 1.0)
        
        if hasError {
            return nil
        }
        
        return receivedData.isEmpty ? nil : receivedData
    }
    
    func send(_ data: Data) -> Bool {
        guard isConnected, let connection = connection else {
            return false
        }
        
        let semaphore = DispatchSemaphore(value: 0)
        var success = false
        
        connection.send(content: data, completion: .contentProcessed { error in
            if let error = error {
                print("Send error: \(error)")
            } else {
                success = true
            }
            semaphore.signal()
        })
        
        _ = semaphore.wait(timeout: .now() + 1.0)
        return success
    }
}
