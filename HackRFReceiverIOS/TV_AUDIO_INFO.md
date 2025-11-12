# PAL-B/G TV - Video VE Ses Decode Edilir

## ✅ Güncellenmiş Özellik

PAL-B/G TV modu artık **hem video hem ses** decode ediyor!

## 📺 Video + 🔊 Ses

### Video Taşıyıcı
- **Demodülasyon**: AM (Genlik modülasyonu)
- **Yöntem**: Zarf algılama √(I²+Q²)
- **Çıkış**: 720x576 grayscale video
- **Standart**: PAL-B/G 625 satır, 25 fps

### Ses Taşıyıcı
- **Demodülasyon**: FM (Frekans modülasyonu)
- **Frekans**: Video + 5.5 MHz
- **De-emphasis**: 50µs (PAL TV standardı)
- **Çıkış**: 48 kHz mono ses

## 🎯 Nasıl Çalışır?

PAL-B/G televizyon sisteminde ses ve video ayrı taşıyıcılarda iletilir:

```
Örnek: TRT 1 (Kanal E2)
├── Video Taşıyıcı: 48.25 MHz (AM)
└── Ses Taşıyıcı:   53.75 MHz (FM) ← Video + 5.5 MHz
```

### İşleme Adımları

1. **16 MHz IQ örnekleri alınır**
2. **Video için**:
   - AM demodülasyonu (√(I²+Q²))
   - Satır ve kare senkronizasyonu
   - 720x576 görüntü oluşturulur
3. **Ses için**:
   - +5.5 MHz frekans kaydırması
   - FM demodülasyonu (faz farklandırma)
   - 50µs de-emphasis filtresi
   - 48 kHz ses çıkışı

## 🔧 Teknik Detaylar

### PAL-B/G Standart Parametreleri

| Parametre | Değer |
|-----------|-------|
| Video Modulasyonu | AM (Negatif) |
| Ses Modulasyonu | FM |
| Ses Offset | +5.5 MHz |
| Video Bandwidth | 5 MHz |
| Ses Bandwidth | ±50 kHz |
| Ses De-emphasis | 50µs |
| Kanal Genişliği | 7 MHz |

### Uygulama Detayları

```swift
// Video: AM demodülasyonu
let videoAmplitude = sqrt(I² + Q²)

// Ses: +5.5 MHz'e kaydır, sonra FM demodülasyonu
let shifted_IQ = frequencyShift(IQ, offset: 5.5MHz)
let audioSignal = fmDemodulate(shifted_IQ)
let audioOutput = deemphasis_50us(audioSignal)
```

## 🎵 Ses Kalitesi

- **Mono ses**: Tek kanal audio
- **Örnekleme hızı**: 48 kHz
- **Dinamik aralık**: ~40 dB (FM limitasyonu)
- **Frekans yanıtı**: 50 Hz - 15 kHz
- **Gecikme**: Video ile senkronize (~1 kare)

## 📻 FM Radio vs TV Ses

| Özellik | FM Radyo | PAL TV Ses |
|---------|----------|------------|
| De-emphasis | 75µs | 50µs |
| Bandwidth | ±75 kHz | ±50 kHz |
| Deviation | ±75 kHz | ±50 kHz |
| Stereo | Evet (pilot tone) | Hayır (mono) |
| Kalite | Yüksek | Orta |

## 🎬 Kullanım

1. **PAL-B/G TV modunu seçin**
2. **Doğru TV kanalı frekansını girin**
   - Örnek: Kanal E2 = 48.25 MHz
3. **Bağlanın**
4. **Hem video hem ses otomatik olarak decode edilir**
   - Video: Ekranda görüntülenir
   - Ses: iPhone hoparlöründen çalar

## 🔍 Debugging

### Video var ama ses yok?
- Ses taşıyıcısı +5.5 MHz'de olmalı
- Sinyal gücü yeterli mi kontrol edin
- LNA gain'i artırın

### Ses bozuk veya anlaşılmaz?
- Frekans doğruluğu kritik (±10 kHz içinde olmalı)
- 16 MHz sample rate kullandığınızdan emin olun
- VGA gain'i ayarlayın

### Ses ile video senkronize değil?
- Normal: ~40ms gecikme olabilir (1 kare)
- Daha fazla gecikme varsa, buffer ayarlarını kontrol edin

## 📡 Türkiye'de TV Kanalları

PAL-B/G standardı kullanılan frekanslar:

**VHF Band I (47-68 MHz)**
- E2: Video 48.25 MHz, Ses 53.75 MHz
- E3: Video 55.25 MHz, Ses 60.75 MHz
- E4: Video 62.25 MHz, Ses 67.75 MHz

**VHF Band III (174-230 MHz)**
- E5-E12: Her kanal 7 MHz genişliğinde

**UHF Band IV/V (470-862 MHz)**
- E21-E69: Her kanal 7 MHz genişliğinde

## 💡 İpuçları

1. **Güçlü bir sinyal bulun** - Ses decode için video'dan daha fazla sinyal gücü gerekir
2. **Frekansı hassas ayarlayın** - ±10 kHz içinde olmalı
3. **Kazançları optimize edin** - VGA 20-30, LNA 24-32
4. **Mono beklentisi** - Stereo ses PAL-B/G'de standart değildir

## 🆚 Diğer TV Sistemleri

| Sistem | Video | Ses | Ses Offset | De-emphasis |
|--------|-------|-----|------------|-------------|
| PAL-B/G | AM | FM | +5.5 MHz | 50µs |
| PAL-D/K | AM | FM | +6.5 MHz | 50µs |
| NTSC-M | AM | FM | +4.5 MHz | 75µs |
| SECAM | AM | FM | +6.5 MHz | 50µs |

Bu uygulama **PAL-B/G** (Türkiye ve Avrupa standardı) için optimize edilmiştir.

## ✨ Sonuç

Artık PAL-B/G TV modu ile **tam bir analog TV deneyimi** yaşayabilirsiniz:
- ✅ Video (AM demodülasyonu)
- ✅ Ses (FM demodülasyonu, 50µs de-emphasis)
- ✅ Gerçek zamanlı işleme
- ✅ iPhone hoparlöründen ses çıkışı
- ✅ Ekranda video görüntüsü

**Hem görüntü hem ses - tıpkı gerçek bir TV gibi!** 📺🔊

---

## Ek Kaynaklar

- PAL-B/G Standardı: ITU-R BT.470
- TV Ses Sistemi: ITU-R BS.450
- Frekans Planı: ETSI EN 300 429

Daha fazla bilgi için README.md dosyasına bakın.
