# HackRF Receiver - iOS Application

iOS application for receiving and processing HackRF signals via TCP connection. Supports FM radio demodulation and PAL-B/G analog TV signal decoding.

## Features

- **TCP Connection**: Connect to HackRF server over network
- **FM Radio Receiver**: Demodulate FM broadcasts with de-emphasis filter and audio output
- **PAL-B/G TV Decoder**: Decode 625-line, 25fps analog TV signals (Turkish standard)
- **Real-time Processing**: Live audio and video output
- **Frequency Control**: Adjust frequency with +/- buttons
- **User-Friendly UI**: SwiftUI-based interface with configuration options

## Requirements

- iOS 16.0 or later
- Xcode 15.0 or later
- HackRF TCP server (like your Python server)
- Network connection to HackRF server

## Project Structure

```
HackRFReceiver/
├── HackRFReceiver.xcodeproj/
│   ├── project.pbxproj
│   ├── project.xcworkspace/
│   └── xcshareddata/
└── HackRFReceiver/
    ├── HackRFReceiverApp.swift      # App entry point
    ├── ContentView.swift             # Main UI
    ├── HackRFReceiver.swift          # Main coordinator
    ├── TCPClient.swift               # Network communication
    ├── FMDemodulator.swift           # FM demodulation
    ├── AudioPlayer.swift             # Audio output
    ├── PALDecoder.swift              # TV signal decoder
    ├── TVDisplayView.swift           # TV display view
    ├── Info.plist                    # App configuration
    └── Assets.xcassets/              # App assets
```

## Installation

1. Open `HackRFReceiver.xcodeproj` in Xcode
2. Select your development team in project settings
3. Connect your iOS device or select a simulator
4. Build and run (⌘R)

## Usage

### FM Radio Mode

1. Launch the app
2. Enter your HackRF server IP address (e.g., `192.168.1.2`)
3. Enter the data port (e.g., `5000`)
4. Set the frequency in MHz (e.g., `100.0` for 100 MHz)
5. Select "FM Radio" mode
6. Tap "Connect"
7. Use +/- buttons to adjust frequency
8. Audio will play through device speaker

**FM Mode Parameters:**
- Sample Rate: 2 MHz
- Audio Rate: 48 kHz
- De-emphasis: 75µs (broadcast standard)

### PAL-B/G TV Mode

1. Follow steps 1-3 from FM mode
2. Set the frequency for a TV channel (usually 47-862 MHz)
3. Select "PAL-B/G TV" mode
4. Tap "Connect"
5. Video will display in the TV Display area

**TV Mode Parameters:**
- Sample Rate: 16 MHz
- Standard: PAL-B/G
- Lines: 625 (576 active)
- Frame Rate: 25 fps
- Resolution: 720x576 (4:3 aspect ratio)

### Server Configuration

Your HackRF TCP server should:
- Listen on specified port for data connections
- Send continuous IQ samples as int8 pairs (I, Q, I, Q, ...)
- For FM: Use 2 MHz sample rate
- For TV: Use 16 MHz sample rate

Example server structure (matching your Python code):
```python
# Control port: 5001 (optional - for frequency changes)
# Data port: 5000 (required - for IQ samples)
```

## Technical Details

### FM Demodulation Algorithm

1. Convert IQ samples to complex numbers (I + jQ)
2. Calculate phase: `atan2(Q, I)`
3. Unwrap phase to handle discontinuities
4. Differentiate phase (FM demodulation)
5. Decimate to audio rate (48 kHz)
6. Apply 75µs de-emphasis filter
7. Normalize and output to speaker

### PAL-B/G Decoding Algorithm

1. Convert IQ to amplitude (AM demodulation)
2. Extract video lines (skip sync and blanking)
3. Detect frame sync (625 lines)
4. Resample to 720 pixels width
5. Convert to grayscale (luminance only)
6. Display as RGBA image

### Network Protocol

- Connection: TCP
- Data Format: Raw IQ samples (int8)
- Sample Order: I, Q, I, Q, I, Q...
- Endianness: Native (typically little-endian)

## Performance Optimization

- Background processing on dedicated queue
- Efficient buffer management
- Hardware-accelerated audio output (AVAudioEngine)
- Optimized signal processing algorithms

## Troubleshooting

### Connection Issues

- **Cannot connect**: Verify server IP and port
- **Connection timeout**: Check network firewall settings
- **Connection drops**: Ensure stable WiFi connection

### Audio Issues

- **No audio**: Check iOS volume and mute switch
- **Distorted audio**: Adjust server VGA/LNA gains
- **Audio lag**: Normal latency is ~0.5-1 second

### TV Issues

- **No video**: Verify 16 MHz sample rate on server
- **Rolling image**: Sync issues - check frequency accuracy
- **Grayscale only**: Color decoding not implemented (Y only)

## Network Security

The app uses:
- `NSAllowsArbitraryLoads = true` for local network access
- `NSLocalNetworkUsageDescription` for permission prompt
- Standard TCP sockets (no encryption)

**Note**: Only use on trusted local networks.

## Future Enhancements

Possible improvements:
- [ ] PAL color decoding (UV chrominance)
- [ ] Stereo FM decoding
- [ ] RDS (Radio Data System) decoding
- [ ] Recording functionality
- [ ] Spectrum analyzer display
- [ ] AM demodulation mode
- [ ] SSB/CW modes
- [ ] Save favorite frequencies

## Compatibility

### Supported TV Standards
- PAL-B/G (Turkey, most of Europe)
- 625 lines, 25 fps
- 50 Hz field rate

### Audio Standards
- Broadcast FM with 75µs de-emphasis (Worldwide)
- Mono audio output

## License

This is a demonstration project for educational purposes.

## Credits

Based on the Python HackRF FM receiver example. Adapted for iOS with native audio/video processing.

## Support

For issues or questions:
1. Verify HackRF server is running and accessible
2. Check iOS device network permissions
3. Review Xcode console logs for detailed error messages
4. Ensure correct sample rates for each mode

---

**Version**: 1.0  
**Platform**: iOS 16.0+  
**Language**: Swift 5.0  
**Framework**: SwiftUI, AVFoundation, Network
