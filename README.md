# GD AutoBot — Geometry Dash 2.209 (Android + Windows)

Geode, Mega Hack, root veya oyun dosyası değişikliği **gerektirmeyen** bir Auto Play / Macro botu.
Bot oyunu yalnızca **ekrandan görür** ve yalnızca **işletim sisteminin resmi giriş yollarıyla** dokunur/tuşa basar:

| | Windows (EXE) | Android (APK) |
|---|---|---|
| Ekran | GDI ile GD penceresinin istemci alanı | MediaProjection (kullanıcı onaylı) |
| Giriş | `SendInput` (sadece GD odaktayken) | AccessibilityService `dispatchGesture` (kullanıcı açar) |
| Kayıt | Low-level klavye/fare kancası (sadece GD odaktayken) | Kayıt pedi (dokunuşu kaydeder + oyuna iletir) |
| Bot göstergesi | GD üstünde "▶ BOT · AUTO PLAY" etiketi | Kayan panel + bildirim |

Ortak bot mantığı tek bir C++17 çekirdekte (`core/`): iki platform da aynı kodu kullanır
(Windows doğrudan, Android JNI ile).

## Klasörler

```
core/            ortak C++ çekirdek (macro, ekran analizi, smart pilot, motor)
windows/         Win32 uygulaması + kaynaklar + Zig cross-compile betiği
android/         Android Studio projesi (Java + NDK/CMake, core'u derler)
tests/           host testleri + GD benzeri simülatör + JNI smoke testi
dist/windows/    derlenmiş GDAutoBot.exe
tools/           ikon üretici
```

## Modlar

- **Smart Auto Play** — her karede oyuncu (kalibre edilmiş renk), engeller ve koridor bulunur.
  Cube/Robot: zıplama engel grubunun ortasına hizalanır. Ship: koridora PD kontrol. Wave: histerezisli koridor takibi.
  UFO/Swing: koridora göre kanat/yerçekimi. Ball/Spider: önde engel varsa yerçekimi çevirme.
- **Macro Record / Replay** — µs zaman damgalı `.gdbm` dosyası. Attempt başlangıcı ekrandan bulunur
  (ilerleme çubuğu sıfırlanması + başlangıç karesi eşleştirme), her ölüm yeni deneme; **en yüksek % alan deneme** saklanır.
- **Practice Assist** — aralıklı otomatik checkpoint (PC: `Z`, Android: kalibre edilmiş düğme).
- **Auto Retry** — çubuk takılı kalırsa (ölüm ekranı) kalibre edilmiş Retry düğmesine tıklar; GD'nin kendi auto-retry'ı açıkken bot her yeni denemede kendini yeniden kurar.

Ayrıntılar: [BUILD.md](BUILD.md) (derleme), [KURULUM.md](KURULUM.md) (kurulum + kullanım), [TEST_REPORT.md](TEST_REPORT.md) (neler çalışıyor / çalışmıyor).

## Dürüst sınırlar (özet)

- Smart Auto Play **genel bir çözüm değildir**: ekrandan oyun modu, portal, orb, pad, speed/mini/dual algılanmaz.
  Bunlar panelden/kısayoldan elle ayarlanır. Dekorasyonu yoğun, renkli, efektli levellerde engel maskesi yanılır.
- Ekrandan senkron, yakalama hızı kadar titreşim içerir (60 Hz'de ±1 kare). Kolay/orta levellerde macro tutar,
  frame-perfect (Extreme Demon) macro'lar için mem-hack tabanlı botlar kadar hassas değildir.
- Android'de yeni bir dokunma hareketi devam edeni iptal eder → 2 oyunculu eşzamanlı basılı tutma yok.
- APK bu ortamda derlenmedi (Android SDK indirilemedi). EXE derlendi ama Windows'ta çalıştırılıp test edilmedi.

Bot kullanımı her iki platformda da ekranda açıkça gösterilir. Rated level rekorları/sıralamalar için bot kullanmak
GD topluluk kurallarına aykırıdır; bu araç pratik, analiz ve kendi levellerin içindir.
