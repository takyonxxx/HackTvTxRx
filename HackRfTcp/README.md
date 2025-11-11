# HackRF TCP Server

Stream IQ samples from HackRF over TCP and control it remotely.

## What it does

This application lets you:
- Stream IQ samples from HackRF to multiple clients over TCP
- Control HackRF parameters (frequency, gains, sample rate) remotely via TCP
- Supports up to 20 MSPS sample rate

## Requirements

- HackRF One device
- Qt 6.0 or higher
- Windows or Linux

## Installation

### Windows
1. Install HackRF drivers using Zadig
2. Download the release or build from source
3. Run `HackRfTcp.exe`

### Linux
```bash
# Install dependencies
sudo apt-get install libhackrf-dev qt6-base-dev

# Build
mkdir build && cd build
qmake ..
make
```

## Quick Start

### 1. Start the server
```bash
HackRfTcp.exe --sample-rate 10000000 -f 100000000
```

The server will start with:
- Data streaming on port **5000**
- Control interface on port **5001**

### 2. Control HackRF remotely

**Using telnet:**
```bash
telnet 192.168.1.100 5001
SET_FREQ:433920000
SET_SAMPLE_RATE:10000000
SET_VGA_GAIN:30
GET_STATUS
```

**Using Python:**
```python
import socket

s = socket.socket()
s.connect(('192.168.1.100', 5001))
s.send(b'SET_FREQ:433920000\n')
print(s.recv(1024).decode())
s.close()
```

### 3. Receive IQ data
```python
import socket
import numpy as np

data_sock = socket.socket()
data_sock.connect(('192.168.1.100', 5000))

while True:
    data = data_sock.recv(262144)
    iq = np.frombuffer(data, dtype=np.int8)
    i_samples = iq[0::2]
    q_samples = iq[1::2]
    # Process samples...
```

## Command Line Options
```bash
HackRfTcp.exe [OPTIONS]

Options:
  -d, --data-port <port>       Data streaming port (default: 5000)
  -c, --control-port <port>    Control port (default: 5001)
  -f, --frequency <freq>       Initial frequency in Hz (default: 100000000)
  --sample-rate <rate>         Sample rate in Hz (default: 16000000)
  --vga-gain <gain>            VGA gain 0-62 (default: 40)
  --lna-gain <gain>            LNA gain 0-40 (default: 40)
  --rx-amp-gain <gain>         RX amp gain 0-14 (default: 14)
  --tx-amp-gain <gain>         TX amp gain 0-47 (default: 20)
  -h, --help                   Show help
```

## Control Commands

Connect to control port (5001) and send these commands:

| Command | Description | Example |
|---------|-------------|---------|
| `SET_FREQ:<value>` | Set frequency in Hz | `SET_FREQ:433920000` |
| `SET_SAMPLE_RATE:<value>` | Set sample rate in Hz | `SET_SAMPLE_RATE:10000000` |
| `SET_VGA_GAIN:<value>` | Set VGA gain (0-62) | `SET_VGA_GAIN:30` |
| `SET_LNA_GAIN:<value>` | Set LNA gain (0-40) | `SET_LNA_GAIN:32` |
| `SET_RX_AMP_GAIN:<value>` | Set RX amp gain (0-14) | `SET_RX_AMP_GAIN:14` |
| `SET_TX_AMP_GAIN:<value>` | Set TX amp gain (0-47) | `SET_TX_AMP_GAIN:20` |
| `GET_STATUS` | Get current settings | `GET_STATUS` |
| `HELP` | Show help | `HELP` |

## Python Examples

### Basic Control

**control_hackrf.py:**
```python
import socket

def control_hackrf(host, port=5001):
    s = socket.socket()
    s.connect((host, port))
    
    # Read welcome message
    print(s.recv(4096).decode())
    
    # Set frequency to 433.92 MHz
    s.send(b'SET_FREQ:433920000\n')
    print(s.recv(1024).decode())
    
    # Set sample rate to 10 MSPS
    s.send(b'SET_SAMPLE_RATE:10000000\n')
    print(s.recv(1024).decode())
    
    # Get status
    s.send(b'GET_STATUS\n')
    print(s.recv(4096).decode())
    
    s.close()

control_hackrf('192.168.1.100')
```

