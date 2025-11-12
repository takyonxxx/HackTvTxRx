# Demodülasyon Rehberi / Demodulation Guide

## Türkçe

### FM ve AM Demodülasyonu Nedir?

**FM (Frekans Modülasyonu)**
- Bilgi sinyalin frekansında kodlanır
- Yüksek ses kalitesi
- Gürültüye karşı dayanıklı
- Kullanım: FM radyo (88-108 MHz), TV ses taşıyıcısı
- Algoritma: Faz farklılaştırma (phase differentiation)

**AM (Genlik Modülasyonu)**
- Bilgi sinyalin genliğinde kodlanır
- Basit alıcı devresi
- Orta dalga ve kısa dalga radyo
- **PAL-B/G TV video taşıyıcısı AM kullanır**
- Algoritma: Zarf algılama (envelope detection)

### Hangi Modu Seçmeliyim?

| Sinyal Türü | Demodülasyon | Frekans Aralığı |
|--------------|--------------|-----------------|
| FM Radyo | FM | 88-108 MHz |
| AM Radyo (Orta Dalga) | AM | 540-1700 kHz |
| PAL-B/G TV Video | **AM** | 47-862 MHz |
| TV Ses | FM | Video + 5.5 MHz |

### PAL-B/G TV için Önemli

PAL-B/G televizyon sisteminde:
- **Video taşıyıcı: AM** (genlik modülasyonu)
- Ses taşıyıcı: FM (frekans modülasyonu)
- Renk alt taşıyıcı: QAM (quadrature amplitude modulation)

Bu uygulama video taşıyıcı için AM demodülasyonu kullanır (√(I²+Q²)).

### Türkiye'de TV Kanalları

PAL-B/G standardı kullanılır:
- VHF Band I: 47-68 MHz
- VHF Band III: 174-230 MHz  
- UHF Band IV/V: 470-862 MHz

Her kanal 7 MHz genişliğindedir.

---

## English

### What is FM and AM Demodulation?

**FM (Frequency Modulation)**
- Information encoded in signal frequency
- High audio quality
- Resistant to noise
- Usage: FM radio (88-108 MHz), TV audio carrier
- Algorithm: Phase differentiation

**AM (Amplitude Modulation)**
- Information encoded in signal amplitude
- Simple receiver design
- Medium wave and short wave radio
- **PAL-B/G TV video carrier uses AM**
- Algorithm: Envelope detection

### Which Mode Should I Choose?

| Signal Type | Demodulation | Frequency Range |
|-------------|--------------|-----------------|
| FM Radio | FM | 88-108 MHz |
| AM Radio (MW) | AM | 540-1700 kHz |
| PAL-B/G TV Video | **AM** | 47-862 MHz |
| TV Audio | FM | Video + 5.5 MHz |

### Important for PAL-B/G TV

In PAL-B/G television system:
- **Video carrier: AM** (amplitude modulation)
- Audio carrier: FM (frequency modulation)
- Color subcarrier: QAM (quadrature amplitude modulation)

This app uses AM demodulation for the video carrier (√(I²+Q²)).

### TV Channels in Turkey

Uses PAL-B/G standard:
- VHF Band I: 47-68 MHz
- VHF Band III: 174-230 MHz
- UHF Band IV/V: 470-862 MHz

Each channel is 7 MHz wide.

---

## Technical Details

### FM Demodulation Algorithm

```
IQ Samples → Complex (I + jQ)
           ↓
      atan2(Q, I) = Phase
           ↓
      Unwrap Phase
           ↓
    Differentiate
           ↓
      FM Audio
```

**Implementation:**
```swift
let phase = atan2(q, i)
let unwrapped = unwrapPhase(phase)
let demod = differentiate(unwrapped)
```

### AM Demodulation Algorithm

```
IQ Samples → Envelope Detection
           ↓
      √(I² + Q²) = Amplitude
           ↓
      Remove DC
           ↓
      AM Audio/Video
```

**Implementation:**
```swift
let amplitude = sqrt(i*i + q*q)
let signal = amplitude - dcOffset
```

### Why PAL TV Uses AM

Analog television systems use AM for the video carrier because:
1. **Bandwidth efficiency**: AM allows for vestigial sideband transmission
2. **Simple demodulation**: Envelope detection is straightforward
3. **Compatibility**: Standard for all analog TV systems worldwide
4. **Separate modulation**: Video (AM) and audio (FM) can coexist

### App Implementation

The app provides three modes:

1. **FM Radyo**: Phase-based FM demodulation
   - For commercial FM broadcasts
   - 75µs de-emphasis filter
   - High fidelity audio

2. **AM Radyo**: Envelope-based AM demodulation
   - For AM broadcasts
   - DC removal
   - Simple and effective

3. **PAL-B/G TV**: AM demodulation + video processing
   - Amplitude detection for video
   - Line and frame sync
   - Grayscale output (luminance only)

### Performance Characteristics

| Feature | FM | AM |
|---------|----|----|
| Noise Immunity | Excellent | Fair |
| Audio Quality | High | Medium |
| Bandwidth | Wide (~200 kHz) | Narrow (~10 kHz) |
| Complexity | Moderate | Simple |
| Power Efficiency | Lower | Higher |

---

## Troubleshooting

### FM Radyo
- **Distorted audio**: Check server gains (VGA/LNA)
- **No audio**: Verify frequency is correct
- **Clicking sounds**: Reduce sample rate or buffer size

### AM Radyo  
- **Weak signal**: Increase LNA gain
- **Hum/buzz**: Check for DC offset removal
- **Fading**: Normal for AM, especially at night

### PAL-B/G TV
- **No sync**: Must use AM demodulation, not FM
- **Rolling image**: Frequency accuracy critical
- **No picture**: Verify 16 MHz sample rate
- **Grainy image**: Increase signal strength

---

## References

- PAL-B/G Standard: ITU-R BT.470
- FM Broadcasting: ITU-R BS.450
- AM Broadcasting: ITU-R BS.703

For more information, see the main README.md file.
