"""Compares the C++ filter designs (from dsp_check.exe) with SciPy.

Usage: dsp_check.exe > designs.json && python check_dsp.py designs.json
"""
import json
import sys

import numpy as np
from scipy import signal

FAMILY = {"butterworth": "butter", "chebyshev1": "cheby1", "chebyshev2": "cheby2", "elliptic": "ellip", "bessel": "bessel"}
WINDOW = {"rectangular": "boxcar", "hann": "hann", "hamming": "hamming", "blackman": "blackman"}


def match(a, b):
    """Largest distance between two root sets after greedy matching (relative to magnitude)."""
    a, b = list(a), list(b)
    if len(a) != len(b):
        return np.inf
    worst = 0.0
    for r in a:
        d = [abs(r - s) / max(1, abs(r)) for s in b]
        i = int(np.argmin(d))
        worst = max(worst, d[i])
        b.pop(i)
    return worst


def main(path):
    designs = json.load(open(path))
    worst_root, worst_k, worst_h, worst_db, worst_fir = 0.0, 0.0, 0.0, 0.0, 0.0
    fails = 0
    for d in designs:
        fs = d["fs"]
        if d["method"] == "fir":
            n = d["taps"]
            wn = [d["f1"], d["f2"]] if d["band"] in ("bandpass", "bandstop") else d["f1"]
            win = ("kaiser", d["beta"]) if d["window"] == "kaiser" else WINDOW[d["window"]]
            h = signal.firwin(n, wn, window=win, pass_zero=d["band"], fs=fs)
            err = float(np.max(np.abs(h - np.array(d["h"]))))
            worst_fir = max(worst_fir, err)
            if err > 1e-12:
                fails += 1
                print("FIR mismatch", err, d["band"], d["window"], n)
            continue
        wn = [d["f1"], d["f2"]] if d["band"] in ("bandpass", "bandstop") else d["f1"]
        kw = dict(btype=d["band"], ftype=FAMILY[d["family"]], fs=fs, output="zpk")
        if d["family"] in ("chebyshev1", "elliptic"):
            kw["rp"] = d["rp"]
        if d["family"] in ("chebyshev2", "elliptic"):
            kw["rs"] = d["rs"]
        z, p, k = signal.iirfilter(d["order"], wn, **kw)
        cz = [complex(*r) for r in d["z"]]
        cp = [complex(*r) for r in d["p"]]
        er = max(match(z, cz), match(p, cp))
        ek = abs(k - d["k"]) / max(abs(k), 1e-300)
        worst_root = max(worst_root, er)
        worst_k = max(worst_k, ek)
        # |H| from the C++ SOS against SciPy's zpk, in dB.
        sos = np.array(d["sos"])
        w, h1 = signal.sosfreqz(sos, worN=512, fs=fs)
        _, h2 = signal.freqz_zpk(z, p, k, worN=512, fs=fs)
        db1 = 20 * np.log10(np.maximum(np.abs(h1), 1e-12))
        db2 = 20 * np.log10(np.maximum(np.abs(h2), 1e-12))
        mask = db2 > -200
        edb = float(np.max(np.abs(db1[mask] - db2[mask]))) if mask.any() else 0.0
        worst_db = max(worst_db, edb)
        if er > 1e-6 or ek > 1e-6 or edb > 1e-6:
            fails += 1
            print("IIR mismatch", d["family"], d["band"], d["order"], "roots", er, "k", ek, "dB", edb)
    print(f"{len(designs)} designs, {fails} mismatches")
    print(f"worst root error {worst_root:.2e}, gain {worst_k:.2e}, |H| {worst_db:.2e} dB, FIR taps {worst_fir:.2e}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
