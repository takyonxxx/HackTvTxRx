# HackRF Alıcı - Kurulum Kılavuzu (Türkçe)

## Hızlı Başlangıç

### FM Radyo Modu
1. Uygulamayı açın
2. Sunucu IP adresini girin (örn: `192.168.1.2`)
3. Port numarasını girin (örn: `5000`)
4. Frekansı MHz olarak girin (örn: `100.0`)
5. "FM Radio" modunu seçin
6. "Connect" düğmesine basın
7. Ses hoparlörden çalacaktır

### PAL-B/G TV Modu (Türkiye Standardı)
1. Sunucu bilgilerini girin
2. TV kanalı frekansını ayarlayın
3. "PAL-B/G TV" modunu seçin
4. "Connect" düğmesine basın
5. Video görüntüsü ekranda gösterilecektir

## Türkiye'de TV Frekansları

PAL-B/G sistemi Türkiye'de kullanılır:
- VHF Band I: 47-68 MHz
- VHF Band III: 174-230 MHz
- UHF Band IV/V: 470-862 MHz

### Örnek Kanal Frekansları
- Kanal E2: 48.25 MHz (video carrier)
- Kanal E3: 55.25 MHz
- Kanal E4: 62.25 MHz
- ...

## Teknik Ayarlar

### FM Radyo
- Örnek Hızı: 2 MHz
- Ses Çıkışı: 48 kHz
- De-emphasis: 75µs

### PAL-B/G TV
- Örnek Hızı: 16 MHz
- Satır Sayısı: 625 (576 aktif)
- Kare Hızı: 25 fps
- Çözünürlük: 720x576

## Sorun Giderme

### Bağlantı Sorunları
- IP adresini ve port numarasını kontrol edin
- WiFi bağlantınızın stabil olduğundan emin olun
- Güvenlik duvarı ayarlarını kontrol edin

### Ses Sorunları
- iPhone ses seviyesini kontrol edin
- Sessiz modda olmadığınızdan emin olun
- Sunucu kazanç ayarlarını düzenleyin

### TV Sorunları
- 16 MHz örnek hızı kullandığınızdan emin olun
- Frekansın doğru olduğunu kontrol edin
- Senkronizasyon için frekans hassasiyeti önemlidir

## Gereksinimler

- iOS 16.0 veya üzeri
- HackRF TCP sunucusu
- Yerel ağ bağlantısı

## Notlar

- Uygulama sadece yerel ağlarda kullanım için tasarlanmıştır
- Güvenilir ağlarda kullanın
- TV modu şu anda sadece siyah-beyaz (luma) desteklemektedir
- Renk desteği gelecek versiyonlarda eklenebilir

---

**Destek**: Sorunlar için README.md dosyasına bakın
