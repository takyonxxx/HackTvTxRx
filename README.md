# HackTvTxRx — HackTvStudio

Analogue radio & TV transceiver for the **HackRF One**, built with Qt 6.
One application, four modes: FM/AM radio receiver, FM/AM radio transmitter,
PAL-B/G television receiver and analogue television transmitter (PAL, SECAM,
NTSC, MAC…). Everything is compiled into the executable — no MSYS2, no FFmpeg
DLLs, no external SDR libraries.

![HackTvStudio](hacktvstudio.jpg)

Based on [hacktv](https://github.com/fsphil/hacktv) by Philip Heron (TV encoder)
and the spectrum / frequency widgets of [gqrx](https://github.com/gqrx-sdr/gqrx).

---

## What is in this repository

| Folder | What it is |
|--------|------------|
| [`HackTvStudio/`](HackTvStudio/) | **The main application.** Radio RX, Radio TX, TV RX, TV TX in one window. Self-contained: libusb, libhackrf and hacktv are vendored and built in. |
| [`PALBDecoder/`](PALBDecoder/) | The original standalone PAL-B/G receiver that HackTvStudio grew out of. Kept as a small, single-purpose reference app (also self-contained). |
| [`PALBDecoderIOS/`](PALBDecoderIOS/) | Legacy iOS/macOS port of the PAL decoder (Xcode project). It expects an IQ-over-TCP server that is no longer part of this repository. |

---

## HackTvStudio

### The window

A persistent top strip and spectrum, with one tab per mode underneath:

- **Frequency dial** 0 – 6 GHz (click a digit, scroll, or click/drag on the spectrum).
- **Band plan readout** — the tuned band and its usual service, e.g.
  `FM Broadcast · Wide FM (Stereo)` or `UHF TV Band IV/V · Channel E22 (video 479.250 MHz)`.
  With **Auto mode** on, tuning into a TV band switches to the TV receiver, tuning into a
  broadcast / amateur / airband frequency switches to the radio receiver and picks WFM / NFM / AM.
- **Channel power** meter — power inside the tuned channel (filter box or TV channel), plus an
  **ADC OVERLOAD** warning when the HackRF front end clips.
- **HackRF One** group — START/STOP, sample rate, LNA / VGA / RX amp, TX VGA, RF amp, 0.5 s raw IQ capture.
- **Spectrum + waterfall** (OpenGL). In RX modes it shows the received band; in TX modes it shows
  the baseband being transmitted (TX monitor). The draggable filter box sets the receiver bandwidth.

### RADIO RX

- WFM (broadcast FM with stereo pilot decoding), NFM, AM.
- Bandwidth from the filter box or the slider; squelch with hysteresis; channel level readout.
- De-emphasis 50/75 µs, modulation index, IF noise reduction (NFM), force mono (WFM), STEREO indicator.
- "Follow band plan" picks the demodulator from the tuned band automatically.

### RADIO TX

- WFM (stereo MPX: 19 kHz pilot, 38 kHz DSB-SC, pre-emphasis), NFM, AM.
- Sources: **microphone** with push-to-talk, **audio file** (mp3 / wav / flac / ogg… decoded by Qt),
  **test tone**. Audio gain with optional AGC, input VU meter.
- Deviation (FM) / depth (AM), baseband level, TX VGA gain.

### TV RX (PAL-B/G)

- Real-time PAL-B/G colour decoding at 720×576 from 16 MS/s: synchronous detection, carrier AFC,
  flywheel sync with sub-sample timing, burst-locked chroma with per-line V-switch detection,
  temporal noise reduction, 16:9 / 4:3 display, fullscreen.
- FM sound (5.5 MHz) demodulated alongside the picture.
- **Record to MP4** (H.264 + AAC through the platform encoders) — picture and sound, in sync.
- VHF Band I / III and UHF Band IV/V channel tables (E2–E69) with automatic video-carrier offsets.

![PALBDecoder](paldecoder.jpg)

### TV TX

- All 52 hacktv standards: PAL B/G/I/D/K/M/N, SECAM L/D/K/B/G, NTSC M/I/A, D/D2-MAC,
  405/819/240/30-line systems, Apollo, CBS field-sequential…
- Sources: hacktv's colour-bar **test pattern** or a **video file** (mp4 / mkv / avi…, decoded by
  Qt Multimedia, looped) with a live preview of what is being sent.
- Options: VSB output filter, colour, audio, A2 stereo, NICAM, ACP, VITS, VITC, WSS, teletext
  pages folder, Videocrypt I/II/S and Syster scrambling, output level.

### Supported hardware

HackRF One only (1 MHz – 6 GHz, half-duplex: one mode at a time).
On Windows the HackRF must be bound to the **WinUSB** or **libusbK** driver (use
[Zadig](https://zadig.akeo.ie/)); on Linux install hacktv's `53-hackrf.rules` udev rule.

---

## Architecture

```
RX:  USB callback ──► lock-free ring ──► RxEngine thread ──► mode sink (own worker threads)
                                              │                 TV: PAL decoder, audio demod
                                              └──► FFT 25 Hz ──► spectrum / waterfall / meter
TX:  mic / file / tone ──► RadioModulator ─┐
     QMediaPlayer ──► QtAvSource ──► hacktv encoder ─┴──► TxEngine thread ──► ring ──► USB callback (memcpy)
```

- The libusb/libhackrf USB callback never does DSP (RX: write to ring, TX: copy from ring).
- The GUI thread never does DSP; it receives spectra and frames through queued signals.
- Each mode is a `ModePanel`; `StudioWindow` owns the device, the two engines and the display.
- The PAL decoder runs block-wise with SSE2 helpers (≈30 ms per 40 ms frame at 16 MS/s, bit-identical
  to the original per-sample implementation); the offline benchmark is in `HackTvStudio/tools/palbench/`.

### Layout of `HackTvStudio/`

```
core/       DeviceController (HackRF owner), RxEngine, TxEngine, SpscRing, Fft, BandPlan, SimdDot
ui/         StudioWindow, RadioRxPanel, RadioTxPanel, TvRxPanel, TvTxPanel, TvRecorder, VideoView
dsp/        fmdemodulator, amdemodulator (RX), RadioModulator (TX)
tv/         QtAvSource (video file -> hacktv), TvTxEncoder (hacktv wrapper)
hacktv/     fsphil's hacktv encoder sources (C), without the FFmpeg / RF backends
hackrf/     HackRfDevice wrapper around libhackrf
widgets/    CPlotter (spectrum/waterfall), CFreqCtrl, CMeter (from gqrx)
3rdparty/   libusb 1.0.27, libhackrf, MSVC compatibility shims
PALDecoder.cpp/.h, audiodemodulator.cpp/.h, audiooutput.cpp/.h   PAL-B/G receiver DSP
```

---

## Building

Requirements: **Qt 6.8+** (tested with 6.11.1) with the Multimedia and OpenGL Widgets modules,
a C++17 compiler. Nothing else — libusb, libhackrf and hacktv are part of the source tree.

### Windows

Open `HackTvStudio/HackTvStudio.pro` in Qt Creator with either kit and build **Release**:

- **MSVC 2022 64-bit** — recommended; MP4 recording uses the Windows Media Foundation encoders.
- **MinGW 64-bit** — also supported.

Both kits build without MSYS2. The only runtime dependencies are the Qt DLLs (`windeployqt`).

### macOS / Linux

```bash
cd HackTvStudio
qmake HackTvStudio.pro CONFIG+=release
make -j
```

libusb's IOKit (macOS) and usbfs/netlink (Linux) backends are compiled in; the only system
dependencies are the OS frameworks (macOS: IOKit, CoreFoundation, Security) and pthread (Linux).
These targets build from the same sources but have not been exercised as much as Windows.

### PALBDecoder

Same procedure with `PALBDecoder/PALBDecoder.pro` (Windows kits only).

---

## Using it

1. Plug in the HackRF, start HackTvStudio.
2. Type a frequency into the dial. With *Auto mode* on, the right tab and demodulator are chosen
   for you: `100.000 MHz` → Radio RX / WFM, `145.500 MHz` → NFM, `119.100 MHz` → AM,
   `479.250 MHz` → TV RX.
3. Press **START**. Set LNA/VGA so that *ADC OVERLOAD* stays off.
4. To transmit, open RADIO TX or TV TX, choose a source and press START. Transmitting needs a
   licence for the band you use; start with a low TX VGA value and a dummy load.

Settings (frequency, gains, every panel's controls) are saved on exit and restored.

---

## Licenses

- Application code: GPL-3.0 (it links hacktv and gqrx code).
- `HackTvStudio/hacktv/` — hacktv, © Philip Heron, GPL-3.0.
- `HackTvStudio/widgets/` — gqrx / CuteSDR, GPL-3.0.
- `HackTvStudio/3rdparty/libusb/` — LGPL-2.1 (sources included, relinking possible).
- `HackTvStudio/3rdparty/libhackrf/` — BSD-3, © Great Scott Gadgets.

See [LICENSE](LICENSE) and the headers of the individual files.
