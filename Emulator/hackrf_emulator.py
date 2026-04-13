#!/usr/bin/env python3
"""
HackRF TCP Emulator v5 - MP3 Radio Broadcast + Space Noise
Only requires: pip install numpy

MP3 decoding attempts (in order):
  1. pydub (if installed)
  2. ffmpeg from MSYS2 paths or script directory
  3. Windows powershell MediaPlayer decode
  4. WAV file fallback (put FaithlessInsomnia.wav in same dir)
  5. 440Hz tone fallback

Place FaithlessInsomnia.mp3 (or .wav) in the same directory as this script.
"""

import socket, threading, math, time, argparse, sys, signal, os, struct, queue

try:
    import numpy as np
except ImportError:
    print("ERROR: numpy required. pip install numpy"); sys.exit(1)

# ============================================================
# Audio Loader - tries multiple methods
# ============================================================

def load_wav(filepath):
    """Load WAV file using Python's wave module (no dependencies)."""
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
            return None

        # Mix to mono
        if nch > 1:
            data = data.reshape(-1, nch).mean(axis=1)

        # Resample to 48kHz if needed
        if fr != 48000:
            old_len = len(data)
            new_len = int(old_len * 48000 / fr)
            data = np.interp(np.linspace(0, old_len - 1, new_len), np.arange(old_len), data)

        return data.astype(np.float32)
    except Exception as e:
        print(f"    WAV load failed: {e}")
        return None


def load_audio(mp3_path=None):
    """Try multiple methods to load audio. Returns float32 mono samples at 48kHz."""
    script_dir = os.path.dirname(os.path.abspath(__file__))

    # Find the audio file
    candidates = []
    if mp3_path:
        candidates.append(mp3_path)
    candidates += [
        os.path.join(script_dir, 'FaithlessInsomnia.mp3'),
        os.path.join(script_dir, 'faithlessinsomnia.mp3'),
        'FaithlessInsomnia.mp3',
    ]

    mp3_file = None
    for c in candidates:
        if os.path.exists(c):
            mp3_file = c
            print(f"  Found audio: {mp3_file}")
            break

    if mp3_file is None:
        # Try WAV fallback
        wav_candidates = [
            os.path.join(script_dir, 'FaithlessInsomnia.wav'),
            os.path.join(script_dir, 'faithlessinsomnia.wav'),
            'FaithlessInsomnia.wav',
        ]
        for w in wav_candidates:
            if os.path.exists(w):
                print(f"  Found WAV: {w}")
                samples = load_wav(w)
                if samples is not None:
                    print(f"  Loaded WAV: {len(samples)} samples, {len(samples)/48000:.1f}s")
                    return samples
        print("  No audio file found")
        return None

    # Method 1: pydub
    try:
        from pydub import AudioSegment
        audio = AudioSegment.from_mp3(mp3_file)
        audio = audio.set_frame_rate(48000).set_channels(1).set_sample_width(2)
        raw = np.frombuffer(audio.raw_data, dtype=np.int16)
        samples = raw.astype(np.float32) / 32768.0
        print(f"  Loaded via pydub: {len(samples)/48000:.1f}s")
        return samples
    except Exception as e:
        print(f"  pydub: {e}")

    # Method 2: ffmpeg from various paths
    import subprocess
    ffmpeg_paths = [
        os.path.join(script_dir, 'ffmpeg.exe'),
        r'C:\msys64\mingw64\bin\ffmpeg.exe',
        r'C:\msys64\ucrt64\bin\ffmpeg.exe',
        r'C:\msys64\clang64\bin\ffmpeg.exe',
        'ffmpeg',
    ]
    for fp in ffmpeg_paths:
        try:
            r = subprocess.run([fp, '-i', mp3_file, '-f', 's16le', '-acodec', 'pcm_s16le',
                                '-ac', '1', '-ar', '48000', '-v', 'quiet', '-'],
                               capture_output=True, timeout=120)
            if r.returncode == 0 and len(r.stdout) > 1000:
                samples = np.frombuffer(r.stdout, dtype=np.int16).astype(np.float32) / 32768.0
                print(f"  Loaded via ffmpeg ({fp}): {len(samples)/48000:.1f}s")
                return samples
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
        except Exception:
            continue
    print("  ffmpeg: not found")

    # Method 3: PowerShell + .NET MediaFoundation decode to WAV then read
    try:
        wav_tmp = os.path.join(script_dir, '_temp_decoded.wav')
        ps_script = f'''
Add-Type -AssemblyName PresentationCore
$player = New-Object System.Windows.Media.MediaPlayer
$player.Open([System.Uri]::new("{os.path.abspath(mp3_file)}"))
Start-Sleep -Milliseconds 500
$duration = $player.NaturalDuration.TimeSpan.TotalSeconds
$player.Close()

# Use NAudio-like approach with .NET
[System.Reflection.Assembly]::LoadWithPartialName("WindowsBase") | Out-Null
$src = New-Object System.Uri("{os.path.abspath(mp3_file)}")
# Simple approach: use ffmpeg from Windows Store or just output silence marker
"DURATION:$duration"
'''
        # This is too complex, skip
        raise Exception("PowerShell method not reliable")
    except Exception:
        pass

    # Method 4: Try reading as WAV (maybe user already converted)
    wav_path = mp3_file.rsplit('.', 1)[0] + '.wav'
    if os.path.exists(wav_path):
        samples = load_wav(wav_path)
        if samples is not None:
            print(f"  Loaded WAV fallback: {len(samples)/48000:.1f}s")
            return samples

    print("  All decode methods failed.")
    print("  TIP: Convert MP3 to WAV manually and place in same directory:")
    print(f"       FaithlessInsomnia.wav (16-bit, any sample rate)")
    return None


