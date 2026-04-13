# HackTvRxTx - SDR Transceiver & Analog TV Decoder

A Qt 6.x based SDR (Software Defined Radio) application for HackRF One and RTL-SDR devices. Includes wideband FM stereo receiver with real-time FFT spectrum analyzer, waterfall display, FM stereo transmitter (mic & file), analog TV PAL B/G transmitter, and a standalone PAL-B/G TV decoder with real-time FM audio demodulation.

Based on [fsphil/hacktv](https://github.com/fsphil/hacktv) with significant modifications for cross-platform GUI operation.

![HackTvGui Screenshot](hacktvgui_screen.png)

## Features

### HackTvGui - SDR Transceiver
- **Multi-Device Support**: HackRF One (USB), HackRF TCP (emulator/remote), RTL-SDR TCP (emulator/remote), RTL-SDR (USB) — select from device dropdown with auto-configured connection parameters
- **Mode-Aware UI**: Dynamic theme colors per operating mode — NFM (green), WFM (blue), AM (orange), FM File TX (teal), TV modes (purple). GroupBox borders, slider handles, FFT fill, frequency digits, and START button all change color with mode. TV modes auto-hide the RX panel for a cleaner layout
- **WFM Stereo Decode**: Real-time 19 kHz pilot tone detection via PLL, 38 kHz DSB-SC subcarrier recovery, L+R / L-R channel separation with de-emphasis — true FM stereo broadcast reception. Stereo/Mono toggle via checkbox or click on STEREO indicator
- **Multiple Modulations**: WFM (default, 150 kHz BW), NFM (12.5 kHz BW), AM (10.5 kHz BW) with mode-specific presets for gain, modulation index, de-emphasis, and bandwidth auto-applied on mode change
- **Auto-Restart on Mode Change**: Switching between WFM/NFM/AM while running automatically stops and restarts with new demodulator — no manual stop/start needed
- **TCP Client (Built-in)**: Direct TCP connection to HackRF TCP emulator (3-port text protocol) or RTL-SDR TCP emulator (single-port rtl_tcp binary protocol). GUI sends SET_FREQ, SET_SAMPLE_RATE, SET_LNA_GAIN, SET_VGA_GAIN commands in real-time as sliders change
- **RX Mode**: Wideband FM receiver with real-time audio demodulation
- **TX Mode**: Analog TV transmitter supporting PAL-I, PAL-B/G, PAL-D/K, PAL-M, PAL-N, SECAM-L, SECAM-D/K, NTSC-M, NTSC-A and more
- **FM Stereo Transmitter (Mic)**: Real-time microphone input FM stereo transmitter with 19 kHz pilot tone and 38 kHz DSB-SC subcarrier per FM broadcasting standard
- **FM Stereo Transmitter (File)**: Audio file playback FM stereo transmitter — supports WAV, MP3, FLAC, OGG, AAC, WMA, M4A and video files (audio track extracted). FFmpeg-based decoding with stereo MPX composite generation
- **FM Stereo MPX**: Full FM stereo multiplex signal generation — (L+R) mono-compatible baseband, 19 kHz pilot tone, (L-R)×sin(38 kHz) subcarrier, 75µs pre-emphasis applied per-channel. MPX composite generated directly at TX sample rate (no intermediate resampler) for maximum efficiency
- **USB Hard Reset**: Physical HackRF USB device reset via `hackrf_reset()` — device detaches and re-enumerates on the USB bus (equivalent to physical unplug/replug). Accessible via the HARD RESET button in the GUI
- **OpenGL Spectrum Analyzer**: GPU-accelerated FFT spectrum display with smooth antialiased rendering
- **Waterfall Display**: Real-time scrolling waterfall with SDR#-style color palette (dark blue → cyan → yellow → red)
- **Band Overlay**: Known frequency band allocations displayed on spectrum (FM Broadcast, Ham Radio, Aviation, Marine VHF, TV bands, Cellular, ISM/WiFi, etc.)
- **Frequency Control**: Click-to-tune on spectrum, mouse wheel tuning with digit-proportional step size, draggable filter bandwidth. Enlarged frequency display (68-90px height)
- **Adjustable Gains**: LNA, VGA, TX Amp, RX Amp with real-time control — slider handles color-match the active mode
- **Multiple Sample Rates**: 2, 4, 8, 10, 12.5, 16, 20 MHz
- **European TV Channels**: Pre-configured E2-E69 channel list
- **Video Input Sources**: File (MP4/FLV), test pattern, RTSP stream via FFmpeg
- **Global Dark Theme**: GitHub-dark inspired stylesheet — consistent dark backgrounds, rounded controls, mode-aware accent colors
- **Cross-platform**: Windows (MinGW 64-bit) and macOS

### TCP Emulators

Software emulators that generate realistic RF signals (WFM stereo, NFM, AM) over TCP — no hardware needed for development and testing.

#### HackRF TCP Emulator (`hackrf_emulator.py`)
- **3-Port Protocol**: Data (5000), Control (5001), Audio (5002) — text-based command protocol compatible with HackTvGui "HackRF TCP" and HackRfRadio
- **WFM Stereo Broadcast**: Full MPX composite signal — 19 kHz pilot tone + 38 kHz DSB-SC subcarrier with per-channel 50µs pre-emphasis. Stereo WAV files preserve L/R channel separation
- **NFM Voice Radio**: 300 Hz-3 kHz bandpass filtered voice with 50µs pre-emphasis, +/-2.5 kHz deviation (PMR/amateur simulation at 145 MHz, 446 MHz)
- **AM Airband**: DSB-FC (A3E) at 119.1 MHz with 50% modulation depth, 300 Hz-3 kHz voice bandpass (ICAO standard)
- **Space Noise**: Whistler sweeps, burst crackle, and rumble on empty frequencies
- **Audio Sources**: WAV/MP3 file playback (pydub or ffmpeg decode), 440 Hz tone fallback
- **Real-time Pacing**: Clock-based IQ streaming at exact sample rate with pre-buffer queue

#### RTL-SDR TCP Emulator (`rtlsdr_emulator.py`)
- **rtl_tcp Binary Protocol**: Single port (default 1234), 12-byte dongle header ("RTL0" + R820T tuner type), 5-byte binary commands — compatible with any rtl_tcp client and HackTvGui "RTL-SDR TCP" device mode
- **Same Signal Engine**: Identical WFM stereo, NFM, AM, and space noise generation as HackRF emulator
- **uint8 IQ Format**: Unsigned 8-bit IQ centered at 127 (RTL-SDR native format), vs HackRF's signed int8

### PALBDecoder - Analog TV Receiver
- **PAL-B/G Video Decoder**: Real-time analog TV reception and decoding at 720x576 resolution with SDRangel-style sync detection
- **Dynamic Sample Rate**: Selectable 12.5, 16, 20 MHz — all timing, filters, and decimation chains automatically recalculated per rate
- **Full TV Band Coverage**: VHF Band I (E2-E4, 47-68 MHz), VHF Band III (E5-E12, 174-230 MHz), UHF Band IV/V (E21-E69, 470-862 MHz) with automatic video carrier offset calculation per channel
- **Video Processing**: Adjustable gain, offset, chroma gain, video inversion, color/monochrome mode with automatic AGC
- **Flywheel Sync Detection**: SDRangel-style horizontal sync with fractional zero-crossing detection, flywheel error tracking, and combined quality metric (detection rate + error magnitude). Real-time sync quality percentage display
- **FM Audio Demodulation**: Narrowband FM audio demod pipeline — frequency shift to baseband, complex IQ decimation, narrowband FM demod at ~160 kHz, multi-stage decimation to 48 kHz output. Automatic Nyquist check disables audio when carrier exceeds bandwidth. Adjustable audio gain and volume controls
- **IQ Recording**: Record raw int8 IQ data to file (0.5 second capture) for offline analysis
- **HackRF Integration**: Direct HackRF One control with LNA, VGA, and RX Amp gain settings
- **Multi-threaded Processing**: Separate thread pool for video and audio demodulation with atomic frame-skip guards for video and continuous processing for audio

![PALBDecoder Screenshot](paldecoder.jpg)

### PALBDecoderIOS - Mobile TV Receiver
- **iOS/macOS Port**: Native Swift/Qt port of PALBDecoder for iPhone and iPad
- **Network Streaming**: Connects to HackRF TCP IQ Server (HackRfTcp) over WiFi — no USB connection needed on the mobile device
- **TV & Radio Modes**: PAL-B/G video decoding and FM radio demodulation
- **Real-time Stats**: FPS, sync quality, data rate, and sample rate display
- **Channel Selection**: UHF/VHF European TV channel presets with fine-tuning controls
- **Optimized DSP**: Separate I/Q FIR filters, NCO lookup table, fast magnitude approximation, 15-tap video FIR, decimation factor 2 at 12.5 MHz for efficient mobile performance

<p align="center">
  <img src="palbdecoder_ios.png" alt="PALBDecoderIOS TV Mode" width="300"/>
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="palbdecoder_ios_radio.png" alt="PALBDecoderIOS Radio Mode" width="300"/>
</p>

### HackRfRadio - TCP Remote SDR Radio

A standalone Qt 6.x radio client that connects to HackRfTcp or HackRF TCP Emulator over TCP/IP, enabling remote SDR operation from any device on the network. Designed for both desktop and touch-friendly mobile-style interfaces.

- **FM Stereo Decode**: Real-time 19 kHz pilot tone detection via Goertzel algorithm, PLL-based 38 kHz subcarrier recovery, L/R channel separation — true stereo FM broadcast reception
- **Stereo Indicator**: Live STEREO/MONO status display with click-to-toggle forced mono mode
- **Multiple Modulations**: WFM (Wide FM), NFM (Narrow FM), AM demodulation with mode-specific presets (gain, modulation index, de-emphasis, audio LPF, IF bandwidth all auto-configured per mode)
- **Spectrum Analyzer**: Real-time FFT spectrum display (CPlotter) with SDRuno-style gradient fill, band overlay, adaptive frequency labels
- **Signal Meter**: CMeter dBFS bar with real-time level tracking
- **Squelch**: Adjustable squelch threshold — audio muted when signal level drops below threshold
- **Remote Operation**: Connects to HackRfTcp server over WiFi/LAN — HackRF can run on a Raspberry Pi while the radio client runs on any PC
- **Adjustable Parameters**: VGA, LNA, RX Gain, IF Bandwidth, Modulation Index, De-emphasis, Audio LPF — all settings auto-saved and restored via dedicated settings page
- **PTT Transmit**: Push-to-talk FM transmit via microphone with real-time audio streaming to server. TX power estimation displayed (dBm)
- **Bandwidth Selection**: 2, 4, 8, 10, 12.5, 16, 20 MHz sample rates with +/- cycling buttons
- **Band Presets**: 2m amateur, Marine VHF, FM broadcast, PMR446, 70cm amateur, FRS, LPD433, CB 27 MHz, and Custom — auto-selects appropriate modulation per band
- **Device Toggle**: HackRF/RTL-SDR device selection sent to server (PTT disabled for RTL-SDR)
- **Low-latency Audio**: Dedicated writer thread with stereo output, buffer priming, and QAudioSink volume control for instant response
- **Touch-Friendly UI**: Mobile-optimized layout (393x852 default), large cycling buttons, scrollable interface, Android-compatible

![HackRfRadio Screenshot](hackrfradio_screen.png)

## Architecture

### HackTvGui TCP Client Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  HackTvGui Device Selection                                      │
│                                                                  │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐           │
│  │  HackRF     │  │ HackRF TCP   │  │ RTL-SDR TCP  │           │
│  │  (USB)      │  │ (Emulator)   │  │ (Emulator)   │           │
│  └──────┬──────┘  └──────┬───────┘  └──────┬───────┘           │
│         │                │                  │                    │
│         ▼                ▼                  ▼                    │
│    HackTvLib       3-port TCP          Single-port TCP           │
│    C++ DLL         text protocol       rtl_tcp binary            │
│                    Data:5000           Port:1234                 │
│                    Ctrl:5001           12-byte header             │
│                    Audio:5002          5-byte commands            │
│                    int8 IQ             uint8 IQ (center=127)     │
│         │                │                  │                    │
│         └────────────────┴──────────────────┘                    │
│                          │                                       │
│                          ▼                                       │
│              262144-byte IQ chunks                               │
│              → complex<float> conversion                         │
│              → processDemod() (thread pool)                      │
│              → processFft() (thread pool)                        │
│                          │                                       │
│              ┌───────────┴───────────┐                           │
│              ▼                       ▼                           │
│         FMDemodulator           AMDemodulator                    │
│         (stereo PLL)            (envelope det)                   │
│              │                       │                           │
│              └───────────┬───────────┘                           │
│                          ▼                                       │
│                    AudioOutput                                   │
│                    (48 kHz stereo)                                │
└─────────────────────────────────────────────────────────────────┘
```

### PAL-B/G Audio Demodulation Pipeline

```
HackRF IQ (16 MHz)
       │
       ▼
┌──────────────────────────────────────────────────────────┐
│  Frequency Shift: carrier (5.5 MHz + tune offset) → DC  │
│  Complex IQ, NCO with persistent phase                   │
└──────────────────────────────────────────────────────────┘
       │
       ▼
┌──────────────────────────────────────────────────────────┐
│  Complex IQ Decimation (narrowband filtering)            │
│  Stage 0: 16 MHz → 1.6 MHz (÷10, 33-tap FIR)          │
│  Stage 1: 1.6 MHz → 160 kHz (÷10, 33-tap FIR)         │
│  Anti-alias filter applied to I and Q separately         │
└──────────────────────────────────────────────────────────┘
       │
       ▼
┌──────────────────────────────────────────────────────────┐
│  FM Demodulation (atan2 phase difference)                │
│  Narrowband signal at 160 kHz — clean FM demod           │
│  Output: scaled phase delta (×0.3)                       │
└──────────────────────────────────────────────────────────┘
       │
       ▼
┌──────────────────────────────────────────────────────────┐
│  Real-valued Decimation                                   │
│  Stage 2: 160 kHz → 53.3 kHz (÷3, 17-tap FIR)         │
│  Resample: 53.3 kHz → 48 kHz (linear interpolation)     │
│  Final: 15 kHz bandwidth filter at 48 kHz                │
└──────────────────────────────────────────────────────────┘
       │
       ▼
  Audio Output (48 kHz, 16-bit stereo, 10ms chunks)
```

### FM Transmitter Audio Pipeline

```
┌─────────────────────────────────────────────────────────────────┐
│  Audio Sources                                                   │
│                                                                  │
│  Microphone ──► PortAudioInput (mono 44.1kHz)                   │
│                    │                                             │
│                    ▼                                             │
│              Mono→Stereo (L=R) ──► Ring Buffer (1M float SPSC)  │
│                                          ▲                      │
│  Audio File ──► FFmpeg Decode ──► Stereo 44.1kHz ───┘           │
│  (MP3/WAV/      (swresample)      (L,R interleaved)            │
│   FLAC/OGG)                                                     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  FM Stereo MPX Generator (StereoMPXGenerator)                   │
│                                                                  │
│  Input: Stereo L,R @ 44.1kHz                                    │
│                                                                  │
│  ┌──────────┐   ┌──────────────────────────────────────┐        │
│  │ L channel├──►│ 75µs Pre-emphasis ──► L'             │        │
│  │ R channel├──►│ 75µs Pre-emphasis ──► R'             │        │
│  └──────────┘   └──────────────────────────────────────┘        │
│                           │                                      │
│                           ▼                                      │
│  ┌──────────────────────────────────────────────────────┐       │
│  │  Linear Interpolation: 44.1kHz → TX Sample Rate     │       │
│  │  (e.g. 2 MHz — direct upsample, no resampler)       │       │
│  └──────────────────────────────────────────────────────┘       │
│                           │                                      │
│                           ▼                                      │
│  MPX Composite @ TX Rate:                                        │
│    0.45×(L'+R') + 0.075×sin(19kHz) + 0.45×(L'-R')×sin(38kHz)  │
│    ──────────    ────────────────    ──────────────────────────  │
│    Mono (L+R)    Pilot Tone         Stereo Subcarrier (DSB-SC)  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  FM Modulator (FrequencyModulator)                               │
│                                                                  │
│  MPX baseband → Phase modulation → IQ output                    │
│  Persistent phase state across TX callbacks                      │
│  Pre-emphasis disabled (handled by MPX generator)                │
│                                                                  │
│  Output: Complex IQ @ TX Sample Rate → HackRF USB Transfer      │
└─────────────────────────────────────────────────────────────────┘
```

### Ring Buffer Design

The audio pipeline uses a lock-free single-producer single-consumer (SPSC) ring buffer (1M float samples ~ 11 seconds stereo @ 44.1kHz) shared between the audio source thread and the HackRF TX callback:

- **Producer** (audio thread): PortAudioInput callback or AudioFileInput decode thread writes stereo interleaved float samples via `ringWrite()`
- **Consumer** (TX callback): `apply_fm_modulation()` reads stereo samples via `ringRead()`, feeds to MPX generator
- **Back-pressure**: File decode blocks when ring buffer is full (`ringFree() < 256`), naturally pacing decode speed to TX consumption rate
- **No timing dependency**: TX hardware callback is the master clock — no `sleep_for` pacing needed

## Project Structure

```
HackTvRxTx/
├── HackTvLib/             # Shared library (DLL/dylib) - core SDR engine
│   ├── hacktvlib.cpp/h    # Main library interface
│   ├── hackrfdevice.cpp/h # HackRF One device driver + ring buffer + FM TX
│   ├── rtlsdrdevice.cpp/h # RTL-SDR device driver
│   ├── audioinput.h       # Microphone input (PortAudio → ring buffer)
│   ├── audiofileinput.h   # Audio file input (FFmpeg → ring buffer)
│   ├── modulation.h       # StereoMPXGenerator, FrequencyModulator, RationalResampler
│   ├── stream_tx.h        # Legacy double-buffer (used by video TX mode)
│   └── hacktv/            # Video encoding, modulation, RF output
├── HackTvGui/             # Main SDR transceiver GUI (USB + TCP)
│   ├── mainwindow.cpp/h   # Main window — USB + TCP client, mode-aware UI
│   ├── glplotter.cpp/h    # OpenGL spectrum analyzer & waterfall
│   ├── freqctrl.cpp/h     # Frequency digit display widget
│   ├── meter.cpp/h        # Signal level meter widget
│   ├── audiooutput.cpp/h  # Audio playback engine
│   ├── fmdemodulator.cpp/h # FM demodulator with stereo PLL decode
│   ├── amdemodulator.cpp/h # AM envelope demodulator
│   └── constants.h        # FFT, frequency macros
├── Emulator/              # TCP emulators (no hardware needed)
│   ├── hackrf_emulator.py # HackRF TCP emulator (3-port, stereo WFM/NFM/AM)
│   ├── rtlsdr_emulator.py # RTL-SDR TCP emulator (rtl_tcp protocol)
│   └── FaithlessInsomnia.wav # Sample audio for broadcast simulation
├── PALBDecoder/           # Standalone PAL-B/G TV decoder application
│   ├── MainWindow.cpp/h   # Decoder GUI with video display & HackRF control
│   ├── PALDecoder.cpp/h   # PAL video decoding engine (sync, AGC, color)
│   ├── audiodemodulator.*  # FM audio demodulator (dynamic decimation chain)
│   ├── audiooutput.cpp/h  # Audio playback engine (48 kHz, FFmpeg backend)
│   └── FrameBuffer.h      # IQ frame accumulator (40ms PAL frames)
├── PALBDecoderIOS/        # iOS/macOS mobile TV decoder (connects via WiFi)
├── HackRfTcp/             # HackRF TCP IQ Server (headless, runs on Raspberry Pi)
│   ├── main.cpp           # Server entry point
│   └── sdrdevice.cpp/h    # TCP streaming IQ server (data port + control port)
├── HackRfRadio/           # TCP Remote SDR Radio Client (mobile-friendly)
│   ├── radiowindow.cpp/h  # Main radio GUI (spectrum, meter, PTT, controls)
│   ├── fmdemodulator.cpp/h # FM demodulator with stereo decode
│   ├── amdemodulator.cpp/h # AM envelope demodulator
│   ├── audioplayback.cpp/h # Stereo audio output (dedicated writer thread)
│   ├── audiocapture.cpp/h # Microphone capture for PTT transmit
│   ├── tcpclient.cpp/h    # TCP client (IQ data + control + audio channels)
│   ├── frequencywidget.cpp/h # Touch-friendly frequency digit display
│   ├── gainsettingsdialog.* # Gain & TX parameters dialog
│   ├── glplotter.cpp/h    # OpenGL spectrum analyzer
│   ├── meter.cpp/h        # Signal level meter
│   └── constants.h        # FFT, frequency macros, gain limits
├── include/               # Shared headers
└── lib/                   # Pre-built libraries (windows/macos/linux)
```

## Supported Hardware

| Device | RX | TX | Notes |
|--------|----|----|-------|
| HackRF One | Yes | Yes | Full duplex, 1 MHz - 6 GHz |
| RTL-SDR | Yes | No | RX only, various tuner chips |
| HackRF TCP Emulator | Yes | No | Software emulator, no hardware needed |
| RTL-SDR TCP Emulator | Yes | No | Software emulator, rtl_tcp protocol |

## Requirements

- Qt 6.x (6.5+ recommended, tested up to 6.10)
- C++17 compiler
- MinGW 64-bit (Windows) or Clang/GCC (macOS/Linux)
- MSYS2 (Windows only, for dependencies)
- Python 3 + numpy (for TCP emulators only)

## Quick Start (Emulator — No Hardware)

```bash
# 1. Install numpy
pip install numpy

# 2. Start the HackRF TCP emulator
python Emulator/hackrf_emulator.py

# 3. In HackTvGui: Device → "HackRF TCP", Addr → "127.0.0.1", click START
# 4. Tune to 100.000 MHz → hear stereo FM music
# 5. Tune to 145.000 MHz → hear NFM voice
# 6. Tune to 119.100 MHz, switch to AM → hear AM airband voice
```

For RTL-SDR TCP emulator:
```bash
python Emulator/rtlsdr_emulator.py --port 1234
# In HackTvGui: Device → "RTL-SDR TCP", Addr → "127.0.0.1:1234", click START
```

For HackRfRadio (TCP-only client):
```bash
python Emulator/hackrf_emulator.py
# In HackRfRadio: enter 127.0.0.1, click Connect
# Tap mode button to cycle NFM → WFM → AM
```

## Build Instructions

### Windows

**1. Install MSYS2** from https://www.msys2.org/

**2. Install dependencies** in MSYS2 MINGW64 terminal:

```bash
pacman -Syu
pacman -S --needed \
  mingw-w64-x86_64-gcc \
  mingw-w64-x86_64-gcc-libs \
  mingw-w64-x86_64-ffmpeg \
  mingw-w64-x86_64-libusb \
  mingw-w64-x86_64-hackrf \
  mingw-w64-x86_64-rtl-sdr \
  mingw-w64-x86_64-fdk-aac \
  mingw-w64-x86_64-fftw \
  mingw-w64-x86_64-portaudio \
  mingw-w64-x86_64-opus \
  mingw-w64-x86_64-svt-av1 \
  mingw-w64-x86_64-x264 \
  mingw-w64-x86_64-x265 \
  mingw-w64-x86_64-zlib \
  mingw-w64-x86_64-bzip2 \
  mingw-w64-x86_64-xz
```

**3. Add MSYS2 to PATH:**

Add `C:\msys64\mingw64\bin` to your system PATH environment variable.

**4. Build HackTvLib first:**

Open `HackTvLib/HackTvLib.pro` in Qt Creator with Desktop Qt 6.x MinGW 64-bit kit, build. The library files are automatically copied to the `lib/` folder.

**5. Build HackTvGui:**

Open `HackTvGui/HackTvGui.pro` in Qt Creator, build and run. The post-build step automatically copies all required DLLs (Qt via `windeployqt`, MSYS2 via `xcopy *.dll`) into the `release/` folder.

**6. Build HackRfRadio (optional):**

Open `HackRfRadio/HackRfRadio.pro` in Qt Creator, build and run.

**7. Build PALBDecoder (optional):**

Open `PALBDecoder/PALBDecoder.pro` in Qt Creator, build and run.

### macOS

Install dependencies via Homebrew:

```bash
brew install hackrf libusb ffmpeg fftw portaudio opus x264 x265
```

Open the `.pro` files in Qt Creator with Desktop Qt 6.x kit and build. Qt 6.x desktop kit works out of the box.

### Linux

```bash
sudo apt install libhackrf-dev libusb-1.0-0-dev librtlsdr-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswresample-dev \
  libfftw3-dev libportaudio-ocaml-dev libopus-dev
```

### Raspberry Pi (Debian Bookworm, aarch64)

Raspberry Pi OS minimal images don't include `-dev` packages. All dependencies must be built from source.

**1. Install base build tools:**

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install build-essential cmake autoconf automake libtool pkg-config git qt6-base-dev qt6-multimedia-dev
```

**2. Build libusb (header + pkg-config):**

```bash
cd /tmp
git clone https://github.com/libusb/libusb.git
cd libusb
./autogen.sh && ./configure && make
sudo make install && sudo ldconfig
```

**3. Build libhackrf:**

```bash
cd /tmp
git clone https://github.com/greatscottgadgets/hackrf.git
cd hackrf && git checkout v2024.02.1
cd host && mkdir build && cd build
cmake ../libhackrf
make && sudo make install && sudo ldconfig
```

**4. Build PortAudio:**

```bash
cd /tmp
git clone https://github.com/PortAudio/portaudio.git
cd portaudio && cmake -B build && cmake --build build
sudo cmake --install build && sudo ldconfig
```

**5. Build FFTW3 (float):**

```bash
cd /tmp
wget http://www.fftw.org/fftw-3.3.10.tar.gz
tar xzf fftw-3.3.10.tar.gz && cd fftw-3.3.10
./configure --enable-float --enable-shared --prefix=/usr/local
make -j4 && sudo make install && sudo ldconfig
```

**6. Build Opus:**

```bash
cd /tmp
git clone https://github.com/xiph/opus.git
cd opus && mkdir build && cd build
cmake .. -DBUILD_SHARED_LIBS=ON
make -j4 && sudo make install && sudo ldconfig
```

**7. Build FDK-AAC:**

```bash
cd /tmp
git clone https://github.com/mstorsjo/fdk-aac.git
cd fdk-aac && mkdir build && cd build
cmake .. -DBUILD_SHARED_LIBS=ON
make -j4 && sudo make install && sudo ldconfig
```

**8. Build RTL-SDR:**

```bash
cd /tmp
git clone https://github.com/osmocom/rtl-sdr.git
cd rtl-sdr && mkdir build && cd build
cmake .. && make && sudo make install && sudo ldconfig
```

**9. Install FFmpeg dev (if not available via apt):**

```bash
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev libavdevice-dev libavfilter-dev
```

If apt packages are unavailable, build from source:

```bash
cd /tmp
git clone https://git.ffmpeg.org/ffmpeg.git
cd ffmpeg
./configure --enable-shared --disable-static --prefix=/usr/local
make -j4 && sudo make install && sudo ldconfig
```

**10. Build HackTvLib:**

```bash
cd HackTvLib
qmake6 HackTvLib.pro
make
```

**11. Build HackRfTcp:**

```bash
cd HackRfTcp
qmake6 HackRfTcp.pro
make
```

**12. Run:**

```bash
export LD_LIBRARY_PATH=$(pwd)/../lib/linux:$LD_LIBRARY_PATH
./HackRfTcp
```

**13. Install as systemd service (optional):**

```bash
sudo tee /etc/systemd/system/hackrftcp.service << 'EOF'
[Unit]
Description=HackRF TCP IQ Server
After=network.target

[Service]
ExecStart=/home/pi/HackRf/HackRfTcp/HackRfTcp
WorkingDirectory=/home/pi/HackRf/HackRfTcp
Environment=LD_LIBRARY_PATH=/home/pi/HackRf/lib/linux
User=root
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable hackrftcp.service
sudo systemctl start hackrftcp.service
```

Monitor the service:

```bash
sudo journalctl -u hackrftcp.service -f
```

> **Note:** HackRF One draws significant USB current. Use a powered USB hub or a 5V/5A power supply to avoid undervoltage issues on the Raspberry Pi.

## Supported TV Modes (TX)

| Mode | Lines | FPS | Audio |
|------|-------|-----|-------|
| PAL-I | 625 | 25 | 6.0 MHz FM |
| PAL-B/G | 625 | 25 | 5.5 MHz FM |
| PAL-D/K | 625 | 25 | 6.5 MHz FM |
| PAL-FM | 625 | 25 | 6.5 MHz FM |
| PAL-N | 625 | 25 | 4.5 MHz AM |
| PAL-M | 525 | 30 | 4.5 MHz FM |
| SECAM-L | 625 | 25 | 6.5 MHz AM |
| SECAM-D/K | 625 | 25 | 6.5 MHz FM |
| NTSC-M | 525 | 29.97 | 4.5 MHz FM |

## FM Transmitter Modes

| Mode | Input | Stereo | Description |
|------|-------|--------|-------------|
| FM Transmitter Mic | Microphone | Yes (dual mono) | Real-time voice/audio transmission via default audio input device |
| FM Transmitter File | Audio/Video file | Yes (true stereo) | File playback transmission — FFmpeg decodes any format, stereo channels preserved in MPX |

Both modes generate a standard FM stereo MPX composite signal with 19 kHz pilot tone, enabling any FM stereo receiver to decode separate left and right channels.

## License

Based on [hacktv](https://github.com/fsphil/hacktv) by Philip Heron. See individual source files for license details.