### Receive IQ Data

**receive_data.py:**
```python
import socket
import numpy as np

def receive_iq_data(host, port=5000, duration=10):
    s = socket.socket()
    s.connect((host, port))
    
    total_samples = 0
    import time
    start = time.time()
    
    while time.time() - start < duration:
        data = s.recv(262144)
        iq = np.frombuffer(data, dtype=np.int8)
        i_samples = iq[0::2]
        q_samples = iq[1::2]
        total_samples += len(i_samples)
        
        print(f"Received {total_samples:,} samples")
    
    s.close()

receive_iq_data('192.168.1.100')
```

### FM Demodulator with Real-time Audio Output

**fm_demodulator.py:**

A complete FM radio receiver with keyboard frequency control.

**Features:**
- Real-time FM demodulation
- Audio output to speakers
- Keyboard controls (LEFT/RIGHT arrows change frequency by 100 kHz)
- De-emphasis filter for broadcast FM

**Requirements:**
```bash
pip install numpy scipy sounddevice pynput
```

**Code:**
```python
import socket
import numpy as np
import sounddevice as sd
from scipy import signal
import time
import threading
from pynput import keyboard

# HackRF Configuration
HACKRF_IP = '192.168.1.2'
CONTROL_PORT = 5001
DATA_PORT = 5000

FREQUENCY = 100000000      # 100 MHz
SAMPLE_RATE = 2000000      # 2 MHz
AUDIO_RATE = 48000
VGA_GAIN = 30
LNA_GAIN = 32
RX_AMP_GAIN = 14

FREQ_STEP = 100000  # 100 kHz

# Frequency control
current_freq = FREQUENCY
freq_change_active = {'left': False, 'right': False}
freq_thread_running = True

def control_hackrf(host, port, command):
    """Send command to HackRF control port"""
    s = socket.socket()
    s.connect((host, port))
    
    # Read welcome message on first connection
    welcome = s.recv(4096).decode()
    
    # Send command
    s.send(command.encode() + b'\n')
    response = s.recv(1024).decode()
    s.close()
    
    # Return only relevant part
    return "OK" if "Ready" in response else response.split('\n')[0]

def set_frequency(freq):
    """Set new frequency"""
    control_hackrf(HACKRF_IP, CONTROL_PORT, f'SET_FREQ:{freq}')
    print(f"\r>>> Frequency: {freq/1e6:.3f} MHz     ", end='', flush=True)
    return freq

def frequency_control_thread():
    """Background thread for continuous frequency changes"""
    global current_freq
    while freq_thread_running:
        if freq_change_active['left']:
            current_freq -= FREQ_STEP
            set_frequency(current_freq)
            time.sleep(0.1)
        elif freq_change_active['right']:
            current_freq += FREQ_STEP
            set_frequency(current_freq)
            time.sleep(0.1)
        else:
            time.sleep(0.05)

def on_press(key):
    """Handle key press"""
    try:
        if key == keyboard.Key.left:
            freq_change_active['left'] = True
        elif key == keyboard.Key.right:
            freq_change_active['right'] = True
    except:
        pass

def on_release(key):
    """Handle key release"""
    try:
        if key == keyboard.Key.left:
            freq_change_active['left'] = False
        elif key == keyboard.Key.right:
            freq_change_active['right'] = False
    except:
        pass

def fm_demod(iq_samples):
    """Demodulate FM signal"""
    i = iq_samples[0::2].astype(np.float32) / 127.0
    q = iq_samples[1::2].astype(np.float32) / 127.0
    iq = i + 1j * q
    phase = np.angle(iq)
    demod = np.diff(np.unwrap(phase))
    return demod

def deemphasis_filter(audio_rate):
    """75µs de-emphasis filter for broadcast FM"""
    d = audio_rate * 75e-6
    x = np.exp(-1 / d)
    b = [1 - x]
    a = [1, -x]
    return b, a

def receive_iq_data():
    """Main function to receive and process IQ data"""
    global current_freq
    
    # Configure HackRF
    print("Configuring HackRF...")
    control_hackrf(HACKRF_IP, CONTROL_PORT, f'SET_FREQ:{FREQUENCY}')
    control_hackrf(HACKRF_IP, CONTROL_PORT, f'SET_SAMPLE_RATE:{SAMPLE_RATE}')
    control_hackrf(HACKRF_IP, CONTROL_PORT, f'SET_VGA_GAIN:{VGA_GAIN}')
    control_hackrf(HACKRF_IP, CONTROL_PORT, f'SET_LNA_GAIN:{LNA_GAIN}')
    control_hackrf(HACKRF_IP, CONTROL_PORT, f'SET_RX_AMP_GAIN:{RX_AMP_GAIN}')
    
    # Calculate decimation
    decimation = int(SAMPLE_RATE / AUDIO_RATE)
    print(f"Decimation factor: {decimation}")
    
    # Setup de-emphasis filter
    b, a = deemphasis_filter(AUDIO_RATE)
    zi = signal.lfilter_zi(b, a)
    
    # Connect to data stream
    print("Connecting to data stream...")
    data_sock = socket.socket()
    data_sock.connect((HACKRF_IP, DATA_PORT))
    
    # Setup audio output
    stream = sd.OutputStream(samplerate=AUDIO_RATE, channels=1, dtype='float32')
    stream.start()
    
    # Start keyboard listener
    listener = keyboard.Listener(on_press=on_press, on_release=on_release)
    listener.start()
    
    # Start frequency control thread
    freq_thread = threading.Thread(target=frequency_control_thread, daemon=True)
    freq_thread.start()
    
    print("\n=== FM Demodulator Running ===")
    print(f"Frequency: {FREQUENCY/1e6:.3f} MHz")
    print(f"Sample Rate: {SAMPLE_RATE/1e6:.2f} MHz")
    print("Controls: LEFT/RIGHT arrows to change frequency")
    print("Press Ctrl+C to stop.\n")
    
    total_samples = 0
    try:
        while True:
            # Receive IQ data
            data = data_sock.recv(262144)
            iq = np.frombuffer(data, dtype=np.int8)
            
            if len(iq) == 0:
                continue
            
            total_samples += len(iq)
            
            # Demodulate FM
            audio = fm_demod(iq)
            
            # Decimate to audio rate
            if decimation > 1:
                audio = signal.decimate(audio, decimation, zero_phase=True, ftype='fir')
            
            # Apply de-emphasis
            audio, zi = signal.lfilter(b, a, audio, zi=zi)
            
            # Normalize
            max_val = np.max(np.abs(audio))
            if max_val > 0:
                audio = audio / max_val * 0.5
            
            # Play audio
            stream.write(audio.astype(np.float32))
            
    except KeyboardInterrupt:
        print(f"\n\nTotal samples received: {total_samples:,}")
        print("Stopping...")
    finally:
        global freq_thread_running
        freq_thread_running = False
        listener.stop()
        stream.stop()
        stream.close()
        data_sock.close()
        print("Closed.")

# Run the receiver
if __name__ == "__main__":
    receive_iq_data()
```

