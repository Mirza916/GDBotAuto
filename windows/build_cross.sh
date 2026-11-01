#!/bin/sh
# Cross-compile GDAutoBot.exe from Linux/macOS with Zig (pip install ziglang).
# On Windows use CMake instead (see BUILD.md).
set -e
cd "$(dirname "$0")/.."
ZIG="python3 -m ziglang"
OUT=dist/windows
mkdir -p $OUT build-win
T=x86_64-windows-gnu
FLAGS="-std=c++17 -O2 -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -Icore/include -Iwindows/src"
$ZIG rc /fo build-win/app.res -- windows/resources/app.rc
for f in core/src/common.cpp core/src/macro.cpp core/src/vision.cpp core/src/pilot.cpp core/src/engine.cpp \
         windows/src/win_platform.cpp windows/src/main.cpp; do
  o=build-win/$(basename $f .cpp).o
  $ZIG c++ -target $T $FLAGS -c $f -o $o
done
$ZIG c++ -target $T -municode -Wl,--subsystem,windows -s build-win/*.o build-win/app.res -o $OUT/GDAutoBot.exe \
  -luser32 -lgdi32 -lwinmm -lshell32 -lcomdlg32 -ldwmapi
echo "built $OUT/GDAutoBot.exe"
rm -f $OUT/GDAutoBot.pdb