# ============================================================
# Broadcast config — each station emulates a real radio transmitter
# ============================================================

# NFM stations (PMR/Amateur — real handheld telsiz)
# Real NFM: 300Hz-3kHz voice band, 50µs pre-emphasis, max deviation ±2.5kHz (12.5kHz ch)
FM_NFM_FREQS = [145000000, 446000000]
NFM_DEVIATION = 2500       # ±2.5 kHz max deviation (12.5kHz channel)
NFM_AUDIO_LOW  = 300.0     # Hz — voice HPF
NFM_AUDIO_HIGH = 3000.0    # Hz — voice LPF
NFM_PREEMPH_TAU = 50e-6    # 50µs pre-emphasis (European standard)

# WFM station (FM broadcast radio)
# Real WFM: 30Hz-15kHz audio, 50µs pre-emphasis (EUR) or 75µs (US), ±75kHz deviation
FM_WFM_FREQS = [100000000]
WFM_DEVIATION = 75000      # ±75 kHz max deviation
WFM_AUDIO_HIGH = 15000.0   # Hz — broadcast audio max
WFM_PREEMPH_TAU = 50e-6    # 50µs pre-emphasis (European)

# Combined FM list
FM_BROADCAST_FREQS = FM_NFM_FREQS + FM_WFM_FREQS

# AM stations (airband — real aviation telsiz)
# ETSI EN 300 676-1: DSB-FC (A3E), 300Hz-3kHz voice, 8.33kHz channel
AM_BROADCAST_FREQS = [119100000]  # 119.100 MHz airband
AM_MODULATION_DEPTH = 0.50  # 50% modulation depth
AIRBAND_AUDIO_LOW  = 300.0
AIRBAND_AUDIO_HIGH = 3000.0
AIRBAND_CHANNEL_BW = 8330   # 8.33 kHz (ICAO EUR)

# Combined list for range checks
ALL_BROADCAST_FREQS = FM_BROADCAST_FREQS + AM_BROADCAST_FREQS


