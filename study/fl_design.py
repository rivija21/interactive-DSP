"""Filter Lab's filter-design maths in plain Python + NumPy.

Each function mirrors one in Sources/FilterLab/DSP/*.swift, so you can study the
algorithms without reading Swift. check_all.py tests every one of them against SciPy.

Conventions (same as SciPy):
  * zeros z, poles p, gain k describe  H = k * prod(x - z) / prod(x - p)
  * polynomials are coefficient lists in DESCENDING powers: [c0, c1, c2] = c0*x^2 + c1*x + c2
"""
import cmath
import math

import numpy as np

# ---------------------------------------------------------------------------
# 1. Analog low-pass prototypes, cutoff = 1 rad/s           (DSP/IIRDesign.swift)
# ---------------------------------------------------------------------------


def buttap(n):
    """Butterworth: n poles evenly spaced on the left half of the unit circle."""
    m = np.arange(-n + 1, n, 2)                      # -n+1, -n+3, ..., n-1
    p = -np.exp(1j * np.pi * m / (2 * n))
    return np.array([]), p, 1.0


def cheb1ap(n, rp):
    """Chebyshev I: ripple of rp dB in the passband; poles on an ellipse."""
    eps = math.sqrt(10 ** (0.1 * rp) - 1)           # ripple factor
    mu = math.asinh(1 / eps) / n
    m = np.arange(-n + 1, n, 2)
    p = -np.sinh(mu + 1j * np.pi * m / (2 * n))
    k = np.prod(-p).real
    if n % 2 == 0:                                   # even order: DC sits at the ripple's bottom
        k /= math.sqrt(1 + eps * eps)
    return np.array([]), p, k


def cheb2ap(n, rs):
    """Chebyshev II: flat passband, stopband ripple stays rs dB down; zeros on the j-axis."""
    de = 1 / math.sqrt(10 ** (0.1 * rs) - 1)
    mu = math.asinh(1 / de) / n
    if n % 2:
        m = np.concatenate([np.arange(-n + 1, 0, 2), np.arange(2, n, 2)])
    else:
        m = np.arange(-n + 1, n, 2)
    z = 1j / np.sin(m * np.pi / (2 * n))             # purely imaginary zeros
    b = -np.exp(1j * np.pi * np.arange(-n + 1, n, 2) / (2 * n))
    p = 1 / (math.sinh(mu) * b.real + 1j * math.cosh(mu) * b.imag)
    k = (np.prod(-p) / np.prod(-z)).real
    return z, p, k


# --- Jacobi elliptic machinery for the elliptic (Cauer) filter (DSP/Elliptic.swift) ---

def agm(a, b):
    """Arithmetic-geometric mean: repeat a, b = (a+b)/2, sqrt(a*b) until they agree."""
    for _ in range(64):
        a, b = (a + b) / 2, math.sqrt(a * b)
        if abs(a - b) <= 1e-16 * a:
            break
    return a


def ellipk(m):
    """Complete elliptic integral of the first kind K(m), m = modulus squared."""
    return math.pi / (2 * agm(1, math.sqrt(1 - m)))


def ellipkm1(p):
    """K(1 - p), computed so it stays accurate when p is tiny."""
    return math.pi / (2 * agm(1, math.sqrt(p)))


