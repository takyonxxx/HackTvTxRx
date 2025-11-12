# HackRF iOS Alıcı

HackRF TCP sunucusundan sinyal alan, FM/AM radyo ve PAL-B/G TV decode eden iOS uygulaması.

## Özellikler

- **FM Radyo**: 88-108 MHz, faz demodülasyonu, 75µs de-emphasis
- **AM Radyo**: 540-1700 kHz, zarf algılama  
- **PAL-B/G TV**: Video (AM) + Ses (FM +5.5MHz), 720x576, 25fps
- Gerçek zamanlı ses/video çıkışı
- Frekans kontrolü (+/- butonları)
- Türkçe arayüz

## Kurulum

1. `HackRFReceiver.xcodeproj` dosyasını Xcode'da açın
2. Signing & Capabilities'ten Team'inizi seçin
3. iPhone'unuza bağlayın veya simulator seçin
4. ⌘R ile çalıştırın

**Gereksinimler:** iOS 16.0+, Xcode 15.0+

## Kullanım

### FM Radyo
```
1. Sunucu IP'sini girin (örn: 192.168.1.2)
2. Port girin (örn: 5000)
3. Frekans girin (örn: 100.0 MHz)
4. "FM Radyo" seçin
5. Bağlan → Ses hoparlörden çalar
```
**Sunucu:** 2 MHz sample rate

### AM Radyo
```
1-3. Yukarıdaki gibi
4. "AM Radyo" seçin
5. Bağlan → Ses hoparlörden çalar
```
**Sunucu:** 2 MHz sample rate

### PAL-B/G TV
```
1. IP/Port girin
2. TV kanalı frekansı (örn: 48.25 MHz)
3. "PAL-B/G TV" seçin
4. Bağlan → Video ekranda + Ses hoparlörde
```
**Sunucu:** 16 MHz sample rate (**önemli!**)

#### TV Frekansları (Türkiye)
- Kanal E2: 48.25 MHz
- Kanal E3: 55.25 MHz
- Kanal E4: 62.25 MHz
- VHF Band III: 174-230 MHz
- UHF: 470-862 MHz

## Teknik Detaylar

### Demodülasyon

| Mod | Yöntem | Açıklama |
|-----|--------|----------|
| FM Radyo | Faz farklandırma | atan2 → unwrap → differentiate |
| AM Radyo | Zarf algılama | √(I²+Q²) |
| TV Video | Zarf algılama | √(I²+Q²) |
| TV Ses | FM (+5.5 MHz) | Frekans shift + FM demod |

### PAL-B/G TV Sistemi
```
Video Carrier (AM):  48.25 MHz
Audio Carrier (FM):  53.75 MHz (+5.5 MHz)
                     50µs de-emphasis
```

### Dosya Yapısı
```
HackRFReceiver/
├── HackRFReceiverApp.swift      (Uygulama başlangıcı)
├── ContentView.swift             (Ana UI)
├── HackRFReceiver.swift          (Koordinatör)
├── TCPClient.swift               (Network)
├── FMDemodulator.swift           (FM işleme)
├── AMDemodulator.swift           (AM işleme)
├── AudioPlayer.swift             (Ses çıkışı)
├── PALDecoder.swift              (TV video + ses)
└── TVDisplayView.swift           (Video görüntü)
```

## Sorun Giderme

### Bağlantı
- **Bağlanamıyor**: IP ve port kontrolü, aynı WiFi'de olun
- **Bağlantı düşüyor**: WiFi stabilitesi, buffer boyutu

### Ses
- **Ses yok**: iPhone ses seviyesi, mute switch, mod doğru mu?
- **Bozuk ses**: Sunucu gain ayarları (VGA/LNA)
- **Gecikme**: Normal (~0.5-1 saniye)

### TV
- **Video senkronize değil**: **16 MHz sample rate** şart!
- **Ses yok**: Frekans ±10 kHz içinde, LNA gain artır
- **Yuvarlanan görüntü**: Frekans hassasiyeti kritik
- **Bulanık**: Sinyal gücü, gain ayarları

### TV İçin Kritik
```
✓ 16 MHz sample rate (2 MHz değil!)
✓ Doğru video carrier frekansı
✓ Ses otomatik +5.5 MHz'de
✓ LNA: 24-32, VGA: 20-30
```

## Sunucu Gereksinimleri

Python TCP sunucunuz şunları göndermeli:
- **IQ örnekleri**: int8 çiftleri (I,Q,I,Q,...)
- **Sample rate**: FM/AM için 2 MHz, TV için 16 MHz
- **Port**: 5000 (varsayılan)

## Performans

| Mod | CPU | Bellek | Gecikme |
|-----|-----|--------|---------|
| FM | %10-15 | ~30 MB | 0.5-1s |
| AM | %10-15 | ~30 MB | 0.5-1s |
| TV | %20-30 | ~50 MB | 40-50ms |

## İpuçları

**FM/AM için:**
- 88-108 MHz (FM) veya 540-1700 kHz (AM)
- +/- butonlarla frekans ayarı
- Yüksek ses kalitesi beklenir

**TV için:**
- Güçlü sinyal gerekir
- Frekans çok hassas olmalı (±10 kHz)
- Mono ses (stereo yok)
- Grayscale görüntü (renk yok)
- Video + Ses aynı anda çalar

## Sık Sorulan Sorular

**Q: TV'de neden ses yok?**  
A: Ses +5.5 MHz'de otomatik. Frekans doğru, sinyal güçlü olmalı.

**Q: Renkli TV olur mu?**  
A: Şimdilik sadece grayscale (Y). Renk (UV) gelecek versiyonda.

**Q: Stereo ses?**  
A: PAL-B/G standardında mono. Stereo standart değil.

**Q: Neden 16 MHz TV için?**  
A: PAL-B/G standardı gereği. Video + ses için gerekli bandwidth.

## Lisans

Eğitim amaçlı demo projesi.

---

**Versiyon:** 1.2  
**Platform:** iOS 16.0+  
**Dil:** Swift 5.0  
**Durum:** ✅ Hazır

**Özellikler:**
- ✅ 3 mod (FM, AM, TV)
- ✅ TV video + ses
- ✅ Gerçek zamanlı
- ✅ Türkçe arayüz
