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
- Data streaming on port **7355**
- Control interface on port **7356**

### 2. Control HackRF remotely

**Using telnet:**
```bash
telnet 192.168.1.100 7356
SET_FREQ:433920000
SET_SAMPLE_RATE:10000000
SET_VGA_GAIN:30
GET_STATUS
```

**Using Python:**
```python
import socket

s = socket.socket()
s.connect(('192.168.1.100', 7356))
s.send(b'SET_FREQ:433920000\n')
print(s.recv(1024).decode())
s.close()
```

### 3. Receive IQ data
```python
import socket
import numpy as np

data_sock = socket.socket()
data_sock.connect(('192.168.1.100', 7355))

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
  -d, --data-port <port>       Data streaming port (default: 7355)
  -c, --control-port <port>    Control port (default: 7356)
  -f, --frequency <freq>       Initial frequency in Hz (default: 100000000)
  --sample-rate <rate>         Sample rate in Hz (default: 16000000)
  --vga-gain <gain>            VGA gain 0-62 (default: 40)
  --lna-gain <gain>            LNA gain 0-40 (default: 40)
  --rx-amp-gain <gain>         RX amp gain 0-14 (default: 14)
  --tx-amp-gain <gain>         TX amp gain 0-47 (default: 20)
  -h, --help                   Show help
```

## Control Commands

Connect to control port (7356) and send these commands:

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

## Python Example

**control_hackrf.py:**
```python
import socket

def control_hackrf(host, port=7356):
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

**receive_data.py:**
```python
import socket
import numpy as np

def receive_iq_data(host, port=7355, duration=10):
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

Your Name - [GitHub](https://github.com/yourusername)

## Contributing

Pull requests are welcome. For major changes, please open an issue first.

---

**Note:** Make sure you have proper authorization before operating RF equipment in your region.
