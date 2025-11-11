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