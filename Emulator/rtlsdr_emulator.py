#!/usr/bin/env python3
"""
RTL-SDR TCP Emulator - rtl_tcp binary protocol compatible
Only requires: pip install numpy

Emulates an rtl_tcp server that GUI (or any rtl_tcp client) can connect to.
Uses the same signal engine as the HackRF emulator:
  - WFM stereo broadcast with 19kHz pilot + 38kHz subcarrier
  - NFM voice radio (300-3kHz BPF, pre-emphasis)
  - AM airband (DSB-FC)
  - Space noise on empty frequencies

Protocol: rtl_tcp binary
  - On connect: sends 12-byte header ("RTL0" + tuner_type + gain_count)
  - IQ data: unsigned 8-bit I,Q interleaved (uint8, center=127)
  - Commands: 5-byte big-endian (1 byte cmd + 4 byte param)

Usage:
  python rtlsdr_emulator.py [--port 1234]
  Then in GUI or any rtl_tcp client: connect to 127.0.0.1:1234

Place FaithlessInsomnia.wav in the same directory.
"""

import socket, threading, math, time, argparse, sys, signal, os, struct, queue

try:
    import numpy as np
except ImportError:
    print("ERROR: numpy required. pip install numpy"); sys.exit(1)

# Import signal engine from hackrf_emulator (same directory)
# We'll inline the necessary parts to keep this self-contained

# ============================================================
# Audio Loader (same as hackrf_emulator)
# ============================================================

def load_wav(filepath):
    import wave
    try:
        with wave.open(filepath, 'rb') as wf:
            nch = wf.getnchannels()
            sw = wf.getsampwidth()
            fr = wf.getframerate()
            nf = wf.getnframes()
            raw = wf.readframes(nf)
        if sw == 2:
            data = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
        elif sw == 1:
            data = np.frombuffer(raw, dtype=np.uint8).astype(np.float32) / 128.0 - 1.0
        elif sw == 4:
            data = np.frombuffer(raw, dtype=np.int32).astype(np.float32) / 2147483648.0
        else:
            return None, 1
        if nch == 2:
            left = data[0::2]; right = data[1::2]
            if fr != 48000:
                old_len = len(left)
                new_len = int(old_len * 48000 / fr)
                left = np.interp(np.linspace(0, old_len-1, new_len), np.arange(old_len), left).astype(np.float32)
                right = np.interp(np.linspace(0, old_len-1, new_len), np.arange(old_len), right).astype(np.float32)
            return np.column_stack([left, right]).astype(np.float32), 2
        else:
            if fr != 48000:
                old_len = len(data)
                new_len = int(old_len * 48000 / fr)
                data = np.interp(np.linspace(0, old_len-1, new_len), np.arange(old_len), data)
            return data.astype(np.float32), 1
    except Exception as e:
        print(f"    WAV load failed: {e}")
        return None, 1


def load_audio(mp3_path=None):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = []
    if mp3_path: candidates.append(mp3_path)
    candidates += [os.path.join(script_dir, 'FaithlessInsomnia.mp3'),
                   os.path.join(script_dir, 'faithlessinsomnia.mp3')]

    wav_candidates = [os.path.join(script_dir, 'FaithlessInsomnia.wav'),
                      os.path.join(script_dir, 'faithlessinsomnia.wav'),
                      'FaithlessInsomnia.wav']
    for w in wav_candidates:
        if os.path.exists(w):
            samples, nch = load_wav(w)
            if samples is not None:
                print(f"  Loaded WAV: {w} ({nch}ch)")
                return samples, nch

    mp3_file = None
    for c in candidates:
        if os.path.exists(c): mp3_file = c; break
    if mp3_file:
        try:
            from pydub import AudioSegment
            audio = AudioSegment.from_mp3(mp3_file)
            nch = audio.channels
            audio = audio.set_frame_rate(48000).set_sample_width(2)
            raw = np.frombuffer(audio.raw_data, dtype=np.int16).astype(np.float32) / 32768.0
            if nch == 2:
                return np.column_stack([raw[0::2], raw[1::2]]).astype(np.float32), 2
            return raw.astype(np.float32), 1
        except: pass

        import subprocess
        for fp in ['ffmpeg', r'C:\msys64\mingw64\bin\ffmpeg.exe']:
            try:
                r = subprocess.run([fp, '-i', mp3_file, '-f', 's16le', '-acodec', 'pcm_s16le',
                                    '-ac', '2', '-ar', '48000', '-v', 'quiet', '-'],
                                   capture_output=True, timeout=120)
                if r.returncode == 0 and len(r.stdout) > 1000:
                    raw = np.frombuffer(r.stdout, dtype=np.int16).astype(np.float32) / 32768.0
                    return np.column_stack([raw[0::2], raw[1::2]]).astype(np.float32), 2
            except: continue

    print("  No audio file found - using 440Hz tone")
    return None, 1


