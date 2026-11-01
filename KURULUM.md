# Kurulum ve kullanım

## Windows 10/11

1. `GDAutoBot.exe`'yi kendi klasörüne koy (yanına `profiles\`, `macros\`, `gdautobot.cfg` oluşturur).
2. Geometry Dash 2.209'u **pencereli** veya **kenarlıksız tam ekran** aç. Exclusive fullscreen'de ekran yakalama siyah dönebilir.
   GD ayarlarında **Show Progress Bar açık** olsun (attempt senkronu bunu kullanır).
3. EXE'yi aç. "GD PENCERESİ" alanında çözünürlük görünmeli.
4. **Kalibrasyon** (fare GD penceresinin içindeyken):
   - Bir levelde oyuncu ikonunun **ana renginin** üstüne fareyi getir → **F7**
   - İlerleme çubuğunun **iç kısmının** sol ucu → **Ctrl+F7**, sağ ucu → **Ctrl+F8**
   - (Auto Retry için) ölüm ekranındaki Retry düğmesi → **Ctrl+F9**
5. Level ID'yi yaz → **Profili kaydet**. Ayarlar ve macro o level'a bağlanır.

### Kısayollar (genel, GD odaktayken de çalışır)

| Tuş | İş |
|---|---|
| F8 | AUTO PLAY aç/kapat |
| F6 | SMART ↔ MACRO |
| F9 | Macro kaydı başlat/bitir (bitince `macros\<level>.gdbm` olarak kaydeder ve yükler) |
| F5 | Elle SYNC (attempt "şimdi başladı") |
| F10 | Pause / Resume |
| **End** | **ACİL DURDURMA** (tüm tuşları bırakır, botu kapatır) |
| F2 / F3 / F4 / Shift+F4 | Oyun modu / hız / yerçekimi / mini |

Bot, tuşu yalnızca GD penceresi **odaktayken** basar. Bot açıkken klavyeden/fareden zıplarsan bot anında manuele geçer
(ayardan kapatılabilir). Tuşlar `keys.ini` ile değiştirilebilir:

```ini
p1=32          ; Space (sanal tuş kodu)
p2=38          ; Up arrow
twoPlayer=0    ; 1 = Up oku 2. oyuncu olarak kaydedilsin
captureWidth=400
```

### Macro akışı (PC)

1. Level'ı aç (normal mod). **F9** → oyna. Her ölümde yeni deneme başlar; en yüksek % alan deneme saklanır.
2. Level'ı bitirince **F9** → kaydedildi.
3. **F6** ile MACRO, **F8** ile AUTO PLAY ON. Bot bir sonraki attempt başlangıcını (çubuğun %0'a dönmesi) bekler ve oynatır.
4. Kaçıyorsa **Timing Offset** ile ±1 ms ayarla.

## Android 10+

1. APK'yı kur (bkz. BUILD.md). Android 13+'ta dışarıdan yüklenen uygulamalarda Erişilebilirlik hizmeti gri olabilir:
   **Ayarlar → Uygulamalar → GD AutoBot → ⋮ → "Kısıtlanmış ayarlara izin ver"**.
2. Uygulamada sırayla:
   - **1. Üstte gösterme izni** (panel ve kayıt pedi için)
   - **2. Erişilebilirlik → GD AutoBot → Aç** (dokunma hareketleri için; ekran içeriğini okumaz)
   - **3. BOTU BAŞLAT** → Android'in ekran kaydı onayı → "Tüm ekran" seç (veya Android 14+'ta Geometry Dash uygulamasını seç)
3. **Geometry Dash'i aç**. Mor-sarı panel oyunun üstünde. Panel, üst çubuğundan sürüklenir; "—" ile küçük baloncuğa döner.
4. **Kalibrasyon**: panelde **⌖** → sarı artıyı oyuncu ikonunun ortasına sürükle → **Oyuncu**. Aynı şekilde
   **Çubuk sol / Çubuk sağ**, **Retry**, **Checkpoint** (practice), **Dokunma** (botun dokunacağı yer; panelin ve pedin dışında olmalı).
5. **AUTO PLAY** açılınca panel otomatik olarak köşedeki baloncuğa küçülür (analiz edilen ekranı kapatmasın diye).

### Macro akışı (Android)

1. Panelde **● KAYIT** → sol altta **KAYIT PEDİ** çıkar. Oyunu **pede dokunarak** oyna: ped dokunuşu kaydeder ve
   aynı anda oyuna (Dokunma noktasına) iletir.
2. Bitince **■ KAYIT** → `Level <id>` macro'su kaydedilir. **MACRO** → **AUTO PLAY: ON**.
3. Uygulama ana ekranından macro'yu **dışa/içe aktarabilirsin** (`.gdbm`, PC ile aynı format).

## Smart Auto Play ipuçları

- Mod/hız/yerçekimi **otomatik algılanmaz**: portal geçerken panelden (Android) ya da F2/F3/F4 (PC) ile değiştir.
- Önce kolay, sade arka planlı levellerde dene. Smart mod "Karar:" satırında neden zıpladığını yazar.
- Erken zıplıyorsa **Input Delay**'i düşür, geç zıplıyorsa artır.