def ellipj(u, m):
    """Jacobi elliptic functions sn, cn, dn (Cephes algorithm: AGM then back-substitution)."""
    if m < 1e-9:                                     # nearly circular functions
        t, b = math.sin(u), math.cos(u)
        ai = 0.25 * m * (u - t * b)
        return t - ai * b, b + ai * t, 1 - 0.5 * m * t * t
    if m >= 0.9999999999:                            # nearly hyperbolic functions
        ai = 0.25 * (1 - m)
        b, t = math.cosh(u), math.tanh(u)
        phi = 1 / b
        twon = b * math.sinh(u)
        sn = t + ai * (twon - u) / (b * b)
        ai *= t * phi
        return sn, phi - ai * (twon - u), phi + ai * (twon + u)
    a = [1.0] + [0.0] * 8
    c = [math.sqrt(m)] + [0.0] * 8
    b = math.sqrt(1 - m)
    twon, i = 1.0, 0
    while abs(c[i] / a[i]) > 1.11e-16 and i <= 7:    # descending AGM
        ai = a[i]
        i += 1
        c[i] = (ai - b) / 2
        a[i], b = (ai + b) / 2, math.sqrt(ai * b)
        twon *= 2
    phi = twon * a[i] * u
    while i > 0:                                     # walk back up the AGM ladder
        last = phi
        phi = (math.asin(c[i] * math.sin(phi) / a[i]) + phi) / 2
        i -= 1
    sn, cn = math.sin(phi), math.cos(phi)
    dnfix = cn / math.cos(phi - last)
    dn = math.sqrt(1 - m * sn * sn) if abs(dnfix) < 0.1 else dnfix
    return sn, cn, dn


def ellipdeg(n, m1):
    """Solve the 'degree equation' for the modulus m (nome series)."""
    q1 = math.exp(-math.pi * ellipkm1(m1) / ellipk(m1))
    q = q1 ** (1 / n)
    num = sum(q ** (k * (k + 1)) for k in range(8))
    den = 1 + 2 * sum(q ** (k * k) for k in range(1, 9))
    return 16 * q * (num / den) ** 4


def arc_jac_sn(w, m):
    """Inverse Jacobi sn for complex w, by the descending Landen transformation."""
    def complement(kx):
        return cmath.sqrt((1 - kx) * (1 + kx))
    ks = [math.sqrt(m)]
    while ks[-1] != 0 and len(ks) < 13:
        kp = complement(ks[-1]).real
        ks.append((1 - kp) / (1 + kp))
    big_k = math.prod(1 + k for k in ks[1:]) * math.pi / 2
    wn = w
    for kn, knext in zip(ks[:-1], ks[1:]):
        wn = 2 * wn / ((1 + knext) * (1 + complement(kn * wn)))
    return big_k * (2 / math.pi) * cmath.asin(wn)


def ellipap(n, rp, rs):
    """Elliptic (Cauer): ripple in both bands -> the steepest possible transition."""
    if n == 1:
        p = -math.sqrt(1 / (10 ** (0.1 * rp) - 1))
        return np.array([]), np.array([p + 0j]), -p
    eps_sq = 10 ** (0.1 * rp) - 1
    ck1_sq = eps_sq / (10 ** (0.1 * rs) - 1)
    m = ellipdeg(n, ck1_sq)
    capk = ellipk(m)
    js = np.arange(1 - n % 2, n, 2)
    sncndn = [ellipj(j * capk / n, m) for j in js]
    z = np.array([1j / (math.sqrt(m) * sn) for sn, _, _ in sncndn if abs(sn) > 2e-16])
    z = np.concatenate([z, z.conj()])
    r = arc_jac_sn(1j / math.sqrt(eps_sq), ck1_sq).imag
    v0 = capk * r / (n * ellipk(ck1_sq))
    sv, cv, dv = ellipj(v0, 1 - m)
    p = np.array([-(cn * dn * sv * cv + 1j * sn * dv) / (1 - (dn * sv) ** 2) for sn, cn, dn in sncndn])
    if n % 2:
        scale = math.sqrt(np.sum(np.abs(p) ** 2))
        p = np.concatenate([p, p[np.abs(p.imag) > 2e-16 * scale].conj()])
    else:
        p = np.concatenate([p, p.conj()])
    k = (np.prod(-p) / np.prod(-z)).real
    if n % 2 == 0:
        k /= math.sqrt(1 + eps_sq)
    return z, p, k


def besselap(n):
    """Bessel-Thomson: poles = roots of the reverse Bessel polynomial, 'phase' normalised."""
    f = math.factorial
    # theta_n(s) = sum a_k s^k with a_k = (2n-k)! / (2^(n-k) k! (n-k)!)   (descending order)
    coeffs = [f(2 * n - k) / (2 ** (n - k) * f(k) * f(n - k)) for k in range(n, -1, -1)]
    p = polyroots(coeffs)
    p = p * coeffs[-1] ** (-1 / n)                    # scale so prod|p| = 1 -> gain 1
    return np.array([]), p, 1.0