# ============================================================
# Broadcast config (identical to hackrf_emulator)
# ============================================================

FM_NFM_FREQS = [145000000, 446000000]
NFM_DEVIATION = 2500
NFM_AUDIO_LOW = 300.0; NFM_AUDIO_HIGH = 3000.0
NFM_PREEMPH_TAU = 50e-6

FM_WFM_FREQS = [100000000]
WFM_DEVIATION = 75000
WFM_AUDIO_HIGH = 15000.0
WFM_PREEMPH_TAU = 50e-6
WFM_PILOT_FREQ = 19000.0
WFM_PILOT_LEVEL = 0.08

FM_BROADCAST_FREQS = FM_NFM_FREQS + FM_WFM_FREQS

AM_BROADCAST_FREQS = [119100000]
AM_MODULATION_DEPTH = 0.50
AIRBAND_AUDIO_LOW = 300.0; AIRBAND_AUDIO_HIGH = 3000.0

ALL_BROADCAST_FREQS = FM_BROADCAST_FREQS + AM_BROADCAST_FREQS

# ============================================================
# rtl_tcp protocol constants
# ============================================================

# rtl_tcp command IDs (from librtlsdr)
CMD_SET_FREQ         = 0x01
CMD_SET_SAMPLE_RATE  = 0x02
CMD_SET_GAIN_MODE    = 0x03
CMD_SET_GAIN         = 0x04
CMD_SET_FREQ_CORR    = 0x05
CMD_SET_IF_GAIN      = 0x06
CMD_SET_AGC_MODE     = 0x08
CMD_SET_DIRECT_SAMP  = 0x09
CMD_SET_OFFSET_TUNE  = 0x0A
CMD_SET_RTL_XTAL     = 0x0B
CMD_SET_TUNER_XTAL   = 0x0C
CMD_SET_TUNER_BW     = 0x0E
CMD_SET_BIAS_TEE     = 0x0F

# Tuner type in header
RTLSDR_TUNER_R820T = 5


