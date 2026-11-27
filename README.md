# Limiter-test

VST3-Limiter-Plugin, komplett von Grund auf in diesem Repo entwickelt — ohne Code aus
anderen Repositories (kein JUCE, kein VST3-SDK, keine externen DSP-Bibliotheken).
Der komplette VST3-Protokoll-Layer (`src/plugin/vst3/`) und der Editor (Win32-GDI)
sind selbst geschrieben.

## Funktionsweise der DSP

`src/dsp/LimiterDsp.{h,cpp}` ist der eigentliche Limiter-Kern (reines C++17, keine
Abhängigkeiten):

- **True-Peak-Erkennung mit 8× Oversampling**: polyphasiges FIR-Filter (65 Taps,
  Blackman-Harris-Fenster, Grenzfrequenz = ursprüngliche Nyquistfrequenz), das
  Inter-Sample-Peaks zuverlässig erkennt.
- **Lookahead**: das Audiosignal läuft durch eine Delay-Line (Länge = Lookahead).
  Gleichzeitig wird über ein gleitendes Fenster (Monotonic-Deque, O(1) amortisiert)
  die nötige Gain-Reduction der nächsten L Samples bestimmt.
- **Attack/Release in dB-Domäne**: der Attack ist eine **lineare dB-Rampe** (60 dB
  Vollaussteuerung in Attack-Zeit), sodass die Reduktion exakt zum Peak im
  Lookahead-Fenster anliegt — true brickwall ohne Overshoot. Der Release ist
  exponentiell in dB mit **Auto-Release**: je tiefer die aktuelle Reduktion
  (bis 24 dB), desto länger die effektive Release-Zeit (bis 3×) — weniger Pumping.
- **Parameter-Smoothing**: Ceiling und Output-Gain werden per Sample mit 10 ms
  One-Pole geglättet (Zipper-Noise bei Automation wird so vermieden; Snap bei
  reset/activate).
- **Stereo-Link**: wahlweise unabhängige Kanäle oder gemeinsame Reduktion
  (Maximum aus L/R), damit das Stereobild nicht kippt.
- **Metering**: Gain-Reduction in dB und Ausgangs-Peak pro Block.

### Parameter und Defaults

| Parameter   | Bereich        | Default | Bedeutung                          |
|-------------|----------------|---------|------------------------------------|
| Ceiling     | -24 … 0 dB     | -1 dB   | hartes Ausgangs-Limit (True Peak)  |
| Lookahead   | 0.1 … 10 ms    | 5 ms    | Verzögerung + Analysefenster       |
| Attack      | 0.05 … 10 ms   | 1 ms    | Anstiegszeit der Reduktion (Ramp)  |
| Release     | 10 … 1000 ms   | 150 ms  | Abfallzeit der Reduktion           |
| True Peak   | On/Off         | On      | 8×-Oversampling aktiv              |
| Stereo Link | Off/Linked     | Linked  | gemeinsame L/R-Reduktion           |
| Output Gain | -24 … +24 dB   | 0 dB    | Makeup-Gain am Ausgang             |
| Bypass      | On/Off         | Off     | unbearbeitetes Signal durchlassen  |

## Aufbau

```
src/dsp/            Limiter-DSP-Kern (plattformunabhängig)
src/plugin/vst3/    minimaler, selbst geschriebener VST3-Protokoll-Layer
src/plugin/         Plugin (Factory, Processor, Controller), Parameter, Editor (Win32-GDI)
tests/              Unit-Tests für DSP und Plugin (eigener Mini-Testhost, kein externes Framework)
tools/render.cpp    Offline-Renderer: synthetisiert ein Signal, rendert es durch das Plugin, schreibt WAVs
```

## Build (Linux)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/limiter_render build   # schreibt limiter_input.wav / limiter_output.wav
```

Das Plugin-Bundle liegt danach unter `build/dist/Limiter.vst3`
(`Contents/x86_64-linux/Limiter.so` + `Contents/PkgInfo`).

## Build (Windows, VST3-Datei)

Cross-Build mit mingw-w64 (Toolchain-File liegt unter `cmake/`):

```sh
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j
```

Ergebnis: `build-win/dist/Limiter.vst3/Contents/x86_64-win/Limiter.vst3`.

Alternativ (z. B. ohne mingw) funktioniert der Build 1:1 mit dem Zig-Cross-Compiler:

```sh
zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -shared \
  src/dsp/LimiterDsp.cpp src/plugin/Parameters.cpp src/plugin/LimiterPlugin.cpp \
  src/plugin/EditorView.cpp src/plugin/PluginEntry.cpp src/plugin/vst3/Vst3Mini.cpp \
  -luser32 -lgdi32 -o releases/windows/Limiter.vst3/Contents/x86_64-win/Limiter.vst3
```

Ein fertiges Windows-Bundle liegt unter `releases/windows/Limiter.vst3/`
(in DAWs nach `C:\Program Files\Common Files\VST3` kopieren).
Neu bauen (z. B. mit [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases),
Linux-hosted Cross-Compiler):

```sh
scripts/build-windows-vst3.sh /pfad/zu/llvm-mingw-*-ucrt-ubuntu-22.04-x86_64/bin
```

## Editor

Der Editor (480×320, dunkles Design) zeigt oben Buttons (bypass / reset / stereo link /
true peak) und die Gain-Reduction in dB, darunter den orangenen GR-Verlauf mit dB-Skala
und Peak-Hold, in der Mitte die Ausgangs-Wellenform mit Ceiling-Linie und unten fünf
Drehregler (Ceiling, Lookahead, Attack, Release, Out Gain). Ziehen verändert die Werte
vertikal. Die UI ist plattformübergreifend über ein eigenes Painter-Interface
implementiert: Win32-GDI unter Windows, X11 (per `dlopen`, ohne Build-Dependency)
unter Linux.

## Tests

`limiter_tests` prüft u. a.: Passthrough & Latenz, Sine-Limiting auf das Ceiling,
Impuls-Brickwall, True-Peak-Erkennung von Inter-Sample-Peaks (8× Oversampling),
kein Fehlalarm bei Sinus, Release-Verlauf, Auto-Release bei tiefer GR, Parameter-
Smoothing, THD-Messung (Goertzel), Samplerate-Sweep (44.1k–192k), Stereo-Link,
Blockgrößen-Unabhängigkeit, Stabilität bei Stille/Denormals sowie den kompletten
VST3-Lebenszyklus (Factory → Component → Processor/Controller → process() inkl.
Automation mitten im Block mit Sample-Offsets → State-Save/Load → View).
