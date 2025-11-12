# HackRF Receiver iOS - Quick Start Guide

## Project Overview

This is a complete iOS Xcode project that connects to your HackRF TCP server, receives IQ samples, and processes them for either:
1. **FM Radio** - Demodulates FM signals and plays audio through iPhone speaker
2. **PAL-B/G TV** - Decodes analog TV signals and displays video

## Files Included

### Source Files
- `HackRFReceiverApp.swift` - App entry point
- `ContentView.swift` - Main user interface
- `HackRFReceiver.swift` - Main coordinator class
- `TCPClient.swift` - Network communication
- `FMDemodulator.swift` - FM signal processing
- `AudioPlayer.swift` - iOS audio output
- `PALDecoder.swift` - PAL TV decoder
- `TVDisplayView.swift` - TV display component
- `Info.plist` - App permissions and settings

### Project Files
- `HackRFReceiver.xcodeproj/` - Xcode project configuration
- `Assets.xcassets/` - App icon and assets
- `README.md` - Full documentation (English)
- `KURULUM_TR.md` - Turkish setup guide

## How to Use

### Step 1: Open in Xcode
1. Double-click `HackRFReceiver.xcodeproj`
2. Xcode will open the project

### Step 2: Configure Signing
1. Select the project in Xcode's navigator
2. Select the "HackRFReceiver" target
3. Go to "Signing & Capabilities" tab
4. Select your Team in the "Team" dropdown
5. Xcode will automatically generate a bundle identifier

### Step 3: Build and Run
1. Connect your iPhone/iPad via USB, or select a simulator
2. Select your device in Xcode's toolbar
3. Press ⌘R (or click the Play button) to build and run

### Step 4: Configure the App
1. Enter your HackRF server IP address (e.g., `192.168.1.2`)
2. Enter the port number (e.g., `5000`)
3. Choose FM or TV mode
4. Set the frequency
5. Tap "Connect"

## Important Notes

### Server Requirements
Your Python server (the one you provided) should be running and:
- Listening on the specified port (e.g., 5000)
- Sending raw IQ samples as int8 pairs
- Using 2 MHz sample rate for FM mode
- Using 16 MHz sample rate for TV mode

### Network Configuration
- The app requires local network access
- Both iPhone and HackRF server must be on the same network
- iOS will ask for local network permission on first use

### Performance Tips
- For FM: Sample rate should be 2 MHz on server
- For TV: Sample rate must be 16 MHz on server
- Keep your iPhone connected to power during long sessions
- Audio latency is normal (~0.5-1 second)

## Compatibility

- **Minimum iOS**: 16.0
- **Xcode**: 15.0 or later
- **Language**: Swift 5.0
- **UI Framework**: SwiftUI
- **Devices**: iPhone, iPad

## Troubleshooting

### Build Errors
- If you get signing errors, make sure you selected a development team
- If modules are missing, clean build folder (Shift+⌘K) and rebuild

### Runtime Issues
- **No connection**: Check IP address and port
- **No audio**: Check iPhone volume and mute switch
- **No video**: Verify 16 MHz sample rate on server
- Check Xcode console for detailed logs

## What's Implemented

### FM Radio Mode ✓
- TCP connection to server
- IQ sample reception
- FM demodulation (phase differentiation)
- Audio decimation (2 MHz → 48 kHz)
- 75µs de-emphasis filter
- Real-time audio playback
- Frequency adjustment

### TV Mode ✓
- 16 MHz IQ sample reception
- AM demodulation (amplitude detection)
- Line and frame synchronization
- 625-line PAL-B/G standard
- 720x576 resolution output
- Grayscale (luminance) display
- Real-time video display

### Not Yet Implemented
- PAL color decoding (chrominance)
- RDS data decoding
- Spectrum display
- Recording functionality
- AM/SSB modes

## Code Architecture

```
HackRFReceiver (ObservableObject)
├── TCPClient - Network communication
├── FMDemodulator - Signal processing for radio
│   └── AudioPlayer - Speaker output
└── PALDecoder - Signal processing for TV
    └── TVDisplayView - Video display
```

## Customization

### Change Sample Rates
Edit in `HackRFReceiver.swift`:
```swift
// For FM
self.fmDemodulator = FMDemodulator(sampleRate: 2_000_000, audioRate: 48_000)

// For TV  
self.palDecoder = PALDecoder(sampleRate: 16_000_000)
```

### Change Buffer Size
Edit in `HackRFReceiver.swift`:
```swift
let bufferSize = 262144 // Adjust as needed
```

### Modify UI
All UI is in `ContentView.swift` using SwiftUI - easy to customize!

## Next Steps

1. **Open the project in Xcode**
2. **Configure your development team**
3. **Build and run on your device**
4. **Start your HackRF server**
5. **Connect and enjoy!**

For detailed information, see `README.md` (English) or `KURULUM_TR.md` (Turkish).

---

**Need Help?**
- Check Xcode console logs for errors
- Verify server is sending correct sample format
- Ensure network connectivity
- Review README.md for technical details