# ---------------------------------------------------------------------------
# 2. Frequency transformations in the s-plane
# ---------------------------------------------------------------------------


def lp2lp(z, p, k, wo):
    """s -> s/wo : move the cutoff from 1 rad/s to wo."""
    degree = len(p) - len(z)
    return z * wo, p * wo, k * wo ** degree


def lp2hp(z, p, k, wo):
    """s -> wo/s : low-pass becomes high-pass; zeros appear at s = 0."""
    degree = len(p) - len(z)
    k_hp = k * (np.prod(-z) / np.prod(-p)).real
    return np.concatenate([wo / z, np.zeros(degree)]), wo / p, k_hp


def lp2bp(z, p, k, wo, bw):
    """s -> (s^2 + wo^2) / (bw*s) : each root splits into two."""
    degree = len(p) - len(z)

    def split(r):
        r = r * bw / 2
        root = np.sqrt(r * r - wo * wo + 0j)
        return np.concatenate([r + root, r - root])
    return np.concatenate([split(z), np.zeros(degree)]), split(p), k * bw ** degree


def lp2bs(z, p, k, wo, bw):
    """s -> bw*s / (s^2 + wo^2) : band-stop; zeros land at +-j*wo."""
    degree = len(p) - len(z)

    def split(r):
        r = (bw / 2) / r
        root = np.sqrt(r * r - wo * wo + 0j)
        return np.concatenate([r + root, r - root])
    z_bs = np.concatenate([split(z), np.full(degree, 1j * wo), np.full(degree, -1j * wo)])
    return z_bs, split(p), k * (np.prod(-z) / np.prod(-p)).real


def bilinear(z, p, k, fs):
    """s = 2 fs (z-1)/(z+1): analog (s-plane) -> digital (z-plane)."""
    degree = len(p) - len(z)
    fs2 = 2 * fs
    z_d = np.concatenate([(fs2 + z) / (fs2 - z), -np.ones(degree)])   # s = infinity -> z = -1
    p_d = (fs2 + p) / (fs2 - p)
    k_d = k * (np.prod(fs2 - z) / np.prod(fs2 - p)).real
    return z_d, p_d, k_d


def prewarp(f, fs):
    """Analog frequency (rad/s) that the bilinear transform maps exactly onto f Hz."""
    return 2 * fs * math.tan(math.pi * f / fs)


PROTOTYPES = {
    "butterworth": lambda n, rp, rs: buttap(n),
    "chebyshev1": lambda n, rp, rs: cheb1ap(n, rp),
    "chebyshev2": lambda n, rp, rs: cheb2ap(n, rs),
    "elliptic": lambda n, rp, rs: ellipap(n, rp, rs),
    "bessel": lambda n, rp, rs: besselap(n),
}


def design_iir(band, family, order, f1, f2, rp, rs, fs):
    """The whole IIR design: prototype -> band transform -> bilinear. Returns digital (z, p, k)."""
    z, p, k = PROTOTYPES[family](order, rp, rs)
    if band == "lowpass":
        z, p, k = lp2lp(z, p, k, prewarp(f1, fs))
    elif band == "highpass":
        z, p, k = lp2hp(z, p, k, prewarp(f1, fs))
    else:
        w1, w2 = prewarp(f1, fs), prewarp(f2, fs)
        transform = lp2bp if band == "bandpass" else lp2bs
        z, p, k = transform(z, p, k, math.sqrt(w1 * w2), w2 - w1)
    return bilinear(z, p, k, fs)


# ---------------------------------------------------------------------------
# 3. FIR design: the window method                           (DSP/FIRDesign.swift)
# ---------------------------------------------------------------------------


