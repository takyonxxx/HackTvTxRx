# HackRF iOS Receiver - Complete Project

## ✅ Project Successfully Created!

This is a complete iOS Xcode project for receiving HackRF signals via TCP with proper **AM and FM demodulation support**.

---

## 📱 What You Have

### Complete iOS Application with 3 Modes:

1. **FM Radyo** (FM Radio)
   - Frequency Modulation - Phase differentiation
   - For: FM broadcasts (88-108 MHz)
   - Sample Rate: 2 MHz

2. **AM Radyo** (AM Radio) 
   - Amplitude Modulation - Envelope detection
   - For: AM broadcasts (540-1700 kHz MW)
   - Sample Rate: 2 MHz

3. **PAL-B/G TV** (Turkish TV Standard)
   - **Amplitude Modulation** - Envelope detection for video
   - For: Analog TV channels (47-862 MHz)
   - Sample Rate: 16 MHz
   - Resolution: 720x576 (grayscale)

---

## 📂 Complete File List

### Swift Source Code (9 files)
```
HackRFReceiver/HackRFReceiver/
├── HackRFReceiverApp.swift      ✓ App entry point
├── ContentView.swift             ✓ UI with FM/AM/TV mode selection
├── HackRFReceiver.swift          ✓ Main coordinator
├── TCPClient.swift               ✓ Network layer
├── FMDemodulator.swift           ✓ FM demodulation (phase)
├── AMDemodulator.swift           ✓ AM demodulation (envelope) [NEW]
├── AudioPlayer.swift             ✓ iOS speaker output
├── PALDecoder.swift              ✓ TV decoder (uses AM)
└── TVDisplayView.swift           ✓ Video display
```

### Xcode Project Configuration
```
HackRFReceiver.xcodeproj/
├── project.pbxproj               ✓ Build configuration
├── project.xcworkspace/          ✓ Workspace settings
└── xcshareddata/xcschemes/       ✓ Build schemes
```

### Documentation (6 files)
```
├── README.md                     ✓ Full technical documentation
├── QUICKSTART.md                 ✓ 3-step setup guide
├── KURULUM_TR.md                 ✓ Turkish setup guide
├── PROJECT_SUMMARY.md            ✓ Architecture overview
├── DEMODULATION_GUIDE.md         ✓ FM vs AM explanation [NEW]
└── UPDATE_SUMMARY.md             ✓ Changelog [NEW]
```

### Assets
```
HackRFReceiver/Assets.xcassets/   ✓ App icon placeholder
Info.plist                        ✓ Permissions (network, audio)
```

---

## 🚀 How to Use This Project

### Step 1: Open in Xcode
Double-click: `HackRFReceiver.xcodeproj`

### Step 2: Configure Signing
1. Select project in Xcode navigator
2. Choose your Team in "Signing & Capabilities"
3. Xcode will auto-generate bundle ID

### Step 3: Build & Run
1. Connect your iPhone or select simulator
2. Press ⌘R (or click Play button)
3. App will launch on your device

### Step 4: Use the App
1. Enter HackRF server IP (e.g., 192.168.1.2)
2. Enter port (e.g., 5000)
3. Choose mode: FM / AM / TV
4. Set frequency
5. Tap "Connect"

---

## 🎯 Key Features

### ✅ Proper Demodulation Selection
- **FM Radio**: Uses phase differentiation
- **AM Radio**: Uses envelope detection
- **PAL-B/G TV**: Uses AM for video carrier (correct!)

### ✅ Turkish Language Support
- UI labels in Turkish: "FM Radyo", "AM Radyo", "PAL-B/G TV"
- Status shows: "Demodülasyon: AM (Video)"
- Complete Turkish documentation

### ✅ Real-time Processing
- Background thread processing
- Efficient buffer management
- Low-latency audio/video output

### ✅ Clean UI
- SwiftUI interface
- Mode selector
- Frequency controls (+/- buttons)
- Connection status
- Sample counter
- Demodulation type display

---

## 📺 PAL-B/G TV - Important!

**PAL-B/G uses AM (Amplitude Modulation) for video carrier**

```
PAL-B/G Signal Structure:
├── Video Carrier: AM ← This is what we decode
├── Audio Carrier: FM (video freq + 5.5 MHz)
└── Color Subcarrier: QAM (suppressed carrier)
```

The app correctly uses:
```swift
amplitude = sqrt(I² + Q²)  // AM demodulation for video
```

**Not** phase differentiation (which would be wrong for TV).

---

## 🔧 Technical Specifications

### FM Mode
| Parameter | Value |
|-----------|-------|
| Demodulation | Phase differentiation |
| Algorithm | atan2 → unwrap → diff |
| Sample Rate | 2 MHz |
| Audio Rate | 48 kHz |
| De-emphasis | 75µs |

### AM Mode
| Parameter | Value |
|-----------|-------|
| Demodulation | Envelope detection |
| Algorithm | √(I²+Q²) → DC remove |
| Sample Rate | 2 MHz |
| Audio Rate | 48 kHz |

