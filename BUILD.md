# Derleme

## 1. Host testleri (Linux / macOS / WSL)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/gdbot_tests            # tam paket (~7 dk, simülasyon dahil)
./build/gdbot_tests --quick    # hızlı
```

JNI köprüsünü host JVM'de denemek (Android SDK gerekmez):

```sh
D=/tmp/jni && mkdir -p $D
javac -h $D/h -d $D/cls android/app/src/main/java/com/mirza/gdautobot/NativeBridge.java
JI=$(dirname $(dirname $(readlink -f $(which javac))))/include
g++ -std=c++17 -fPIC -shared -Icore/include -I$JI -I$JI/linux \
    -include $D/h/com_mirza_gdautobot_NativeBridge.h \
    android/app/src/main/cpp/jni_bridge.cpp core/src/*.cpp -o $D/libgdautobot.so
javac -cp $D/cls -d $D/cls tests/jni/JniSmoke.java
java -Djava.library.path=$D -cp $D/cls JniSmoke
```

## 2. Windows EXE

### a) Windows'ta (önerilen)

Gerekenler: Visual Studio 2022 ("Desktop development with C++") **veya** MSYS2 MinGW-w64, ve CMake ≥ 3.16.

```bat
cmake -B build-win -G "Visual Studio 17 2022" -A x64 -DGDBOT_TESTS=OFF
cmake --build build-win --config Release
:: çıktı: build-win\Release\GDAutoBot.exe
```

MinGW ile:

```sh
cmake -B build-win -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DGDBOT_TESTS=OFF
cmake --build build-win
```

### b) Linux/macOS'tan cross-compile (bu teslimattaki EXE böyle derlendi)

```sh
pip install ziglang
./windows/build_cross.sh       # -> dist/windows/GDAutoBot.exe (tek dosya, statik C++ runtime)
```

## 3. Android APK

Gerekenler: Android Studio (Koala veya yenisi), Android SDK 35, NDK (Android Studio'nun önerdiği sürüm),
CMake 3.22.1 (SDK Manager → SDK Tools), JDK 17.

**Önemli:** `android/app/src/main/cpp/CMakeLists.txt`, depo kökündeki `core/` klasörünü göreli yolla derler.
Bu yüzden `android/` klasörünü tek başına kopyalama, depoyu bütün olarak aç.

Android Studio: **File → Open → `GDAutoBot/android`** → Gradle sync → **Build → Build APK(s)**.

Komut satırı:

```sh
cd android
./gradlew assembleDebug        # app/build/outputs/apk/debug/app-debug.apk
./gradlew assembleRelease      # debug anahtarıyla imzalı, kurulabilir release APK
```

Windows'ta `gradlew.bat assembleDebug`. `local.properties` yoksa Android Studio oluşturur
ya da elle: `sdk.dir=C\:\\Users\\<sen>\\AppData\\Local\\Android\\Sdk`.

İlk derlemede Gradle; AGP 8.5.2, NDK ve CMake'i indirir (internet gerekir).

Telefona kurma: `adb install -r app/build/outputs/apk/debug/app-debug.apk`
