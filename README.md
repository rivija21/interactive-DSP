# Filter Lab

A digital-filter design lab for macOS. Design a filter, see its poles, zeros and frequency
response, and hear it live on music, test signals, your own audio files or your microphone.
The theory panel explains what you're looking at and gives you MATLAB, SciPy or C code for
your coursework.

## What you can do

- **Design IIR filters**: Butterworth, Chebyshev I, Chebyshev II, Elliptic and Bessel, as
  low-pass, high-pass, band-pass or band-stop, up to order 12. The design follows the same steps
  as `scipy.signal.iirfilter` / MATLAB's `butter()`: analog prototype, then band transform, then
  bilinear transform with pre-warping.
- **Design FIR filters** with the window method (rectangular, Hann, Hamming, Blackman, Kaiser),
  up to 255 taps, like `scipy.signal.firwin` / MATLAB's `fir1`.
- **Place poles and zeros by hand** in the z-plane: drag them, add them with + Zero / + Pole,
  delete them with a right-click. Presets: resonator, notch, 50 Hz mains-hum remover, comb
  filter, all-pass, moving average and DC blocker.
- **Drag a designed filter's poles** to turn it into a hand-edited one, and see what happens.
  For example, push a pole outside the unit circle and watch the impulse response blow up.
- **Hear it**: turn on Listen and press **Space** to switch between the original and filtered
  sound.
- **See it live**: spectrum (input vs output, with an optional "predicted" overlay), a scrolling
  spectrogram, and a triggered oscilloscope.
- **Learn**: 8 short lessons (the z-plane, stability, filter families, the bilinear transform, FIR
  windows, phase and group delay, the displays, sampling) and 12 one-click experiments.
- **Export**: copy MATLAB / Python / C code (Edit menu or the Theory panel), save any plot as a
  white-background PNG for a lab report, or save the filtered audio as a WAV file.

## Sources

Music loop (synthesized), music + 50 Hz hum, sine, square, 8-second log sweep, white and
pink noise, clicks (impulses), microphone and audio files (open with ⌘O or drag one onto the
window).

The microphone path has an anti-aliasing filter when you pick a sample rate below the mic's own
rate. **Use headphones** when listening to the microphone, or the speakers will feed back into
the mic.

## Keys

| Key | Action |
| --- | --- |
| Space | Original / filtered (A/B) |
| L or ⌘L | Listen on/off |
| ⌘1 ⌘2 ⌘3 | Spectrum / spectrogram / scope |
| ⌥⌘1 … ⌥⌘5 | Magnitude / phase / group delay / impulse / step |
| ⌘Z, ⇧⌘Z | Undo / redo design changes |
| Delete | Remove the selected pole or zero |
| Scroll or pinch on the z-plane | Zoom (double-click to reset) |
| ⌘O / ⌘E | Open an audio file / export filtered audio |
| ⌥⌘M, ⌥⌘Y, ⌥⌘K | Copy the filter as MATLAB, Python, C |

## Building

```bash
./build.sh
```

This needs only the Xcode Command Line Tools and produces `Filter Lab.app` in this folder.

## How it works

- `DSP/`: complex numbers, polynomial roots (Aberth–Ehrlich), Jacobi elliptic functions, the
  IIR and FIR designs, and second-order-section conversion. These were checked against SciPy over
  780 random designs: poles, zeros and gain agree to about 12 digits, and |H| to within 1e-9 dB.
- `Audio/`: a real-time engine (AVAudioEngine output, plus a raw AUHAL unit for the mic). Filters
  run in double precision as cascaded biquads or a direct-form FIR. New designs reach the audio
  thread without locks blocking it or memory being freed there.
- `UI/`: the plots (z-plane with an |H(z)| map, response, spectrum, spectrogram, scope), the
  controls and the theory panel.
- `App/`: the app state, lessons and experiments, code and figure export, and the menus.

## Studying the DSP in Python

`study/` has plain NumPy versions of every algorithm, for reading without Swift: `fl_design.py`
(filter design) and `fl_realtime.py` (the audio path and analysers). `python3 study/check_all.py`
runs 13 checks against SciPy. The companion study guide is [docs/STUDY_GUIDE.md](docs/STUDY_GUIDE.md).
