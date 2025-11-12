//
//  ContentView.swift
//  HackRFReceiver
//
//  Main UI for HackRF receiver application
//

import SwiftUI

enum ReceiverMode: String, CaseIterable {
    case fm = "FM Radio"
    case tv = "PAL-B/G TV"
}

struct ContentView: View {
    @StateObject private var receiver = HackRFReceiver()
    @State private var serverIP = "192.168.1.2"
    @State private var serverPort = "5000"
    @State private var frequency = "100.0"
    @State private var mode: ReceiverMode = .fm
    @State private var showAlert = false
    @State private var alertMessage = ""
    
    var body: some View {
        NavigationView {
            VStack(spacing: 20) {
                // Connection Settings
                GroupBox(label: Label("Server Configuration", systemImage: "network")) {
                    VStack(alignment: .leading, spacing: 10) {
                        HStack {
                            Text("IP Address:")
                                .frame(width: 100, alignment: .leading)
                            TextField("192.168.1.2", text: $serverIP)
                                .textFieldStyle(RoundedBorderTextFieldStyle())
                                .keyboardType(.decimalPad)
                                .disabled(receiver.isConnected)
                        }
                        
                        HStack {
                            Text("Port:")
                                .frame(width: 100, alignment: .leading)
                            TextField("5000", text: $serverPort)
                                .textFieldStyle(RoundedBorderTextFieldStyle())
                                .keyboardType(.numberPad)
                                .disabled(receiver.isConnected)
                        }
                    }
                    .padding(.vertical, 5)
                }
                .padding(.horizontal)
                
                // Mode Selection
                GroupBox(label: Label("Reception Mode", systemImage: "antenna.radiowaves.left.and.right")) {
                    Picker("Mode", selection: $mode) {
                        ForEach(ReceiverMode.allCases, id: \.self) { mode in
                            Text(mode.rawValue).tag(mode)
                        }
                    }
                    .pickerStyle(SegmentedPickerStyle())
                    .disabled(receiver.isConnected)
                }
                .padding(.horizontal)
                
                // Frequency Control
                GroupBox(label: Label("Frequency", systemImage: "waveform")) {
                    VStack(spacing: 10) {
                        HStack {
                            Text("Frequency:")
                                .frame(width: 100, alignment: .leading)
                            TextField("100.0", text: $frequency)
                                .textFieldStyle(RoundedBorderTextFieldStyle())
                                .keyboardType(.decimalPad)
                            Text("MHz")
                        }
                        
                        if receiver.isConnected {
                            HStack(spacing: 20) {
                                Button(action: { adjustFrequency(-0.1) }) {
                                    Image(systemName: "minus.circle.fill")
                                        .font(.title)
                                }
                                
                                Text(String(format: "%.3f MHz", receiver.currentFrequency / 1_000_000))
                                    .font(.title2)
                                    .fontWeight(.bold)
                                    .frame(width: 150)
                                
                                Button(action: { adjustFrequency(0.1) }) {
                                    Image(systemName: "plus.circle.fill")
                                        .font(.title)
                                }
                            }
                        }
                    }
                    .padding(.vertical, 5)
                }
                .padding(.horizontal)
                
                // Status Display
                GroupBox(label: Label("Status", systemImage: "info.circle")) {
                    VStack(alignment: .leading, spacing: 8) {
                        StatusRow(label: "Connection:", value: receiver.isConnected ? "Connected" : "Disconnected", isGood: receiver.isConnected)
                        StatusRow(label: "Samples:", value: "\(receiver.samplesReceived)", isGood: receiver.samplesReceived > 0)
                        StatusRow(label: "Sample Rate:", value: mode == .tv ? "16 MHz" : "2 MHz", isGood: true)
                    }
                    .padding(.vertical, 5)
                }
                .padding(.horizontal)
                
                // TV Display (only in TV mode)
                if mode == .tv && receiver.isConnected {
                    GroupBox(label: Label("TV Display", systemImage: "tv")) {
                        TVDisplayView(receiver: receiver)
                            .aspectRatio(4/3, contentMode: .fit)
                            .frame(height: 250)
                    }
                    .padding(.horizontal)
                }
                
                Spacer()
                
                // Control Buttons
                HStack(spacing: 20) {
                    if !receiver.isConnected {
                        Button(action: connectToServer) {
                            Label("Connect", systemImage: "play.circle.fill")
                                .font(.headline)
                                .frame(maxWidth: .infinity)
                                .padding()
                                .background(Color.green)
                                .foregroundColor(.white)
                                .cornerRadius(10)
                        }
                    } else {
                        Button(action: disconnect) {
                            Label("Disconnect", systemImage: "stop.circle.fill")
                                .font(.headline)
                                .frame(maxWidth: .infinity)
                                .padding()
                                .background(Color.red)
                                .foregroundColor(.white)
                                .cornerRadius(10)
                        }
                    }
                }
                .padding(.horizontal)
                .padding(.bottom)
            }
            .navigationTitle("HackRF Receiver")
            .alert("Error", isPresented: $showAlert) {
                Button("OK", role: .cancel) { }
            } message: {
                Text(alertMessage)
            }
        }
    }
    
    private func connectToServer() {
        guard let port = Int(serverPort) else {
            showError("Invalid port number")
            return
        }
        
        guard let freq = Double(frequency) else {
            showError("Invalid frequency")
            return
        }
        
        let freqHz = Int(freq * 1_000_000)
        receiver.connect(to: serverIP, port: port, frequency: freqHz, mode: mode)
    }
    
    private func disconnect() {
        receiver.disconnect()
    }
    
    private func adjustFrequency(_ deltaMHz: Double) {
        let newFreq = receiver.currentFrequency + Int(deltaMHz * 1_000_000)
        receiver.setFrequency(newFreq)
        frequency = String(format: "%.1f", Double(newFreq) / 1_000_000)
    }
    
    private func showError(_ message: String) {
        alertMessage = message
        showAlert = true
    }
}

struct StatusRow: View {
    let label: String
    let value: String
    let isGood: Bool
    
    var body: some View {
        HStack {
            Text(label)
                .fontWeight(.medium)
                .frame(width: 120, alignment: .leading)
            Text(value)
                .foregroundColor(isGood ? .green : .secondary)
                .fontWeight(.semibold)
            Spacer()
        }
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
