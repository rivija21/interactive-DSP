"""Checks every algorithm in fl_design.py and fl_realtime.py against SciPy / NumPy.

Run:  python3 check_all.py        (needs numpy and scipy)
"""
import math

import numpy as np
from scipy import signal

import fl_design as d
import fl_realtime as rt

np.seterr(all="ignore")   # the unstable-filter test overflows on purpose
rng = np.random.default_rng(7)
FTYPE = {"butterworth": "butter", "chebyshev1": "cheby1", "chebyshev2": "cheby2",
         "elliptic": "ellip", "bessel": "bessel"}


def match(a, b):
    """Largest distance between two root sets (order doesn't matter)."""
    a, b = list(a), list(b)
    if len(a) != len(b):
        return math.inf
    worst = 0.0
    for x in a:
        j = min(range(len(b)), key=lambda i: abs(b[i] - x))
        worst = max(worst, abs(b[j] - x) / max(1, abs(x)))
        b.pop(j)
    return worst


def check(name, ok, detail=""):
    print(f"{'PASS' if ok else 'FAIL'}  {name}  {detail}")
    return ok


results = []

# 1. IIR designs vs scipy.signal.iirfilter -------------------------------------------
worst = 0.0
for _ in range(200):
    fs = float(rng.choice([8000, 44100, 48000]))
    fam = str(rng.choice(list(FTYPE)))
    band = str(rng.choice(["lowpass", "highpass", "bandpass", "bandstop"]))
    order = int(rng.integers(1, 11))
    f1 = float(rng.uniform(0.01, 0.3) * fs / 2)
    f2 = min(f1 * float(rng.uniform(1.3, 3)), 0.45 * fs)
    rp, rs = float(rng.choice([0.1, 0.5, 1, 3])), float(rng.choice([20, 40, 60, 80]))
    z, p, k = d.design_iir(band, fam, order, f1, f2, rp, rs, fs)
    kw = dict(N=order, Wn=f1 if band in ("lowpass", "highpass") else [f1, f2], btype=band,
              ftype=FTYPE[fam], fs=fs, output="zpk")
    if fam in ("chebyshev1", "elliptic"):
        kw["rp"] = rp
    if fam in ("chebyshev2", "elliptic"):
        kw["rs"] = rs
    zs, ps, ks = signal.iirfilter(**kw)
    worst = max(worst, match(z, zs), match(p, ps), abs(k - ks) / abs(ks))
results.append(check("IIR zeros/poles/gain match SciPy (200 random designs)", worst < 1e-9, f"worst rel. error {worst:.1e}"))

# 2. SOS cascade response equals the zpk response ----------------------------------
z, p, k = d.design_iir("bandpass", "elliptic", 4, 500, 3000, 1, 60, 48000)
sos = d.zpk_to_sos(z, p, k)
f = np.geomspace(10, 23900, 500)
h_sos = d.sos_response(sos, f, 48000)
_, h_ref = signal.sosfreqz(signal.ellip(4, 1, 60, [500, 3000], "bandpass", fs=48000, output="sos"), worN=f, fs=48000)
err = np.max(np.abs(20 * np.log10(np.abs(h_sos)) - 20 * np.log10(np.abs(h_ref))))
results.append(check("biquad cascade |H| matches SciPy", err < 1e-6, f"max {err:.1e} dB"))

# 3. FIR window method vs scipy.signal.firwin ---------------------------------------
worst = 0.0
for window, sw in [("rectangular", "boxcar"), ("hann", "hann"), ("hamming", "hamming"),
                   ("blackman", "blackman"), ("kaiser", ("kaiser", 6.0))]:
    for band in ["lowpass", "highpass", "bandpass", "bandstop"]:
        taps = 63
        h = d.design_fir(band, taps, 1000, 4000, window, 6.0, 48000)
        cutoff = 1000 if band in ("lowpass", "highpass") else [1000, 4000]
        ref = signal.firwin(taps, cutoff, window=sw, pass_zero=band, fs=48000)
        worst = max(worst, np.max(np.abs(h - ref)))
results.append(check("FIR taps match scipy.signal.firwin (20 designs)", worst < 1e-12, f"max {worst:.1e}"))

# 4. Aberth roots vs numpy.roots, including a 255-tap FIR ---------------------------
h = d.design_fir("lowpass", 255, 1000, 0, "hamming", 0, 48000)
mine = d.pair_conjugates(d.polyroots(h))
err = match(mine, np.roots(h))
results.append(check("Aberth roots of a 255-tap FIR match numpy.roots", err < 1e-9, f"max {err:.1e}"))