class HackRFEmulator:
    def __init__(self, data_port=5000, control_port=5001, audio_port=5002, mp3_path=None):
        self.data_port = data_port
        self.control_port = control_port
        self.audio_port = audio_port
        self.mp3_path = mp3_path

        self.frequency = 145000000
        self.sample_rate = 2000000
        self.vga_gain = 40
        self.lna_gain = 40
        self.rx_amp_gain = 14
        self.tx_amp_gain = 47
        self.amp_enable = False
        self.modulation_index = 0.40
        self.amplitude = 0.10
        self.modulation_type = 0  # 0=NFM, 1=WFM, 2=AM (from client)
        self.is_tx_mode = False

        self.chunk_samples = 32768  # larger chunks for smoother streaming
        self.sample_counter = 0

        self.mp3_samples = None
        self.audio_pos = {}
        self.audio_rate = 48000

        # Pre-compute airband bandpass filter (300Hz-3kHz at 48kHz sample rate)
        self.airband_bp_taps = self._design_bandpass(
            AIRBAND_AUDIO_LOW, AIRBAND_AUDIO_HIGH, self.audio_rate, num_taps=127)
        self.airband_bp_state = {}  # per-station filter delay line

        # Pre-compute NFM voice bandpass filter (300Hz-3kHz at 48kHz)
        self.nfm_bp_taps = self._design_bandpass(
            NFM_AUDIO_LOW, NFM_AUDIO_HIGH, self.audio_rate, num_taps=127)
        self.nfm_bp_state = {}

        # Pre-compute WFM audio lowpass filter (15kHz at 48kHz)
        self.wfm_lp_taps = self._design_lowpass(WFM_AUDIO_HIGH, self.audio_rate, num_taps=63)
        self.wfm_lp_state = {}

        # Pre-emphasis filter state (per-station)
        self.preemph_state = {}  # stores previous sample for each station

        self.tx_audio_buffer = bytearray()
        self.tx_audio_lock = threading.Lock()

        self.running = False
        self.data_clients = []
        self.control_clients = []
        self.audio_clients = []
        self.data_lock = threading.Lock()
        self.total_bytes_sent = 0

        # Pre-buffer queue for smooth streaming
        self.iq_queue = queue.Queue(maxsize=8)

    def _get_audio_chunk(self, station_freq, num_samples):
        if self.mp3_samples is None:
            # 440Hz fallback
            if station_freq not in self.audio_pos:
                self.audio_pos[station_freq] = 0.0
            ph = self.audio_pos[station_freq]
            t = ph + np.arange(num_samples, dtype=np.float64) * (2*np.pi*440/self.audio_rate)
            self.audio_pos[station_freq] = float(t[-1]) % (2*np.pi)
            return np.sin(t).astype(np.float32)

        if station_freq not in self.audio_pos:
            self.audio_pos[station_freq] = hash(station_freq) % max(1, len(self.mp3_samples)//3)

        pos = self.audio_pos[station_freq]
        total = len(self.mp3_samples)
        chunk = np.zeros(num_samples, dtype=np.float32)
        rem = num_samples; wp = 0
        while rem > 0:
            take = min(rem, total - pos)
            chunk[wp:wp+take] = self.mp3_samples[pos:pos+take]
            wp += take; rem -= take; pos += take
            if pos >= total: pos = 0
        self.audio_pos[station_freq] = pos
        return chunk

    @staticmethod
    def _design_bandpass(f_low, f_high, fs, num_taps=127):
        """Design a FIR bandpass filter using windowed-sinc method (Hamming).
        300Hz-3kHz bandpass for airband voice channel simulation."""
        n = np.arange(num_taps)
        M = num_taps // 2
        # Low-pass at f_high
        fc_hi = f_high / fs
        h_hi = np.where(n == M, 2*fc_hi,
                        np.sin(2*np.pi*fc_hi*(n - M)) / (np.pi*(n - M)))
        # Low-pass at f_low
        fc_lo = f_low / fs
        h_lo = np.where(n == M, 2*fc_lo,
                        np.sin(2*np.pi*fc_lo*(n - M)) / (np.pi*(n - M)))
        # Bandpass = HPF(f_low) via spectral inversion of LPF
        h_bp = h_hi - h_lo
        # Hamming window
        w = 0.54 - 0.46 * np.cos(2*np.pi*n / (num_taps - 1))
        h_bp *= w
        # Normalize passband gain to 1.0
        # Evaluate gain at center of passband
        f_center = (f_low + f_high) / 2.0
        omega = 2 * np.pi * f_center / fs
        gain = np.abs(np.sum(h_bp * np.exp(-1j * omega * n)))
        if gain > 0:
            h_bp /= gain
        return h_bp.astype(np.float64)

    def _apply_airband_filter(self, audio, station_freq):
        """Apply 300Hz-3kHz bandpass filter with persistent state per station."""
        key = f"bp_{station_freq}"
        taps = self.airband_bp_taps
        ntaps = len(taps)

        # Get or create delay line
        if key not in self.airband_bp_state:
            self.airband_bp_state[key] = np.zeros(ntaps - 1, dtype=np.float64)
        delay = self.airband_bp_state[key]

        # Prepend delay line for overlap-save convolution
        extended = np.concatenate([delay, audio.astype(np.float64)])
        out = np.zeros(len(audio), dtype=np.float64)
        for i in range(len(audio)):
            out[i] = np.dot(taps, extended[i:i+ntaps])

        # Save tail for next call
        if len(audio) >= ntaps - 1:
            self.airband_bp_state[key] = audio[-(ntaps-1):].astype(np.float64)
        else:
            keep = (ntaps - 1) - len(audio)
            self.airband_bp_state[key] = np.concatenate([
                delay[-keep:], audio.astype(np.float64)])

        return out.astype(np.float32)

    @staticmethod
    def _design_lowpass(f_cutoff, fs, num_taps=63):
        """Design a FIR lowpass filter using windowed-sinc (Hamming)."""
        n = np.arange(num_taps)
        M = num_taps // 2
        fc = f_cutoff / fs
        h = np.where(n == M, 2*fc,
                     np.sin(2*np.pi*fc*(n - M)) / (np.pi*(n - M)))
        w = 0.54 - 0.46 * np.cos(2*np.pi*n / (num_taps - 1))
        h *= w
        s = np.sum(h)
        if s > 0:
            h /= s
        return h.astype(np.float64)

    def _apply_fir_filter(self, audio, taps, state_dict, station_freq, prefix="fir"):
        """Apply FIR filter with persistent state per station."""
        key = f"{prefix}_{station_freq}"
        ntaps = len(taps)

        if key not in state_dict:
            state_dict[key] = np.zeros(ntaps - 1, dtype=np.float64)
        delay = state_dict[key]

        extended = np.concatenate([delay, audio.astype(np.float64)])
        out = np.zeros(len(audio), dtype=np.float64)
        for i in range(len(audio)):
            out[i] = np.dot(taps, extended[i:i+ntaps])

        if len(audio) >= ntaps - 1:
            state_dict[key] = audio[-(ntaps-1):].astype(np.float64)
        else:
            keep = (ntaps - 1) - len(audio)
            state_dict[key] = np.concatenate([
                delay[-keep:], audio.astype(np.float64)])

        return out.astype(np.float32)

    def _apply_preemphasis(self, audio, tau, station_freq):
        """Apply FM pre-emphasis filter: H(s) = 1 + s*tau
        Discrete: y[n] = x[n] - alpha * x[n-1], where alpha = 1/(1 + 2*pi*tau*fs)
        This boosts high frequencies like a real FM transmitter."""
        key = f"preemph_{station_freq}"
        if key not in self.preemph_state:
            self.preemph_state[key] = 0.0

        # Pre-emphasis: first-order high-shelf
        # y[n] = x[n] + alpha * (x[n] - x[n-1])
        # alpha = tau * fs (time constant × sample rate)
        alpha = tau * self.audio_rate
        prev = self.preemph_state[key]
        out = np.zeros_like(audio)
        for i in range(len(audio)):
            out[i] = audio[i] + alpha * (audio[i] - prev)
            prev = audio[i]
        self.preemph_state[key] = float(prev)

        # Normalize to prevent clipping from boost
        peak = np.max(np.abs(out))
        if peak > 0.95:
            out *= 0.95 / peak

        return out

    def _space_noise(self, n, fs):
        t = (self.sample_counter + np.arange(n, dtype=np.float64)) / fs
        # Whistler sweep
        sw1 = np.sin(2*np.pi * 800 * np.sin(2*np.pi*0.3*t) * t) * 0.15
        sw2 = np.cos(2*np.pi * 500 * np.cos(2*np.pi*0.17*t) * t) * 0.1
        # Crackle bursts
        burst = np.abs(np.sin(2*np.pi*2.3*t))**8
        crackle = np.random.normal(0,1,n) * burst * 0.2
        # Rumble
        rumble = 0.04 * np.sin(2*np.pi*23*t + 3*np.sin(2*np.pi*0.1*t))
        i_s = sw1 + crackle + rumble
        q_s = sw2 + np.roll(crackle, 50) + rumble*0.7
        return i_s, q_s

    def _generate_iq(self):
        n = self.chunk_samples
        fs = self.sample_rate
        dt = 1.0 / fs
        half_bw = fs / 2.0

        i_acc = np.zeros(n, dtype=np.float64)
        q_acc = np.zeros(n, dtype=np.float64)
        has_music = False

        # FM broadcast stations
        for bf in FM_BROADCAST_FREQS:
            fo = bf - self.frequency
            if abs(fo) > half_bw: continue
            has_music = True

            is_wfm = bf in FM_WFM_FREQS
            fm_dev = WFM_DEVIATION if is_wfm else NFM_DEVIATION
            amp = 0.60

            dur = n * dt
            na = int(dur * self.audio_rate) + 1
            audio = self._get_audio_chunk(bf, na)

            if is_wfm:
                # WFM transmitter chain: LPF 15kHz → pre-emphasis 50µs → FM mod
                audio = self._apply_fir_filter(audio, self.wfm_lp_taps,
                                               self.wfm_lp_state, bf, "wfm_lp")
                audio = self._apply_preemphasis(audio, WFM_PREEMPH_TAU, bf)
            else:
                # NFM transmitter chain: BPF 300-3kHz → pre-emphasis 50µs → FM mod
                audio = self._apply_fir_filter(audio, self.nfm_bp_taps,
                                               self.nfm_bp_state, bf, "nfm_bp")
                audio = self._apply_preemphasis(audio, NFM_PREEMPH_TAU, bf)
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

        # AM broadcast stations — DSB-FC (A3E) per ETSI EN 300 676-1
        # Airband: 8.33kHz channel, 300-3kHz voice band
        for bf in AM_BROADCAST_FREQS:
            fo = bf - self.frequency
            if abs(fo) > half_bw: continue
            has_music = True

            # Carrier amplitude — sets int8 dynamic range utilization
            # With 50% mod depth: envelope swings 0.5 to 1.5 × carrier
            # carrier=0.70 → IQ peak = 0.70*1.5*gain ≈ 0.23 → int8 ≈ 29 (good SNR)
            amp = 0.70

            dur = n * dt
            na = int(dur * self.audio_rate) + 1
            audio = self._get_audio_chunk(bf, na)

            # 1. Apply 300Hz-3kHz bandpass filter (airband voice channel)
            audio = self._apply_airband_filter(audio, bf)

            # 2. Limiter — prevent over-modulation (keep envelope > 0)
            # With 50% depth, audio must stay within ±1.0 to keep envelope positive
            # Clip to ±0.95 for safety margin
            audio = np.clip(audio, -0.95, 0.95)

            # 3. Resample filtered audio to IQ sample rate
            audio_r = np.interp(np.linspace(0, len(audio)-1, n), np.arange(len(audio)), audio)

            # 4. DSB-FC AM: s(t) = A · (1 + m·audio(t)) · cos(2π·f·t)
            # This is the standard aviation AM formula per ICAO Annex 10
            # m = modulation index (0.0 to 1.0)
            # envelope = 1 + m*audio, always positive when |audio| ≤ 1/m
            envelope = 1.0 + AM_MODULATION_DEPTH * audio_r

            # 5. Carrier phase — continuous across chunks
            t = (self.sample_counter + np.arange(n, dtype=np.float64)) / fs
            carrier_phase = 2 * np.pi * fo * t

            key = f"am_{bf}"
            if key not in self.audio_pos: self.audio_pos[key] = 0.0
            carrier_phase += self.audio_pos[key]
            self.audio_pos[key] = float(carrier_phase[-1]) % (2*np.pi*10000)

            # 6. Generate IQ — DSB-FC with both sidebands symmetric around carrier
            i_acc += amp * envelope * np.cos(carrier_phase)
            q_acc += amp * envelope * np.sin(carrier_phase)

        if not has_music:
            ni, nq = self._space_noise(n, fs)
            i_acc += ni; q_acc += nq

        i_acc += np.random.normal(0, 0.012, n)
        q_acc += np.random.normal(0, 0.012, n)

        # Gain scaling: S3-S5 at mid, S7 at max
        total_gain_db = self.vga_gain * 0.2 + self.lna_gain * 0.2
        if self.amp_enable: total_gain_db += 4
        g = 10**(total_gain_db / 60) * 0.12
        i_acc *= g; q_acc *= g

        ii = np.clip(i_acc*127, -127, 127).astype(np.int8)
        qi = np.clip(q_acc*127, -127, 127).astype(np.int8)
        iq = np.empty(n*2, dtype=np.int8)
        iq[0::2] = ii; iq[1::2] = qi

        self.sample_counter += n
        if self.sample_counter > fs*10: self.sample_counter = 0
        return iq.tobytes()

    def calculate_tx_power_dbm(self):
        e = -40 + float(self.tx_amp_gain) + 20*math.log10(max(self.amplitude,0.01))
        if self.amp_enable: e += 14
        fg = self.frequency/1e9
        if fg > 2: e -= (fg-2)*5
        elif fg < 0.1: e -= (0.1-fg)*10
        return max(-60, min(15, e))

    def handle_command(self, cmd):
        p = cmd.strip().split(':'); c = p[0].upper()
        try:
            if c=="SET_FREQ" and len(p)==2:
                f=int(p[1])
                if 1e6<=f<=6e9: self.frequency=f; self.sample_counter=0; self._log(f"Freq->{f/1e6:.3f}MHz"); return f"OK: Frequency set to {f} Hz\n"
            elif c=="SET_SAMPLE_RATE" and len(p)==2:
                s=int(p[1])
                if 2e6<=s<=20e6: self.sample_rate=s; self.sample_counter=0; self._log(f"SR->{s/1e6:.1f}MHz"); return f"OK: SR set to {s}\n"
            elif c=="SET_VGA_GAIN" and len(p)==2:
                g=int(p[1])
                if 0<=g<=62: self.vga_gain=g; return f"OK: VGA={g}\n"
            elif c=="SET_LNA_GAIN" and len(p)==2:
                g=int(p[1])
                if 0<=g<=40: self.lna_gain=g; return f"OK: LNA={g}\n"
            elif c=="SET_RX_AMP_GAIN" and len(p)==2:
                g=int(p[1])
                if 0<=g<=14: self.rx_amp_gain=g; return f"OK: RXamp={g}\n"
            elif c=="SET_TX_AMP_GAIN" and len(p)==2:
                g=int(p[1])
                if 0<=g<=47: self.tx_amp_gain=g; return f"OK: TXamp={g}\n"
            elif c=="SET_AMP_ENABLE" and len(p)==2:
                v=int(p[1])
                if v in(0,1): self.amp_enable=v==1; self._log(f"RFamp->{'ON'if self.amp_enable else'OFF'}"); return f"OK: RF amp {'en'if self.amp_enable else'dis'}abled\n"
            elif c=="SET_MODULATION_INDEX" and len(p)==2:
                v=float(p[1])
                if 0.1<=v<=20: self.modulation_index=v; return f"OK: ModIdx={v}\n"
            elif c=="SET_AMPLITUDE" and len(p)==2:
                v=float(p[1])
                if 0<=v<=2: self.amplitude=v; return f"OK: Amp={v}\n"
            elif c=="SET_MODULATION_TYPE" and len(p)==2:
                v=int(p[1])
                if v in(0,1,2):
                    self.modulation_type=v
                    names={0:"NFM",1:"WFM",2:"AM"}
                    self._log(f"ModType->{names[v]}")
                    return f"OK: Modulation={names[v]}\n"
            elif c=="SWITCH_RX": self.is_tx_mode=False; self.sample_counter=0; self._log("RX"); return "OK: Switched to RX mode\n"
            elif c=="SWITCH_TX": self.is_tx_mode=True; self._log(f"TX {self.calculate_tx_power_dbm():.1f}dBm"); return "OK: Switched to TX mode\n"
            elif c=="GET_STATUS": return f"Mode:{'TX'if self.is_tx_mode else'RX'} {self.frequency/1e6:.3f}MHz TXpwr:{self.calculate_tx_power_dbm():.1f}dBm\n"
            elif c=="HELP": return "SET_FREQ SET_SAMPLE_RATE SET_VGA_GAIN SET_LNA_GAIN SET_RX_AMP_GAIN SET_TX_AMP_GAIN SET_AMP_ENABLE SET_MODULATION_INDEX SET_MODULATION_TYPE SET_AMPLITUDE SWITCH_RX SWITCH_TX GET_STATUS\n"
        except: pass
        return "ERROR\n"

    # ── TCP ──
    def _iq_producer(self):
        """Generate IQ chunks into queue."""
        while self.running:
            if self.is_tx_mode or not self.data_clients:
                time.sleep(0.02); continue
            try:
                iq = self._generate_iq()
                self.iq_queue.put(iq, timeout=0.5)
            except queue.Full:
                time.sleep(0.001)
            except Exception as e:
                self._log(f"IQ gen err: {e}"); time.sleep(0.01)

    def _data_stream(self):
        """Send IQ at exact real-time rate using clock-based pacing."""
        bytes_per_sec = 0
        stream_start = 0.0
        total_streamed = 0

        while self.running:
            if self.is_tx_mode or not self.data_clients:
                while not self.iq_queue.empty():
                    try: self.iq_queue.get_nowait()
                    except: break
                stream_start = 0.0
                total_streamed = 0
                time.sleep(0.02); continue

            # Initialize stream timing on first chunk
            if stream_start == 0.0:
                bytes_per_sec = self.sample_rate * 2  # 2 bytes per IQ pair
                stream_start = time.perf_counter()
                total_streamed = 0

            try:
                iq = self.iq_queue.get(timeout=0.05)
            except queue.Empty:
                continue

            # Send data
            dead = []
            with self.data_lock:
                for c in self.data_clients:
                    try: c.sendall(iq); self.total_bytes_sent += len(iq)
                    except: dead.append(c)
                for d in dead:
                    self.data_clients.remove(d); self._log("Data disc")
                    try: d.close()
                    except: pass

            total_streamed += len(iq)

            # Calculate when this many bytes should have been sent
            target_time = stream_start + total_streamed / bytes_per_sec
            now = time.perf_counter()
            wait = target_time - now

            if wait > 0.001:
                time.sleep(wait)
            elif wait < -0.5:
                # We're way behind, reset timing
                stream_start = time.perf_counter()
                total_streamed = 0

    def _ctrl(self, cl):
        cl.settimeout(1.0); b=""
        while self.running:
            try:
                d=cl.recv(4096)
                if not d: break
                b+=d.decode('utf-8',errors='ignore')
                while '\n' in b:
                    l,b=b.split('\n',1)
                    if l.strip():
                        try: cl.sendall(self.handle_command(l).encode())
                        except: return
            except socket.timeout: continue
            except: break
        if cl in self.control_clients: self.control_clients.remove(cl)
        self._log("Ctrl disc")

    def _audio(self, cl):
        cl.settimeout(1.0)
        while self.running:
            try:
                d=cl.recv(16384)
                if not d: break
                with self.tx_audio_lock:
                    self.tx_audio_buffer.extend(d)
                    if len(self.tx_audio_buffer)>192000: self.tx_audio_buffer=self.tx_audio_buffer[-192000:]
            except socket.timeout: continue
            except: break
        if cl in self.audio_clients: self.audio_clients.remove(cl)

    def _accept(self, srv, lst, name, handler=None):
        srv.settimeout(1.0)
        while self.running:
            try:
                c,a=srv.accept(); c.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
                lst.append(c); self._log(f"{name}:{a[0]}:{a[1]}")
                if handler: threading.Thread(target=handler,args=(c,),daemon=True).start()
            except socket.timeout: continue
            except OSError: break

    def start(self):
        self.running=True
        print("  Loading audio...")
        self.mp3_samples = load_audio(self.mp3_path)

        svs=[]
        for port in (self.data_port,self.control_port,self.audio_port):
            s=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
            s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
            s.bind(('0.0.0.0',port)); s.listen(5); svs.append(s)

        print("="*60)
        print("  HackRF TCP Emulator v7 - Real Radio Emulation")
        print("="*60)
        print(f"  Ports: {self.data_port}/{self.control_port}/{self.audio_port}")
        print(f"  Audio: {'LOADED '+str(len(self.mp3_samples)//48000)+'s' if self.mp3_samples is not None else '440Hz FALLBACK'}")
        print(f"\n  NFM Stations (voice radio — 300-3kHz BPF, {NFM_PREEMPH_TAU*1e6:.0f}us pre-emph):")
        for bf in FM_NFM_FREQS:
            print(f"    {bf/1e6:>10.3f} MHz [NFM] dev=+/-{NFM_DEVIATION/1000:.1f}kHz")
        print(f"\n  WFM Stations (broadcast — 15kHz LPF, {WFM_PREEMPH_TAU*1e6:.0f}us pre-emph):")
        for bf in FM_WFM_FREQS:
            print(f"    {bf/1e6:>10.3f} MHz [WFM] dev=+/-{WFM_DEVIATION/1000:.0f}kHz")
        print(f"\n  AM Stations (airband DSB-FC A3E — 300-3kHz BPF, 8.33kHz ch):")
        for bf in AM_BROADCAST_FREQS:
            print(f"    {bf/1e6:>10.3f} MHz [AM]  depth={AM_MODULATION_DEPTH*100:.0f}%")
        print(f"\n  Other frequencies -> space noise")
        print(f"\n  Waiting for connections...\n{'='*60}")

        ts=[
            threading.Thread(target=self._accept,args=(svs[0],self.data_clients,"Data"),daemon=True),
            threading.Thread(target=self._accept,args=(svs[1],self.control_clients,"Ctrl",self._ctrl),daemon=True),
            threading.Thread(target=self._accept,args=(svs[2],self.audio_clients,"Audio",self._audio),daemon=True),
            threading.Thread(target=self._data_stream,daemon=True),
            threading.Thread(target=self._iq_producer,daemon=True),
        ]
        for t in ts: t.start()

        try:
            while self.running:
                time.sleep(5)
                if self.data_clients:
                    m="TX"if self.is_tx_mode else"RX"
                    ns=sum(1 for bf in ALL_BROADCAST_FREQS if abs(bf-self.frequency)<=self.sample_rate/2)
                    self._log(f"[{m}] {self.frequency/1e6:.3f}MHz {'MUSIC'if ns else'NOISE'} {self.total_bytes_sent/(1024*1024):.0f}MB")
        except KeyboardInterrupt: print("\nStop")
        self.running=False
        for s in svs: s.close()

    def _log(self,m): print(f"[{time.strftime('%H:%M:%S')}] {m}",flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--data-port',type=int,default=5000)
    p.add_argument('--control-port',type=int,default=5001)
    p.add_argument('--audio-port',type=int,default=5002)
    p.add_argument('--mp3',type=str,default=None)
    a=p.parse_args()
    emu=HackRFEmulator(a.data_port,a.control_port,a.audio_port,a.mp3)
    signal.signal(signal.SIGINT,lambda s,f:setattr(emu,'running',False))
    emu.start()