**Usage:**
```bash
# Start HackRF TCP Server
HackRfTcp.exe --sample-rate 2000000 -f 100000000

# Run FM demodulator
python fm_demodulator.py

# Use LEFT/RIGHT arrow keys to tune frequency
```

## Troubleshooting

**HackRF not found:**
- Check USB connection
- Install drivers (Zadig on Windows)
- Run `hackrf_info` to verify device
- Close other applications using HackRF

**Connection refused:**
- Check firewall settings
- Verify correct IP address
- Make sure server is running

**No data received:**
- Check if HackRF is in RX mode
- Verify sample rate is supported
- Check antenna connection

**No audio in FM demodulator:**
- Make sure you're tuned to an FM station (88-108 MHz)
- Check antenna connection
- Adjust gains (VGA_GAIN, LNA_GAIN, RX_AMP_GAIN)
- Verify sample rate is at least 2 MHz

## Building from Source
```bash
git clone https://github.com/yourusername/hackrf-tcp-server.git
cd hackrf-tcp-server
mkdir build && cd build
qmake ..
make
```

## License

MIT License - See LICENSE file for details

## Author

Türkay Biliyor - [GitHub](https://github.com/takyonxxx)

## Contributing

Pull requests are welcome. For major changes, please open an issue first.

---

**Note:** Make sure you have proper authorization before operating RF equipment in your region.
