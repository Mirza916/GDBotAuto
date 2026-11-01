# Test raporu — GD AutoBot 0.1.0 (2026-11-01)

Ortam: Linux x86_64 bulut ortamı. **Burada Windows, Android cihaz/emülatör veya Geometry Dash yok.**
Android SDK sunucuları (dl.google.com, maven.google.com) bu ortamdan erişilemezdi.

## Gerçekten çalıştırılan testler

| Test | Sonuç |
|---|---|
| Çekirdek birim testleri (`gdbot_tests`): config, macro formatı, kayıt (en iyi deneme), oynatma zamanlaması, ilerleme çubuğu okuma (%0–100), attempt/ölüm/takılma algılama, sahne analizi, acil durdurma, manuele geçiş, pause, practice assist | **48/48 geçti** |
| Uçtan uca macro (simülatör): motor ekrandan kayıt yapar (1. deneme ölür, 2. biter) → macro → 3 farklı yakalama fazıyla yeniden oynatma, senkron ilerleme çubuğundan | **24/24 replay bitti** (8 kayıt × 3) |
| JNI köprüsü: `javac -h` ile üretilen başlığa karşı derlendi (14/14 imza eşleşti), host JVM'de `NativeBridge` ile çağrıldı (padding'li RGBA stride, kalibrasyon, kayıt, macro yükleme, tick zamanlaması) | **13/13 ok** |
| Android XML'leri: parse + tüm `R.*` / `@…/` referansları mevcut | ok |
| Windows EXE: Zig ile x86_64-windows-gnu cross-compile, GUI subsystem, sadece sistem DLL'leri + UCRT import | **derlendi** (591 KB) |

### Smart Auto Play — simülatör sonuçları

Simülatör: GD benzeri çizim (gradyan arka plan, zemin, siyah engeller, yeşil oyuncu, ilerleme çubuğu, gürültü)
+ **basitleştirilmiş fizik** (kutu hitbox, yaklaşık zıplama/ship ivmesi). Her biri 20 rastgele 150 blokluk parkur:

| Mod | Yakalama | Gecikme | Tamamlanan |
|---|---|---|---|
| Cube (1–3'lü dikenler, bloklar) | 60 Hz | 20 ms | 20/20 |
| Cube | 60 Hz | 50 ms | 20/20 |
| Cube | 30 Hz | 20 ms | 20/20 |
| Ship (dalgalı koridor) | 60 Hz | 20 ms | 20/20 |
| Ship | 60 Hz | 50 ms | 20/20 |
| Wave (merdiven koridor) | 60 Hz | 20 ms | 13/20 |
| Wave | 30 Hz | 20 ms | 6/20 |

**Bu sayılar gerçek GD sonucu değildir.** Gerçek levellerde dekorasyon, efektler, parçacıklar, renk tetikleyicileri,
kamera hareketleri ve gerçek hitbox'lar başarıyı belirgin şekilde düşürür.

Testlerin bulup düzelttiği gerçek hatalar: çubuk okuyucuda %15 taşma; %100'ün %0 sanılması; duvarla dolu satırlarda
arka planın yanlış tahmini (ship/wave 0/4 → 4/4); boş level bölümünün "başlangıç karesi" sanılıp macro'nun sıfırlanması
(replay 6/24 → 24/24); %98'de "level bitti" sanılıp macro'nun kesilmesi.

## Özellik durumu

| Özellik | Windows | Android | Not |
|---|---|---|---|
| Macro Record | kodlandı, **cihazda test edilmedi** | kodlandı, **test edilmedi** | Android'de kayıt pedi ile |
| Macro Replay | kodlandı, test edilmedi | kodlandı, test edilmedi | çekirdek mantık simülatörde test edildi |
| Attempt senkronu (çubuk + başlangıç karesi) | ✓ çekirdek test edildi | ✓ | GD'de "Show Progress Bar" açık olmalı |
| Smart Auto Play: Cube / Ship / Robot | çekirdek simülatörde ✓ | ✓ | gerçek levelde doğrulanmadı |
| Smart: Wave | kısmen (13/20 sim) | kısmen | dar koridorlarda zayıf |
| Smart: UFO / Swing / Ball / Spider | kodlandı, **simülatörde test edilmedi** | aynı | basit kurallar |
| Oyun modu / portal / speed / mini / dual / gravity **otomatik algılama** | ✗ yok | ✗ yok | elle: F2/F3/F4 veya panel |
| Orb ve pad algılama | ✗ yok | ✗ yok | engel maskesine karışabilir |
| Dual / 2 oyuncu | macro'da P2 tuşu destekli; Smart sadece P1 | eşzamanlı 2 basılı tutma **mümkün değil** | Android gesture API sınırı |
| Practice Assist | Z tuşu (aralıklı) | kalibre edilmiş düğmeye dokunma | |
| Auto Retry | Retry düğmesine tıklama (kalibre) + her yeni attempt'te yeniden kurulma | aynı | |
| Input Delay / Timing Offset / Dokunma süresi | ✓ | ✓ | |
| Analiz FPS göstergesi | ✓ | ✓ | GD'nin FPS'i değil, botun analiz hızı |
| Level ID profilleri | ✓ | ✓ | level ID ekrandan okunamaz, elle girilir |
| Macro içe/dışa aktarma | ✓ dosya diyaloğu | ✓ SAF | aynı `.gdbm` formatı |
| Pause/Resume, Acil durdurma | ✓ (End) | ✓ (■ DUR) | çekirdekte test edildi |
| Bot durum göstergesi | GD üstünde etiket | panel + baloncuk + bildirim | |
| Kendi pencereleri ekran analizinden hariç | ✓ (WDA_EXCLUDEFROMCAPTURE, Win10 2004+) | ✗ panel görüntüye girer → AUTO PLAY'de baloncuğa küçülür | |
| APK | ✗ burada derlenemedi | | BUILD.md'deki komutlarla derlenir |

## Bilinen riskler (gerçek cihazda ilk kontrol edilecekler)

1. **Windows:** GD'nin `SendInput` Space tuşunu kabul etmesi (beklenen: evet), exclusive fullscreen'de siyah yakalama.
2. **Android:** `dispatchGesture` gecikmesi ve devam eden vuruşların (continueStroke) cihazdan cihaza davranışı;
   gecikme kayıt ve oynatmada aynı yoldan geçtiği için büyük kısmı birbirini götürür ama titreşim kalır.
3. Ekrandan senkron ±1 yakalama karesi titreşir: 60 Hz'de ~±8 ms. Kolay/orta levellerde yeterli, frame-perfect geçişlerde değil.
4. Çok kısa ölümler (ilk ~%1) çubuktan okunamaz; bunlar "başlangıç karesi eşleştirici" ile yakalanır.
