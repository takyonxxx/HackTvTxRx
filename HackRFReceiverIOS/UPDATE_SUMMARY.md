# Update Summary - AM/FM Demodulation Support

## What Changed

I've updated the iOS HackRF Receiver project to properly support **both FM and AM demodulation**, with clear selection in the UI.

### Key Changes

#### 1. New Files Added
- **AMDemodulator.swift** - Complete AM demodulation implementation using envelope detection (√(I²+Q²))

#### 2. UI Updates (ContentView.swift)
- Changed mode enum from 2 modes to 3 modes:
  - ✅ **FM Radyo** (FM Radio)
  - ✅ **AM Radyo** (AM Radio) - NEW
  - ✅ **PAL-B/G TV** (uses AM for video)
- Added demodulation type display showing "FM (Faz)" or "AM (Genlik)" or "AM (Video)"
- Turkish language labels for better clarity

#### 3. Core Logic Updates (HackRFReceiver.swift)
- Added `amDemodulator` property
- Updated `connect()` to initialize correct demodulator based on mode
- Added `processAMData()` function for AM signal processing
- Switch statement to route data to correct processor

#### 4. Documentation Updates
- **README.md**: Added AM mode section, clarified TV uses AM
- **KURULUM_TR.md**: Turkish guide updated with AM mode
- **DEMODULATION_GUIDE.md**: NEW comprehensive guide explaining FM vs AM
- All docs now clearly state: **PAL-B/G TV uses AM demodulation for video**

#### 5. Project Configuration
- Updated `project.pbxproj` to include AMDemodulator.swift in build

## Why This Matters

### PAL-B/G TV Uses AM!
You were absolutely correct - **PAL-B/G television signals use AM (Amplitude Modulation) for the video carrier**, not FM. The analog TV system works like this:

```
PAL-B/G TV Signal:
├── Video Carrier: AM (amplitude modulation) ← What we decode
├── Audio Carrier: FM (frequency modulation, +5.5 MHz offset)
└── Color Subcarrier: QAM (quadrature amplitude modulation)
```

The app now properly uses **envelope detection** (AM demodulation) for TV:
```swift
amplitude = sqrt(I² + Q²)  // AM demodulation
```

Instead of phase differentiation (which would be wrong for TV).

## Three Modes Now Available

### 1. FM Radyo (FM Radio)
- **Demodulation**: Phase differentiation
- **Use**: Commercial FM broadcasts (88-108 MHz)
- **Algorithm**: atan2 → unwrap → differentiate
- **Sample Rate**: 2 MHz

### 2. AM Radyo (AM Radio) 
- **Demodulation**: Envelope detection  
- **Use**: AM broadcasts (MW 540-1700 kHz, SW)
- **Algorithm**: √(I²+Q²) → remove DC
- **Sample Rate**: 2 MHz

### 3. PAL-B/G TV
- **Demodulation**: AM (envelope detection) for video
- **Use**: Analog TV (47-862 MHz in Turkey)
- **Algorithm**: √(I²+Q²) → line sync → frame assembly
- **Sample Rate**: 16 MHz

## UI Now Shows Demodulation Type

When connected, the status area displays:
```
Status: Connected ✓
Samples: 1,234,567
Sample Rate: 16 MHz
Demodülasyon: AM (Video)  ← NEW!
```

This makes it crystal clear which demodulation method is being used.

## Files Modified

1. ✏️ ContentView.swift - UI with 3 modes
2. ✏️ HackRFReceiver.swift - AM demodulator support
3. ✏️ PALDecoder.swift - Clarified AM demodulation comments
4. ✏️ project.pbxproj - Added AMDemodulator to build
5. ✏️ README.md - Updated with AM info
6. ✏️ KURULUM_TR.md - Turkish guide updated
7. ✨ AMDemodulator.swift - NEW file
8. ✨ DEMODULATION_GUIDE.md - NEW comprehensive guide

## Technical Implementation

### AM Demodulator
```swift
class AMDemodulator {
    func demodulate(_ iqSamples: [Int8]) -> [Float]? {
        // 1. Calculate amplitude (envelope detection)
        for idx in 0..<sampleCount {
            let i = Float(iqSamples[idx * 2]) / 127.0
            let q = Float(iqSamples[idx * 2 + 1]) / 127.0
            amplitude[idx] = sqrt(i * i + q * q)  // AM!
        }
        
        // 2. Remove DC component
        let mean = amplitude.reduce(0, +) / Float(amplitude.count)
        for idx in 0..<amplitude.count {
            amplitude[idx] -= mean
        }
        
        // 3. Decimate to audio rate
        // 4. Normalize and return
    }
}
```

### Mode Selection Logic
```swift
switch mode {
case .fmRadio:
    fmDemodulator = FMDemodulator(...)  // Phase differentiation
case .amRadio:
    amDemodulator = AMDemodulator(...)  // Envelope detection
case .tvPAL:
    palDecoder = PALDecoder(...)        // Also uses envelope detection
}
```

## How to Use

1. **Open project** in Xcode
2. **Select mode** in the UI:
   - "FM Radyo" for FM broadcasts
   - "AM Radyo" for AM broadcasts  
   - "PAL-B/G TV" for Turkish TV (uses AM for video)
3. **Connect** and the correct demodulator will be used automatically
4. **Status shows** which demodulation method is active

## Benefits

✅ **Correct demodulation** for each signal type  
✅ **Clear UI** showing what's happening  
✅ **Three modes** instead of two  
✅ **Proper AM** implementation for TV  
✅ **Educational** - users can compare FM vs AM  
✅ **Turkish language** labels and documentation  
✅ **No breaking changes** to existing functionality

## Testing Recommendations

1. **FM Radio**: Test on 88-108 MHz FM broadcasts
2. **AM Radio**: Test on 540-1700 kHz MW broadcasts
3. **TV**: Test on Turkish PAL-B/G channels (47-862 MHz)

For TV, remember:
- Must use 16 MHz sample rate on server
- Video carrier frequency is critical
- AM demodulation is mandatory (not FM!)

## Next Steps

The project is now complete with proper AM/FM support. Users can:
- Choose the correct demodulation for their signal
- See which method is being used
- Understand why TV uses AM, not FM
- Use the comprehensive demodulation guide

Everything is ready to build and run! 🎉

---

**Note**: The original implementation already had AM demodulation in the PALDecoder (using envelope detection), but now it's explicit in the UI and documentation, and we've added a separate AM radio mode for completeness.