def make_window(kind, n, beta=8.6):
    """Symmetric windows of length n."""
    if n == 1:
        return np.ones(1)
    x = np.arange(n) / (n - 1)
    if kind == "rectangular":
        return np.ones(n)
    if kind == "hann":
        return 0.5 - 0.5 * np.cos(2 * np.pi * x)
    if kind == "hamming":
        return 0.54 - 0.46 * np.cos(2 * np.pi * x)
    if kind == "blackman":
        return 0.42 - 0.5 * np.cos(2 * np.pi * x) + 0.08 * np.cos(4 * np.pi * x)
    if kind == "kaiser":
        r = 2 * x - 1
        return np.i0(beta * np.sqrt(np.maximum(0, 1 - r * r))) / np.i0(beta)
    raise ValueError(kind)


def design_fir(band, taps, f1, f2, window, beta, fs):
    """Windowed sinc: ideal (infinitely long) impulse response, cut to `taps` and smoothed."""
    nyq = fs / 2
    c1, c2 = f1 / nyq, f2 / nyq                       # cutoffs as fractions of Nyquist
    bands = {"lowpass": [(0, c1)], "highpass": [(c1, 1)],
             "bandpass": [(c1, c2)], "bandstop": [(0, c1), (c2, 1)]}[band]
    m = np.arange(taps) - (taps - 1) / 2              # time index centred on 0
    h = sum(right * np.sinc(right * m) - left * np.sinc(left * m) for left, right in bands)
    h = h * make_window(window, taps, beta)
    left, right = bands[0]                            # scale: exactly 0 dB in the first passband
    f0 = 0 if left == 0 else (1 if right == 1 else (left + right) / 2)
    return h / np.sum(h * np.cos(np.pi * m * f0))


# ---------------------------------------------------------------------------
# 4. Polynomial roots: Aberth-Ehrlich                         (DSP/Polynomial.swift)
# ---------------------------------------------------------------------------


def polyroots(coeffs, iterations=600):
    """All roots of a polynomial (descending coefficients), found simultaneously."""
    c = np.array(coeffs, dtype=complex)
    c = np.trim_zeros(c, "f")
    origin = len(c) - len(np.trim_zeros(c, "b"))      # trailing zeros = roots at 0
    c = np.trim_zeros(c, "b") / c[0]                  # make it monic
    n = len(c) - 1
    if n < 1:
        return np.zeros(origin, complex)
    dc = c[:-1] * np.arange(n, 0, -1)                 # derivative
    rc, rdc = c[::-1], c[::-1][:-1] * np.arange(n, 0, -1)   # reversed polynomial and its derivative

    def newton_ratio(x):
        """p(x)/p'(x), computed with the reversed polynomial when |x| > 1 so x^n can't overflow."""
        if abs(x) <= 1:
            return np.polyval(c, x) / np.polyval(dc, x)
        w = 1 / x
        return x / (n - w * np.polyval(rdc, w) / np.polyval(rc, w))

    radius = max(1e-6, abs(c[-1]) ** (1 / n))         # geometric mean of the root sizes
    x = radius * np.exp(1j * (2 * np.pi * np.arange(n) / n + 0.4))
    for _ in range(iterations):
        biggest = 0.0
        for i in range(n):
            ratio = newton_ratio(x[i])
            repel = np.sum(1 / (x[i] - np.delete(x, i)))   # push away from the other estimates
            step = ratio / (1 - ratio * repel)
            x[i] -= step
            biggest = max(biggest, abs(step) / max(1, abs(x[i])))
        if biggest < 1e-14:
            break
    return np.concatenate([np.zeros(origin, complex), x])


def pair_conjugates(roots, tol=1e-8):
    """Snap near-real roots to the real axis and make each complex root's partner its exact conjugate."""
    out, upper, lower = [], [], []
    for r in roots:
        if abs(r.imag) <= tol * max(1, abs(r)):
            out.append(complex(r.real, 0))
        elif r.imag > 0:
            upper.append(r)
        else:
            lower.append(r)
    while upper and lower:
        u = upper.pop()
        j = min(range(len(lower)), key=lambda i: abs(lower[i] - u.conjugate()))
        l = lower.pop(j)
        avg = complex((u.real + l.real) / 2, (u.imag - l.imag) / 2)
        out += [avg, avg.conjugate()]
    return np.array(out + upper + lower)


