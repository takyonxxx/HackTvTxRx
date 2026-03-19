# HackTvRxTx - SDR Transceiver & Analog TV Decoder

A Qt 6.x based SDR (Software Defined Radio) application for HackRF One and RTL-SDR devices. Includes wideband FM receiver with real-time FFT spectrum analyzer, waterfall display, analog TV PAL B/G transmitter, and a standalone PAL-B/G TV decoder.

Based on [fsphil/hacktv](https://github.com/fsphil/hacktv) with significant modifications for cross-platform GUI operation.

![HackTvGui Screenshot](hacktvgui_screen.png)

## Features

### HackTvGui - SDR Transceiver
- **RX Mode**: Wideband FM receiver with real-time audio demodulation
- **TX Mode**: Analog TV transmitter supporting PAL-I, PAL-B/G, PAL-D/K, PAL-M, PAL-N, SECAM-L, SECAM-D/K, NTSC-M, NTSC-A and more
- **FM Transmitter**: Microphone input FM transmitter mode
- **OpenGL Spectrum Analyzer**: GPU-accelerated FFT spectrum display with smooth antialiased rendering
- **Waterfall Display**: Real-time scrolling waterfall with SDR#-style color palette (dark blue → cyan → yellow → red)
- **Band Overlay**: Known frequency band allocations displayed on spectrum (FM Broadcast, Ham Radio, Aviation, Marine VHF, TV bands, Cellular, ISM/WiFi, etc.)
- **Frequency Control**: Click-to-tune on spectrum, mouse wheel tuning with digit-proportional step size, draggable filter bandwidth
- **Adjustable Gains**: LNA, VGA, TX Amp, RX Amp with real-time control
- **Multiple Sample Rates**: 2, 4, 8, 10, 12.5, 16, 20 MHz
- **European TV Channels**: Pre-configured E2-E69 channel list
- **Video Input Sources**: File (MP4/FLV), test pattern, RTSP stream via FFmpeg
- **Cross-platform**: Windows (MinGW 64-bit) and macOS

### PALBDecoder - Analog TV Receiver
- **PAL-B/G Video Decoder**: Real-time analog TV reception and decoding at 576x384 resolution
- **UHF TV Band**: Channel selection from E21 (470 MHz) to E69 (862 MHz)
- **Video Processing**: Adjustable gain, offset, chroma gain, video inversion, color/monochrome mode
- **Sync Detection**: Real-time sync percentage display with adjustable threshold
- **Audio Demodulation**: FM audio carrier demodulation with adjustable gain and volume
- **HackRF Integration**: Direct HackRF One control with LNA, VGA, and RX Amp gain settings

![PALBDecoder Screenshot](paldecoder.jpg)

## Project Structure

```
HackTvRxTx/
├── HackTvLib/          # Shared library (DLL/dylib) - core SDR engine
│   ├── hacktvlib.cpp   # Main library interface
│   ├── hackrfdevice.*  # HackRF One device driver
│   ├── rtlsdrdevice.*  # RTL-SDR device driver
│   └── hacktv/         # Video encoding, modulation, RF output
├── HackTvGui/          # Main SDR transceiver GUI application
│   ├── mainwindow.*    # Main application window
│   ├── glplotter.*     # OpenGL spectrum analyzer & waterfall
│   ├── freqctrl.*      # Frequency digit display widget
│   ├── meter.*         # Signal level meter widget
│   ├── audiooutput.*   # Audio playback engine
│   └── modulator.h     # FM/AM modulation DSP
├── PALBDecoder/        # Standalone PAL-B/G TV decoder application
│   ├── MainWindow.*    # Decoder GUI with video display
│   ├── PALDecoder.*    # PAL video decoding engine
│   └── audiodemodulator.* # FM audio demodulator
├── include/            # Shared headers
└── lib/                # Pre-built libraries (windows/macos/linux)
```

## Supported Hardware

| Device | RX | TX | Notes |
|--------|----|----|-------|
| HackRF One | Yes | Yes | Full duplex, 1 MHz - 6 GHz |
| RTL-SDR | Yes | No | RX only, various tuner chips |

## Requirements

- Qt 6.x (6.5+ recommended, tested up to 6.10)
- C++17 compiler
- MinGW 64-bit (Windows) or Clang/GCC (macOS/Linux)
- MSYS2 (Windows only, for dependencies)

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

Open `HackTvGui/HackTvGui.pro` in Qt Creator, build and run.

**6. Build PALBDecoder (optional):**

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

## License

Based on [hacktv](https://github.com/fsphil/hacktv) by Philip Heron. See individual source files for license details.