class RtlSdrEmulator:
    """Emulates an rtl_tcp server with realistic RF signals."""

    def __init__(self, port=1234, mp3_path=None):
        self.port = port
        self.mp3_path = mp3_path

        self.frequency = 100000000
        self.sample_rate = 2000000
        self.gain = 400  # in tenths of dB (40.0 dB)
        self.gain_mode = 0  # 0=auto, 1=manual
        self.agc_mode = 0
        self.freq_correction = 0
        self.direct_sampling = 0
        self.offset_tuning = 0
        self.bias_tee = 0

        self.chunk_samples = 32768
        self.sample_counter = 0
        self.audio_rate = 48000

        self.mp3_samples = None
        self.mp3_channels = 1
        self.audio_pos = {}
        self.pilot_phase = {}
        self.preemph_state = {}

        # Filters
        self.airband_bp_taps = self._design_bandpass(AIRBAND_AUDIO_LOW, AIRBAND_AUDIO_HIGH, self.audio_rate)
        self.airband_bp_state = {}
        self.nfm_bp_taps = self._design_bandpass(NFM_AUDIO_LOW, NFM_AUDIO_HIGH, self.audio_rate)
        self.nfm_bp_state = {}
        self.wfm_lp_taps = self._design_lowpass(WFM_AUDIO_HIGH, self.audio_rate)
        self.wfm_lp_state = {}

        self.running = False
        self.clients = []
        self.clients_lock = threading.Lock()
        self.iq_queue = queue.Queue(maxsize=8)
        self.total_bytes_sent = 0

    # ── Filter design (same as hackrf_emulator) ──

    @staticmethod
    def _design_bandpass(f_low, f_high, fs, num_taps=127):
        n = np.arange(num_taps); M = num_taps // 2
        fc_hi = f_high / fs
        h_hi = np.where(n == M, 2*fc_hi, np.sin(2*np.pi*fc_hi*(n-M)) / (np.pi*(n-M)))
        fc_lo = f_low / fs
        h_lo = np.where(n == M, 2*fc_lo, np.sin(2*np.pi*fc_lo*(n-M)) / (np.pi*(n-M)))
        h_bp = h_hi - h_lo
        w = 0.54 - 0.46 * np.cos(2*np.pi*n / (num_taps-1))
        h_bp *= w
        f_center = (f_low + f_high) / 2.0
        omega = 2 * np.pi * f_center / fs
        gain = np.abs(np.sum(h_bp * np.exp(-1j * omega * n)))
        if gain > 0: h_bp /= gain
        return h_bp.astype(np.float64)

    @staticmethod
    def _design_lowpass(f_cutoff, fs, num_taps=63):
        n = np.arange(num_taps); M = num_taps // 2
        fc = f_cutoff / fs
        h = np.where(n == M, 2*fc, np.sin(2*np.pi*fc*(n-M)) / (np.pi*(n-M)))
        w = 0.54 - 0.46 * np.cos(2*np.pi*n / (num_taps-1))
        h *= w; s = np.sum(h)
        if s > 0: h /= s
        return h.astype(np.float64)

    def _apply_fir(self, audio, taps, state_dict, key):
        ntaps = len(taps)
        if key not in state_dict:
            state_dict[key] = np.zeros(ntaps-1, dtype=np.float64)
        delay = state_dict[key]
        extended = np.concatenate([delay, audio.astype(np.float64)])
        out = np.zeros(len(audio), dtype=np.float64)
        for i in range(len(audio)):
            out[i] = np.dot(taps, extended[i:i+ntaps])
        if len(audio) >= ntaps-1:
            state_dict[key] = audio[-(ntaps-1):].astype(np.float64)
        else:
            keep = (ntaps-1) - len(audio)
            state_dict[key] = np.concatenate([delay[-keep:], audio.astype(np.float64)])
        return out.astype(np.float32)

    def _apply_preemphasis(self, audio, tau, key):
        if key not in self.preemph_state: self.preemph_state[key] = 0.0
        alpha = tau * self.audio_rate
        prev = self.preemph_state[key]
        out = np.zeros_like(audio)
        for i in range(len(audio)):
            out[i] = audio[i] + alpha * (audio[i] - prev)
            prev = audio[i]
        self.preemph_state[key] = float(prev)
        peak = np.max(np.abs(out))
        if peak > 0.95: out *= 0.95 / peak
        return out

    # ── Audio ──

    def _get_audio_stereo(self, station_freq, num_samples):
        if self.mp3_samples is None:
            if station_freq not in self.audio_pos: self.audio_pos[station_freq] = 0.0
            ph = self.audio_pos[station_freq]
            t = ph + np.arange(num_samples, dtype=np.float64) * (2*np.pi*440/self.audio_rate)
            self.audio_pos[station_freq] = float(t[-1]) % (2*np.pi)
            mono = np.sin(t).astype(np.float32)
            return mono, mono.copy()

        if station_freq not in self.audio_pos:
            total = len(self.mp3_samples)
            self.audio_pos[station_freq] = hash(station_freq) % max(1, total // 3)

        pos = self.audio_pos[station_freq]
        if self.mp3_channels == 2:
            total = len(self.mp3_samples)
            left = np.zeros(num_samples, dtype=np.float32)
            right = np.zeros(num_samples, dtype=np.float32)
            rem = num_samples; wp = 0
            while rem > 0:
                take = min(rem, total - pos)
                left[wp:wp+take] = self.mp3_samples[pos:pos+take, 0]
                right[wp:wp+take] = self.mp3_samples[pos:pos+take, 1]
                wp += take; rem -= take; pos += take
                if pos >= total: pos = 0
            self.audio_pos[station_freq] = pos
            return left, right
        else:
            total = len(self.mp3_samples)
            chunk = np.zeros(num_samples, dtype=np.float32)
            rem = num_samples; wp = 0
            while rem > 0:
                take = min(rem, total - pos)
                chunk[wp:wp+take] = self.mp3_samples[pos:pos+take]
                wp += take; rem -= take; pos += take
                if pos >= total: pos = 0
            self.audio_pos[station_freq] = pos
            return chunk, chunk.copy()

    def _get_audio_mono(self, freq, n):
        l, _ = self._get_audio_stereo(freq, n)
        return l

    # ── WFM Stereo MPX ──

    def _generate_wfm_mpx(self, bf, n, dt):
        dur = n * dt
        na = int(dur * self.audio_rate) + 1
        left, right = self._get_audio_stereo(bf, na)

        left = self._apply_fir(left, self.wfm_lp_taps, self.wfm_lp_state, f"wfm_L_{bf}")
        right = self._apply_fir(right, self.wfm_lp_taps, self.wfm_lp_state, f"wfm_R_{bf}")
        left = self._apply_preemphasis(left, WFM_PREEMPH_TAU, f"pe_L_{bf}")
        right = self._apply_preemphasis(right, WFM_PREEMPH_TAU, f"pe_R_{bf}")

        mono = (left + right) * 0.5
        diff = (left - right) * 0.5

        mono_r = np.interp(np.linspace(0, len(mono)-1, n), np.arange(len(mono)), mono)
        diff_r = np.interp(np.linspace(0, len(diff)-1, n), np.arange(len(diff)), diff)

        key = f"pilot_{bf}"
        if key not in self.pilot_phase: self.pilot_phase[key] = 0.0
        t_idx = np.arange(n, dtype=np.float64)
        pilot_ph = self.pilot_phase[key] + 2 * np.pi * WFM_PILOT_FREQ * t_idx * dt
        self.pilot_phase[key] = float(pilot_ph[-1]) % (2*np.pi*1000)

        pilot = WFM_PILOT_LEVEL * np.sin(pilot_ph)
        subcarrier = np.sin(2.0 * pilot_ph)

        return 0.45 * mono_r + pilot + 0.45 * diff_r * subcarrier

    # ── Noise ──

    def _space_noise(self, n, fs):
        t = (self.sample_counter + np.arange(n, dtype=np.float64)) / fs
        sw1 = np.sin(2*np.pi*800*np.sin(2*np.pi*0.3*t)*t) * 0.15
        sw2 = np.cos(2*np.pi*500*np.cos(2*np.pi*0.17*t)*t) * 0.1
        burst = np.abs(np.sin(2*np.pi*2.3*t))**8
        crackle = np.random.normal(0,1,n) * burst * 0.2
        rumble = 0.04 * np.sin(2*np.pi*23*t + 3*np.sin(2*np.pi*0.1*t))
        return sw1+crackle+rumble, sw2+np.roll(crackle,50)+rumble*0.7

    # ── IQ Generation (uint8 for rtl_tcp) ──

    def _generate_iq(self):
        n = self.chunk_samples
        fs = self.sample_rate
        dt = 1.0 / fs
        half_bw = fs / 2.0

        i_acc = np.zeros(n, dtype=np.float64)
        q_acc = np.zeros(n, dtype=np.float64)
        has_signal = False

        # FM stations
        for bf in FM_BROADCAST_FREQS:
            fo = bf - self.frequency
            if abs(fo) > half_bw: continue
            has_signal = True
            is_wfm = bf in FM_WFM_FREQS
            fm_dev = WFM_DEVIATION if is_wfm else NFM_DEVIATION
            amp = 0.60

            if is_wfm:
                mpx = self._generate_wfm_mpx(bf, n, dt)
                inst_freq = fo + fm_dev * mpx
            else:
                dur = n * dt; na = int(dur * self.audio_rate) + 1
                audio = self._get_audio_mono(bf, na)
                audio = self._apply_fir(audio, self.nfm_bp_taps, self.nfm_bp_state, f"nfm_{bf}")
                audio = self._apply_preemphasis(audio, NFM_PREEMPH_TAU, f"pe_{bf}")
                audio = np.clip(audio, -0.95, 0.95)
                audio_r = np.interp(np.linspace(0, len(audio)-1, n), np.arange(len(audio)), audio)
                inst_freq = fo + fm_dev * audio_r

            phase = np.cumsum(inst_freq) * dt * 2 * np.pi
            key = f"c_{bf}"
            if key not in self.audio_pos: self.audio_pos[key] = 0.0
            phase += self.audio_pos[key]
            self.audio_pos[key] = float(phase[-1]) % (2*np.pi*10000)
            i_acc += amp * np.cos(phase)
            q_acc += amp * np.sin(phase)

        # AM stations
        for bf in AM_BROADCAST_FREQS:
            fo = bf - self.frequency
            if abs(fo) > half_bw: continue
            has_signal = True
            amp = 0.70; dur = n * dt; na = int(dur * self.audio_rate) + 1
            audio = self._get_audio_mono(bf, na)
            audio = self._apply_fir(audio, self.airband_bp_taps, self.airband_bp_state, f"am_{bf}")
            audio = np.clip(audio, -0.95, 0.95)
            audio_r = np.interp(np.linspace(0, len(audio)-1, n), np.arange(len(audio)), audio)
            envelope = 1.0 + AM_MODULATION_DEPTH * audio_r
            t = (self.sample_counter + np.arange(n, dtype=np.float64)) / fs
            carrier_phase = 2 * np.pi * fo * t
            key = f"am_{bf}"
            if key not in self.audio_pos: self.audio_pos[key] = 0.0
            carrier_phase += self.audio_pos[key]
            self.audio_pos[key] = float(carrier_phase[-1]) % (2*np.pi*10000)
            i_acc += amp * envelope * np.cos(carrier_phase)
            q_acc += amp * envelope * np.sin(carrier_phase)

        if not has_signal:
            ni, nq = self._space_noise(n, fs)
            i_acc += ni; q_acc += nq

        i_acc += np.random.normal(0, 0.012, n)
        q_acc += np.random.normal(0, 0.012, n)

        # RTL-SDR gain model: gain in tenths of dB
        gain_db = self.gain / 10.0
        g = 10**(gain_db / 60) * 0.12
        i_acc *= g; q_acc *= g

        # RTL-SDR uses unsigned 8-bit IQ (center=127)
        ii = np.clip(i_acc * 127 + 127, 0, 255).astype(np.uint8)
        qi = np.clip(q_acc * 127 + 127, 0, 255).astype(np.uint8)
        iq = np.empty(n * 2, dtype=np.uint8)
        iq[0::2] = ii; iq[1::2] = qi

        self.sample_counter += n
        if self.sample_counter > fs * 10: self.sample_counter = 0
        return iq.tobytes()

    # ── rtl_tcp binary command handler ──

    def _handle_command(self, cmd_byte, param):
        """Handle rtl_tcp 5-byte command: 1 byte cmd + 4 byte big-endian param."""
        if cmd_byte == CMD_SET_FREQ:
            self.frequency = param
            self.sample_counter = 0
            self._log(f"Freq->{param/1e6:.3f}MHz")
        elif cmd_byte == CMD_SET_SAMPLE_RATE:
            if 225001 <= param <= 3200000:
                self.sample_rate = param
                self.sample_counter = 0
                self._log(f"SR->{param/1e6:.3f}MHz")
        elif cmd_byte == CMD_SET_GAIN_MODE:
            self.gain_mode = param
            self._log(f"GainMode->{'manual' if param else 'auto'}")
        elif cmd_byte == CMD_SET_GAIN:
            self.gain = param  # tenths of dB
            self._log(f"Gain->{param/10:.1f}dB")
        elif cmd_byte == CMD_SET_FREQ_CORR:
            self.freq_correction = param
        elif cmd_byte == CMD_SET_IF_GAIN:
            # param encodes stage and gain: stage in upper 16, gain in lower 16
            stage = (param >> 16) & 0xFFFF
            gain_val = param & 0xFFFF
            self._log(f"IFGain stage{stage}={gain_val/10:.1f}dB")
        elif cmd_byte == CMD_SET_AGC_MODE:
            self.agc_mode = param
            self._log(f"AGC->{'ON' if param else 'OFF'}")
        elif cmd_byte == CMD_SET_DIRECT_SAMP:
            self.direct_sampling = param
        elif cmd_byte == CMD_SET_OFFSET_TUNE:
            self.offset_tuning = param
        elif cmd_byte == CMD_SET_BIAS_TEE:
            self.bias_tee = param
            self._log(f"BiasTee->{'ON' if param else 'OFF'}")

    # ── Client handler ──

    def _handle_client(self, client, addr):
        """Handle a single rtl_tcp client connection."""
        self._log(f"Client connected: {addr[0]}:{addr[1]}")

        # Send 12-byte dongle info header
        # struct: "RTL0" (4 bytes) + tuner_type (4 bytes BE) + gain_count (4 bytes BE)
        header = b'RTL0' + struct.pack('>II', RTLSDR_TUNER_R820T, 29)  # R820T has 29 gain steps
        try:
            client.sendall(header)
        except:
            self._log("Failed to send header")
            client.close()
            return

        # Start command receiver thread
        cmd_running = threading.Event()
        cmd_running.set()

        def cmd_receiver():
            client.settimeout(0.5)
            buf = b''
            while cmd_running.is_set() and self.running:
                try:
                    data = client.recv(4096)
                    if not data:
                        cmd_running.clear()
                        break
                    buf += data
                    while len(buf) >= 5:
                        cmd_byte = buf[0]
                        param = struct.unpack('>I', buf[1:5])[0]
                        self._handle_command(cmd_byte, param)
                        buf = buf[5:]
                except socket.timeout:
                    continue
                except:
                    cmd_running.clear()
                    break

        cmd_thread = threading.Thread(target=cmd_receiver, daemon=True)
        cmd_thread.start()

        # Stream IQ data at real-time rate
        bytes_per_sec = 0
        stream_start = 0.0
        total_streamed = 0

        while cmd_running.is_set() and self.running:
            if bytes_per_sec != self.sample_rate * 2:
                bytes_per_sec = self.sample_rate * 2
                stream_start = time.perf_counter()
                total_streamed = 0

            if stream_start == 0.0:
                stream_start = time.perf_counter()

            try:
                iq = self._generate_iq()
                client.sendall(iq)
                self.total_bytes_sent += len(iq)
                total_streamed += len(iq)

                target_time = stream_start + total_streamed / bytes_per_sec
                wait = target_time - time.perf_counter()
                if wait > 0.001:
                    time.sleep(wait)
                elif wait < -0.5:
                    stream_start = time.perf_counter()
                    total_streamed = 0

            except (BrokenPipeError, ConnectionResetError, OSError):
                break

        cmd_running.clear()
        try: client.close()
        except: pass
        self._log(f"Client disconnected: {addr[0]}:{addr[1]}")

    # ── Server ──

    def start(self):
        self.running = True
        print("  Loading audio...")
        self.mp3_samples, self.mp3_channels = load_audio(self.mp3_path)

        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(('0.0.0.0', self.port))
        srv.listen(5)
        srv.settimeout(1.0)

        print("=" * 60)
        print("  RTL-SDR TCP Emulator (rtl_tcp protocol)")
        print("=" * 60)
        print(f"  Port: {self.port}")
        ch = f"{self.mp3_channels}ch" if self.mp3_samples is not None else "440Hz"
        print(f"  Audio: {ch}")
        print(f"\n  WFM: {', '.join(f'{f/1e6:.3f}MHz' for f in FM_WFM_FREQS)} [STEREO]")
        print(f"  NFM: {', '.join(f'{f/1e6:.3f}MHz' for f in FM_NFM_FREQS)}")
        print(f"  AM:  {', '.join(f'{f/1e6:.3f}MHz' for f in AM_BROADCAST_FREQS)}")
        print(f"\n  Connect with: rtl_tcp client -> 127.0.0.1:{self.port}")
        print(f"  {'=' * 60}\n  Waiting...\n")

        try:
            while self.running:
                try:
                    client, addr = srv.accept()
                    client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    client.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 262144)
                    threading.Thread(target=self._handle_client, args=(client, addr), daemon=True).start()
                except socket.timeout:
                    continue
                except OSError:
                    break
        except KeyboardInterrupt:
            print("\nStopping...")
        self.running = False
        srv.close()

    def _log(self, m):
        print(f"[{time.strftime('%H:%M:%S')}] {m}", flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description="RTL-SDR TCP Emulator (rtl_tcp compatible)")
    p.add_argument('--port', type=int, default=1234, help="TCP port (default: 1234)")
    p.add_argument('--mp3', type=str, default=None, help="Path to audio file (mp3/wav)")
    a = p.parse_args()
    emu = RtlSdrEmulator(a.port, a.mp3)
    signal.signal(signal.SIGINT, lambda s, f: setattr(emu, 'running', False))
    emu.start()
