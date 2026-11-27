#!/bin/sh
# Baut das Windows-VST3-Bundle nach releases/windows/Limiter.vst3/.
# Aufruf: scripts/build-windows-vst3.sh [PFAD_ZU_LLVM_MINGW_BIN]
# llvm-mingw (Linux-hosted Cross-Compiler): https://github.com/mstorsjo/llvm-mingw/releases
# Alternativ funktioniert jeder x86_64-w64-mingw32-g++ (z. B. mingw-w64) im PATH.
set -e
cd "$(dirname "$0")/.."

if [ -n "$1" ]; then
    CXX="$1/x86_64-w64-mingw32-clang++"
elif command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    CXX=x86_64-w64-mingw32-g++
else
    echo "usage: $0 <llvm-mingw bin dir>" >&2
    exit 1
fi

OUT=releases/windows/Limiter.vst3/Contents/x86_64-win
mkdir -p "$OUT"

"$CXX" -O2 -std=c++17 -shared -fvisibility=hidden -Wall -Wextra \
    src/dsp/LimiterDsp.cpp \
    src/plugin/Parameters.cpp \
    src/plugin/LimiterPlugin.cpp \
    src/plugin/EditorView.cpp \
    src/plugin/PluginEntry.cpp \
    src/plugin/vst3/Vst3Mini.cpp \
    src/plugin/x11/X11Shim.cpp \
    -luser32 -lgdi32 \
    -o "$OUT/Limiter.vst3"

cat > releases/windows/Limiter.vst3/Contents/PkgInfo <<'PKGINFO'
<?xml version="1.0" encoding="utf-8"?>
<PkgInfo>
  <bundle id="com.vibe.limiter-test" name="Limiter" vendor="Vibe" category="Fx" version="1.0.0.0" sdkVersion="VST 3"/>
</PkgInfo>
PKGINFO

echo "built: $OUT/Limiter.vst3"