# 5. Group delay formula vs a numerical derivative of the phase ------------------------
sos = d.zpk_to_sos(*d.design_iir("lowpass", "butterworth", 4, 1000, 0, 0, 0, 48000))
f = np.linspace(50, 5000, 50)
gd = d.sos_group_delay(sos, f, 48000)
df = 0.01
ph = np.unwrap(np.angle(d.sos_response(sos, np.concatenate([f - df, f + df]), 48000)).reshape(2, -1), axis=0)
gd_num = -(ph[1] - ph[0]) / (2 * np.pi * 2 * df / 48000)
results.append(check("group delay formula = -dphi/dw", np.max(np.abs(gd - gd_num)) < 1e-4,
                     f"at 1 kHz: {d.sos_group_delay(sos, [1000], 48000)[0]:.2f} samples"))

# 6. Real-time biquad loop vs scipy.signal.sosfilt, processed in blocks ------------------
x = rng.standard_normal(6000) * 0.2
cascade = rt.BiquadCascade(sos)
y = np.concatenate([cascade.process(x[i:i + 512])[0] for i in range(0, len(x), 512)])
results.append(check("block-by-block biquad cascade = sosfilt", np.max(np.abs(y - signal.sosfilt(sos, x))) < 1e-12))

# 7. FIR loop vs lfilter ---------------------------------------------------------------
h = d.design_fir("lowpass", 31, 2000, 0, "hamming", 0, 48000)
fir = rt.FIRFilter(h)
y = np.concatenate([fir.process(x[i:i + 300]) for i in range(0, 3000, 300)])
results.append(check("doubled-delay-line FIR = lfilter", np.max(np.abs(y - signal.lfilter(h, 1, x[:3000]))) < 1e-12))

# 8. The number from the app's audio test: 3 kHz sine through a 4th-order Butterworth at 1 kHz --
gen = rt.Generator(48000)
gen.freq = 3000
tone = gen.sine(48000, 3000)
cascade = rt.BiquadCascade(d.zpk_to_sos(*d.design_iir("lowpass", "butterworth", 4, 1000, 0, 0, 0, 48000)))
out, _ = cascade.process(tone)
measured = 20 * np.log10(np.std(out[4800:]) / np.std(tone[4800:]))
results.append(check("3 kHz through 1 kHz Butterworth (4th order)", abs(measured + 38.571) < 0.01, f"{measured:.3f} dB"))

# 9. Unstable filters are silenced -----------------------------------------------------
z, p, k, sos_bad = d.pole_zero_filter([-1], [1.04 * np.exp(0.5j), 1.04 * np.exp(-0.5j)], 48000)
y, ok = rt.BiquadCascade(sos_bad).process(gen.clicks(48000))
results.append(check("runaway filter is caught and muted", not ok and np.all(y == 0)))

# 10. Hum remover: notches at the mains frequencies -------------------------------------
z, p, k, sos_hum = d.pole_zero_filter(*d.preset_hum_remover(48000), 48000)
depth = 20 * np.log10(np.abs(d.sos_response(sos_hum, [50, 100, 250, 1000], 48000)))
results.append(check("hum remover: deep at 50/100/250 Hz, flat at 1 kHz",
                     depth[0] < -100 and depth[2] < -100 and abs(depth[3]) < 0.1, np.array2string(depth, precision=1)))

# 11. Spectrum scaling: a full-scale sine reads 0 dBFS ---------------------------------
n = 4096
frame = np.sin(2 * np.pi * (48000 / n * 100) * np.arange(n) / 48000)   # exactly on bin 100
peak_db = 10 * np.log10(rt.power_spectrum(frame).max())
results.append(check("full-scale sine reads 0 dBFS", abs(peak_db) < 1e-9, f"{peak_db:.2e} dB"))

# 12. Mic reader keeps pitch and level with drifting clocks -----------------------------
ring = rt.SampleRing(1 << 17)
reader = rt.MicReader(ring, 48000, 8000)
phase, t_mic, t_out, outs = 0.0, 0.0, 0.0, []
while t_out < 3:
    if t_mic <= t_out:
        block = 0.5 * np.sin(phase + 2 * np.pi * 1000 * np.arange(1, 481) / 48000)
        phase += 2 * np.pi * 1000 * 480 / 48000
        ring.write(block)
        t_mic += 480 / (48000 * 1.0003)                 # mic clock 0.03 % fast
    else:
        outs.append(reader.read(128))
        t_out += 128 / 8000
tail = np.concatenate(outs)[-16000:]
crossings = np.sum((tail[:-1] < 0) & (tail[1:] >= 0)) / 2
results.append(check("mic resampler 48 kHz -> 8 kHz keeps 1 kHz and level",
                     abs(crossings - 1000) <= 1 and abs(np.std(tail) - 0.5 / math.sqrt(2)) < 0.01,
                     f"{crossings:.0f} Hz, rms {np.std(tail):.4f}"))

# 13. Exchange hands over without blocking ------------------------------------------------
ex = rt.Exchange()
current = "old kernel"
ex.publish("new kernel")
current = ex.take(current)
results.append(check("lock-free hand-off swaps in the new kernel", current == "new kernel" and ex.retired == "old kernel"))

print(f"\n{sum(results)}/{len(results)} checks passed")