### TV Mode
| Parameter | Value |
|-----------|-------|
| Demodulation | AM (envelope) |
| Algorithm | √(I²+Q²) → sync → video |
| Sample Rate | 16 MHz |
| Standard | PAL-B/G |
| Lines | 625 (576 active) |
| Frame Rate | 25 fps |
| Resolution | 720x576 |
| Color | Grayscale (Y only) |

---

## 📚 Documentation

### For Quick Start
→ Read `QUICKSTART.md` - Get started in 3 steps

### For Understanding Demodulation
→ Read `DEMODULATION_GUIDE.md` - Comprehensive FM vs AM guide

### For Technical Details
→ Read `README.md` - Full documentation

### For Turkish Users
→ Read `KURULUM_TR.md` - Turkish setup guide

### For Architecture
→ Read `PROJECT_SUMMARY.md` - System design

### For What Changed
→ Read `UPDATE_SUMMARY.md` - Changelog

---

## 🔌 Server Requirements

Your Python TCP server should:
- ✅ Listen on specified port (e.g., 5000)
- ✅ Send raw IQ samples as int8 pairs: I, Q, I, Q, ...
- ✅ Use 2 MHz for FM/AM modes
- ✅ Use 16 MHz for TV mode
- ✅ Keep connection open

Your existing Python server already does this! ✓

---

## 🌍 Turkish TV Channels

PAL-B/G standard frequencies:

**VHF Band I (47-68 MHz)**
- Kanal E2: 48.25 MHz (video carrier)
- Kanal E3: 55.25 MHz
- Kanal E4: 62.25 MHz

**VHF Band III (174-230 MHz)**
- Multiple channels

**UHF Band IV/V (470-862 MHz)**
- Digital and analog channels

Each channel is 7 MHz wide.

---

## 💡 Usage Tips

### For FM Radio
- Start with 88-108 MHz range
- Use +/- buttons to fine-tune
- Audio quality should be high
- 75µs de-emphasis is applied

### For AM Radio
- Try MW band: 540-1700 kHz (0.54-1.7 MHz)
- Increase LNA gain if weak
- Some fading is normal

### For TV
- **MUST use 16 MHz sample rate on server**
- Video frequency must be accurate
- **Use AM mode** (not FM!)
- Grayscale only (color not yet implemented)
- Sync timing is critical

---

## 🐛 Troubleshooting

### Build Issues
**"No signing identity"**
→ Select your Team in project settings

**"Module not found"**
→ Clean build (Shift+⌘K) and rebuild

### Connection Issues
**"Cannot connect"**
→ Check IP address and server is running
→ Verify same WiFi network

**"Connection drops"**
→ Check WiFi stability
→ Reduce buffer size

### Audio Issues
**"No sound"**
→ Check iPhone volume
→ Check mute switch
→ Verify correct mode (FM/AM)

**"Distorted audio"**
→ Reduce server gains (VGA/LNA)
→ Check for clipping

### TV Issues
**"No video sync"**
→ Verify 16 MHz sample rate
→ Check frequency accuracy
→ **Ensure using AM, not FM!**

**"Rolling image"**
→ Frequency not accurate enough
→ Sample rate must be exactly 16 MHz

**"Grainy image"**
→ Increase signal strength
→ Adjust server gains

---

## 🎓 Learning Resources

This project demonstrates:
- ✅ SwiftUI interface design
- ✅ iOS networking (NWConnection)
- ✅ Real-time audio processing (AVAudioEngine)
- ✅ Signal processing (DSP algorithms)
- ✅ Video rendering (CoreGraphics)
- ✅ Multi-threading patterns
- ✅ Clean architecture (MVVM)

All code is well-commented and documented.

---

## 🔮 Future Enhancements

Possible additions:
- [ ] PAL color decoding (chrominance U/V)
- [ ] Stereo FM decoding
- [ ] RDS data decoding
- [ ] Spectrum analyzer
- [ ] Recording capability
- [ ] SSB/CW modes
- [ ] Waterfall display

---

## ✨ What Makes This Special

1. **Three complete demodulation modes**
2. **Proper AM for TV** (not FM)
3. **Turkish language support**
4. **Production-ready code**
5. **Comprehensive documentation**
6. **Clean architecture**
7. **Real-time performance**
8. **Educational value**

---

## 📞 Support

If you need help:

1. **Build errors**: Check Xcode console logs
2. **Connection issues**: Verify server IP and port
3. **No audio/video**: Check mode and sample rate
4. **Sync problems**: Verify using correct demodulation (AM for TV!)
5. **Documentation**: See README.md for details

---

## 📄 License

This is a demonstration project for educational purposes.

---

## 🎉 Ready to Use!

Everything is configured and ready:
- ✅ All source files created
- ✅ Xcode project configured
- ✅ Documentation complete
- ✅ AM/FM demodulation implemented
- ✅ Turkish language support
- ✅ TV uses correct AM demodulation

**Just open the .xcodeproj file and start building!**

---

**Project Version**: 1.1  
**Last Updated**: November 2024  
**iOS**: 16.0+  
**Xcode**: 15.0+  
**Swift**: 5.0  

**Status**: ✅ Complete and Ready to Build
