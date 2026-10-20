# HackTv Studio — Android (teyp) sürümü

Masaüstü Qt 6 uygulamasının Android portu. Aynı C++/DSP kodu, aynı hacktv/libhackrf/libusb;
tek fark USB erişiminin Android yolu. RADIO RX/TX + TV RX/TX hepsi dahil.

## Android'e özgü ne eklendi

Android uygulaması USB cihazını doğrudan açamaz (usbfs izinleri yok). Bu yüzden:

- **`android/src/com/hacktvstudio/HackRfUsb.java`** — Java `UsbManager` ile HackRF'i bulur
  (VID 0x1d50, PID 0x6089/0x604b/0xcc15), izin ister, açar ve dosya tanımlayıcısını (fd) verir.
- **`hackrf/AndroidUsb.cpp/.h`** — JNI köprüsü; `QNativeInterface` ile Android context'ini alıp
  yukarıdaki Java metotlarını çağırır.
- **`3rdparty/libhackrf/src/hackrf.c`** — `hackrf_open_wrapped(int fd, ...)` eklendi:
  `libusb_wrap_sys_device` ile fd'yi libusb'ye sarar; `hackrf_init` içinde Android'de
  `LIBUSB_OPTION_NO_DEVICE_DISCOVERY` ayarlanır.
- **`hackrf/hackrfdevice.cpp`** — Android'de `listDevices()` UsbManager'dan sorar, açma
  `hts_open_hackrf()` ile fd yolundan `hackrf_open_wrapped` çağırır. START GUI iş parçacığında
  çalıştığı için izin beklenirken `QCoreApplication::processEvents()` ile olay döngüsü işletilir;
  aksi halde Android'in izin yayını (aynı looper) hiç işlenmez ve ilk START her zaman
  "bulunamadı" derdi.
- **`android/AndroidManifest.xml`** — `usb.host` özelliği, mikrofon izni, HackRF takılınca
  otomatik açılma (`USB_DEVICE_ATTACHED` + `res/xml/device_filter.xml`).
- **`.pro` `android { }` bloğu** — libusb'nin Linux usbfs backend'i (bionic) + Android USB köprüsü.

DSP tarafı: `core/SimdDot.h` SSE2 olmayan ARM'da skaler yedek yola düşer; değişiklik gerekmedi.

## Derleme

NDK: r26b (`%LOCALAPPDATA%\Android\Sdk\ndk\android-ndk-r26b`), Qt 6.11.1 Android kitleri, JDK 17.

```powershell
cd C:\Users\turka\Desktop\HackTvTxRx\HackTvStudio
.\build_android.ps1 arm64     # teyp (arm64-v8a)  -> ..\HackTvStudio-arm64.apk
.\build_android.ps1 x86_64    # emülatör          -> ..\HackTvStudio-x86_64.apk
```

## Kurulum ve test

- **Emülatör (x86_64):** `adb install -r HackTvStudio-x86_64.apk`. Arayüz, sekmeler ve
  "cihaz yok" davranışı test edilebilir. **HackRF emülatörde görünmez** (QEMU USB host
  cihazını aktarmaz), bu yüzden RX/TX/TV sinyal işleme emülatörde çalışmaz.
- **Teyp (arm64):** APK'yı teybe kopyalayıp kurun. HackRF'i **USB-OTG** kablosuyla takın.
  Uygulama ilk START'ta USB izni ister; onaylayın. RX/TX/TV testi burada yapılır.

Emülatörde doğrulandı (2026-10-21, dört sekme tek tek): APK kuruluyor, Qt arayüzü tam yükleniyor (frekans kontrolü,
kanal gücü, spektrum/şelale, RADIO RX/TX + TV RX/TX sekmeleri, HackRF kontrol grubu), START'a
basınca cihaz yokken düzgün uyarı veriyor, çökmüyor.

## Ekran yerleşimi (Android)

`StudioWindow.cpp` ve `main.cpp`'de `Q_OS_ANDROID` blokları: üst şerit sıkıştırıldı (frekans/metre
yüksekliği, cihaz ızgarası), REC IQ gizlendi, spektrum/şelale splitter'da ~%45 pay alır, global font
0.85× + sekme/başlık/değer kutuları için sabit piksel boyutları (`main.cpp`), spektrum eksen fontu 12 px (`glplotter.cpp`). RADIO RX/TX 1280×720'de tamamen sığar; TV RX/TX'te video+spektrum büyük olduğundan alt
kontroller kaydırılır (paneller QScrollArea içinde).

## İmzalama

`build_android.ps1` debug imzalı APK üretir (sideload için yeterli). Yayın için ayrı bir
keystore ile release imzalama gerekir.
