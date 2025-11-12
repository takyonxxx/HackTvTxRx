# HackRF iOS Alıcı

HackRF TCP sunucusundan sinyal alan, FM/AM radyo ve PAL-B/G TV decode eden iOS uygulaması.

## Özellikler

- **FM Radyo**: 88-108 MHz, geniş bant FM, 75µs de-emphasis
- **AM Radyo**: 540-1700 kHz (OD/KD), zarf algılama
- **NFM Telsiz**: VHF/UHF (136-174, 400-470 MHz), dar bant FM
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

#### AM Kullanım Alanları
- **Orta Dalga:** 540-1700 kHz (radyo yayınları)
- **Kısa Dalga:** 3-30 MHz (uluslararası yayınlar)
- **Havacılık:** 118-137 MHz (pilot-kule konuşmaları) ✈️

**Havacılık örnekleri (Türkiye):**
- İstanbul Atatürk: 118.100 MHz
- Sabiha Gökçen: 118.600 MHz
- Esenboğa Ankara: 118.800 MHz
- Antalya: 120.050 MHz
- **Acil Durum:** 121.500 MHz

### NFM Telsiz (VHF/UHF)
```
1-3. Yukarıdaki gibi
4. "NFM Telsiz" seçin
5. Frekans: 136-174 MHz (VHF) veya 400-470 MHz (UHF)
6. Bağlan → Telsiz konuşmaları hoparlörden
```
**Sunucu:** 2 MHz sample rate

#### NFM Frekans Örnekleri
**VHF (136-174 MHz):**
- Amatör: 144-146 MHz
- Denizcilik: 156-162 MHz

**UHF (400-470 MHz):**
- Amatör: 430-440 MHz
- PMR446: 446 MHz
- İş telsizleri: 450-470 MHz

**Not:** Havacılık (118-137 MHz) **AM Radyo** modunu kullanır, NFM değil!

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
| FM Radyo | Faz farklandırma | atan2 → unwrap → differentiate (geniş bant) |
| AM Radyo | Zarf algılama | √(I²+Q²) |
| NFM Telsiz | Faz farklandırma | atan2 → unwrap → differentiate (dar bant ±5kHz) |
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
├── FMDemodulator.swift           (Geniş bant FM)
├── AMDemodulator.swift           (AM işleme)
├── NFMDemodulator.swift          (Dar bant FM - VHF/UHF)
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

**NFM için:**
- VHF: 136-174 MHz, UHF: 400-470 MHz
- Dar bant (±5 kHz deviation)
- Telsiz konuşmaları için
- Sessizlikte squelch gürültüsü normal

**TV için:**
- Güçlü sinyal gerekir
- Frekans çok hassas olmalı (±10 kHz)
- Mono ses (stereo yok)
- Grayscale görüntü (renk yok)
- Video + Ses aynı anda çalar

## Sık Sorulan Sorular

**Q: Havacılık frekanslarını dinleyebilir miyim?**  
A: Evet! 118-137 MHz havacılık bandı için **AM Radyo** modunu kullanın. Örnek: 118.1 MHz (İstanbul Atatürk Kule). Acil durum frekansı 121.500 MHz.

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

**Versiyon:** 1.3  
**Platform:** iOS 16.0+  
**Dil:** Swift 5.0  
**Durum:** ✅ Hazır

**Özellikler:**
- ✅ 4 mod (FM, AM, NFM, TV)
- ✅ VHF/UHF telsiz desteği
- ✅ TV video + ses
- ✅ Gerçek zamanlı
- ✅ Türkçe arayüz