# ---------------------------------------------------------------------------
# 5. ZPK -> second-order sections                             (DSP/Filter.swift)
# ---------------------------------------------------------------------------


def zpk_to_sos(z, p, k):
    """Pair poles with nearby zeros into biquads [b0, b1, b2, 1, a1, a2].

    Poles closest to the unit circle (the 'loudest') are handled first, each paired
    with the zeros nearest to it. The overall gain k goes into the first section.
    """
    def split(roots):
        pairs = [r for r in roots if abs(r.imag) > 1e-9 * max(1, abs(r)) and r.imag > 0]
        reals = [r.real for r in roots if abs(r.imag) <= 1e-9 * max(1, abs(r))]
        return pairs, reals

    p_pairs, p_reals = split(p)
    z_pairs, z_reals = split(z)

    def take_zeros(target):
        best_pair = min(z_pairs, key=lambda q: abs(q - target), default=None)
        best_real = min(z_reals, key=lambda q: abs(q - target), default=None)
        if best_pair is not None and (best_real is None or abs(best_pair - target) <= abs(best_real - target)):
            z_pairs.remove(best_pair)
            return [best_pair, best_pair.conjugate()]
        if best_real is not None:
            z_reals.remove(best_real)
            if z_reals:
                second = min(z_reals, key=lambda q: abs(q - target.real))
                z_reals.remove(second)
                return [best_real, second]
            return [best_real]
        return []

    def section(zeros, poles):
        b = list(np.real(np.poly(zeros))) if zeros else [1.0]
        a = list(np.real(np.poly(poles))) if poles else [1.0]
        b += [0.0] * (3 - len(b))
        a += [0.0] * (3 - len(a))
        return [b[0], b[1], b[2], 1.0, a[1], a[2]]

    sos = []
    while p_pairs or p_reals:
        best_pair = min(p_pairs, key=lambda q: abs(1 - abs(q)), default=None)
        best_real = min(p_reals, key=lambda q: abs(1 - abs(q)), default=None)
        if best_pair is not None and (best_real is None or abs(1 - abs(best_pair)) <= abs(1 - abs(best_real))):
            p_pairs.remove(best_pair)
            poles = [best_pair, best_pair.conjugate()]
        else:
            p_reals.remove(best_real)
            poles = [complex(best_real)]
            if p_reals:
                second = min(p_reals, key=lambda q: abs(1 - abs(q)))
                p_reals.remove(second)
                poles.append(complex(second))
        zeros = take_zeros(poles[0])
        if len(poles) == 1 and len(zeros) == 2 and abs(zeros[0].imag) > 1e-12 and z_reals:
            z_pairs.append(zeros[0] if zeros[0].imag > 0 else zeros[1])   # keep the pair for later
            zeros = [z_reals.pop(0)]
        sos.append(section(zeros, poles))
    while z_pairs or z_reals:                            # more zeros than poles: FIR-like sections
        sos.append(section(take_zeros(1 + 0j), []))
    if not sos:
        sos.append([1.0, 0, 0, 1.0, 0, 0])
    sos = np.array(sos)
    sos[0, :3] *= k
    return sos


# ---------------------------------------------------------------------------
# 6. Looking at a filter: response, group delay, impulse      (DSP/Filter.swift)
# ---------------------------------------------------------------------------


def sos_response(sos, f, fs):
    """H(e^jw) of a biquad cascade at frequencies f (Hz)."""
    zi = np.exp(-2j * np.pi * np.asarray(f, float) / fs)     # z^-1 on the unit circle
    h = np.ones_like(zi)
    for b0, b1, b2, _, a1, a2 in sos:
        h *= (b0 + b1 * zi + b2 * zi ** 2) / (1 + a1 * zi + a2 * zi ** 2)
    return h


def fir_response(h, f, fs):
    zi = np.exp(-2j * np.pi * np.asarray(f, float) / fs)
    return np.polyval(h[::-1], zi)                        # sum h[k] z^-k


