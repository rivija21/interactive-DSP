# Filter Lab for Windows

A C++ port of the macOS app in `../Sources/FilterLab`, with the same interface, features and maths.
`FilterLab.exe` in this folder is a single self-contained executable for Windows 10 and 11 (64-bit):
copy it anywhere and run it, no installer or extra DLLs needed.

## Building

```bat
build.bat
```

This needs CMake and either MinGW-w64 (g++ and Ninja on `PATH`) or Visual Studio 2022+ with the C++
workload. It writes `FilterLab.exe` into this folder. By hand:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The app icon is drawn at build time by `tools/make_icon.cpp` (the port of `scripts/make_icon.swift`).

## What changed for Windows

Only what the platform needs. The layout, colours, text, lessons, experiments and DSP are the same.

| macOS | Windows |
| --- | --- |
| AppKit views drawn with Core Graphics | Win32 window drawn with Direct2D + DirectWrite (`src/ui`) |
| SF Pro / New York / SF Mono | Segoe UI / Georgia (titles), Cambria (equations) / Consolas |
| SF Symbols | Vector icons drawn to match (`src/ui/Icons.cpp`) |
| AVAudioEngine output, AUHAL microphone | WASAPI output and capture (`src/audio`) |
| AVAudioFile, AVAudioConverter | Media Foundation decoding, windowed-sinc resampler |
| Accelerate (vDSP) FFT | Radix-2 FFT scaled to match vDSP (`src/ui/LiveViews.cpp`) |
| UserDefaults | `%APPDATA%\Filter Lab\settings.txt` |
| Menu bar, ⌘ shortcuts | Window menu bar, Ctrl shortcuts (below) |
| Open/save panels, NSAlert | Windows file dialogs and message boxes |
| Hold ⌥ to stop zeros snapping | Hold Alt |
| System Settings → Privacy → Microphone | Settings → Privacy & security → Microphone |

## Keys

| Key | Action |
| --- | --- |
| Space | Original / filtered (A/B) |
| L or Ctrl+L | Listen on/off |
| Ctrl+1 Ctrl+2 Ctrl+3 | Spectrum / spectrogram / scope |
| Ctrl+Alt+1 … Ctrl+Alt+5 | Magnitude / phase / group delay / impulse / step |
| Ctrl+Z, Ctrl+Shift+Z (or Ctrl+Y) | Undo / redo design changes |
| Delete | Remove the selected pole or zero |
| Mouse wheel or pinch on the z-plane | Zoom (double-click to reset) |
| Ctrl+O / Ctrl+E | Open an audio file / export filtered audio |
| Ctrl+Alt+M, Ctrl+Alt+Y, Ctrl+Alt+K | Copy the filter as MATLAB, Python, C |
| Ctrl+T | Show or hide the Theory panel |
| F11 | Full screen |
| F1 | Lessons |

Audio files can also be dragged onto the window, or passed on the command line (so "Open with"
from Explorer works).

## Source layout

- `src/dsp`: complex numbers, polynomial roots, elliptic functions, IIR/FIR design, biquads.
  Line-for-line ports of `Sources/FilterLab/DSP`.
- `src/audio`: the real-time engine (lock-free exchange, filter kernel, signal sources, music loop,
  WASAPI output and microphone, file decoding, resampling, WAV writing).
- `src/app`: app state, undo, persistence, lessons and experiments, code export.
- `src/ui`: the small view toolkit (`Widget`, `Graphics`, `Controls`) and the panels and plots.
- `src/platform`: main-thread dispatch, settings and file dialogs.
- `src/main.cpp`: the window, menus, keyboard shortcuts and message loop.

## Checking the maths against SciPy

`tests/dsp_check.cpp` prints 780 random designs and `tests/check_dsp.py` compares them with SciPy:

```bat
g++ -O2 -std=c++20 -Isrc tests/dsp_check.cpp src/dsp/*.cpp -o dsp_check.exe
dsp_check.exe > designs.json
python tests/check_dsp.py designs.json
```

Poles and zeros agree with SciPy to about 12 significant digits, and FIR taps to 2e-15.