def poly_group_delay(b, w):
    """Group delay of B(z) = sum b_k z^-k:  Re{ sum k b_k e^-jwk / sum b_k e^-jwk }."""
    k = np.arange(len(b))
    e = np.exp(-1j * np.outer(w, k))
    return np.real((e @ (k * np.asarray(b))) / (e @ np.asarray(b)))


def sos_group_delay(sos, f, fs):
    """Samples of delay per frequency: numerators add delay, denominators subtract it."""
    w = 2 * np.pi * np.asarray(f, float) / fs
    return sum(poly_group_delay(s[:3], w) - poly_group_delay([1, s[4], s[5]], w) for s in sos)


def zpk_response_at(z, p, k, point):
    """|H| anywhere in the z-plane, the product-of-distances way."""
    return k * np.prod(point - z) / np.prod(point - p)


# ---------------------------------------------------------------------------
# 7. Hand-placed poles and zeros                              (DSP/Design.swift)
# ---------------------------------------------------------------------------


def pole_zero_filter(zeros, poles, fs):
    """Balance with roots at the origin (so the filter is causal) and scale the peak |H| to 0 dB."""
    zeros, poles = list(zeros), list(poles)
    if len(zeros) > len(poles):
        poles += [0j] * (len(zeros) - len(poles))
    elif len(poles) > len(zeros):
        zeros += [0j] * (len(poles) - len(zeros))
    z, p = np.array(zeros, complex), np.array(poles, complex)
    sos = zpk_to_sos(z, p, 1.0)
    f = fs / 2 * (np.arange(4097) / 4096) ** 2        # dense near DC, where hum notches live
    f = np.concatenate([f, np.abs(np.angle(p[np.abs(p) > 0.5])) / (2 * np.pi) * fs])
    peak = np.max(np.abs(sos_response(sos, f, fs)))
    k = 1 / peak if np.isfinite(peak) and peak > 1e-12 else 1.0
    return z, p, k, zpk_to_sos(z, p, k)


def preset_hum_remover(fs):
    """Notch pairs at 50, 100, ..., 250 Hz. Pole radius sets the notch width: about (1-r) fs / pi Hz."""
    r = 1 - 4 * math.pi / fs                          # ~4 Hz wide notches
    zeros, poles = [], []
    for f in [50, 100, 150, 200, 250]:
        w = 2 * math.pi * f / fs
        zeros += [cmath.exp(1j * w), cmath.exp(-1j * w)]
        poles += [r * cmath.exp(1j * w), r * cmath.exp(-1j * w)]
    return zeros, poles


def preset_comb(g=0.8, d=16):
    """y[n] = x[n] + g*y[n-d]  ->  poles at g^(1/d) * e^(j 2 pi k / d)."""
    r = g ** (1 / d)
    return [], [r * cmath.exp(2j * math.pi * k / d) for k in range(d)]


def minus_3db(sos, fs):
    """Frequencies where |H| crosses 3.01 dB below its peak: a log-grid scan, then bisection."""
    grid = np.geomspace(1, fs / 2 * 0.9999, 2001)
    db = 20 * np.log10(np.maximum(np.abs(sos_response(sos, grid, fs)), 1e-12))
    level = db.max() - 3.0103
    crossings = []
    for i in np.nonzero((db[:-1] - level) * (db[1:] - level) < 0)[0]:
        lo, hi = grid[i], grid[i + 1]
        rising = db[i + 1] > db[i]
        for _ in range(40):
            mid = math.sqrt(lo * hi)                 # bisect on a log scale
            above = 20 * np.log10(abs(sos_response(sos, [mid], fs)[0])) > level
            lo, hi = (lo, mid) if above == rising else (mid, hi)
        crossings.append(math.sqrt(lo * hi))
    return crossings


def impulse_length(p, fs, floor_db=-60):
    """How many samples until the slowest pole has decayed by 60 dB (with 20 % margin)."""
    r = max(np.abs(p), default=0)
    if r <= 0:
        return 32
    if r >= 1:
        return 120                                   # unstable: just show the growth
    return int(min(4096, max(32, 1.2 * math.log(10 ** (floor_db / 20)) / math.log(r))))
