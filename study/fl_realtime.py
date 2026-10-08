"""Filter Lab's real-time audio path and live analysis, in plain Python + NumPy.

Mirrors Sources/FilterLab/Audio/*.swift and UI/LiveViews.swift. Python is far too slow
(and has a global interpreter lock) for a real 48 kHz audio callback, so treat these as
readable models of what the Swift code does every few milliseconds.
"""
import math
import threading

import numpy as np

# ---------------------------------------------------------------------------
# 1. Running filters sample by sample                       (Audio/FilterKernel.swift)
# ---------------------------------------------------------------------------


class BiquadCascade:
    """IIR filter as cascaded biquads, transposed direct form II, 2 state values per section."""

    def __init__(self, sos):
        self.sos = np.asarray(sos, float)
        self.state = np.zeros((len(self.sos), 2))       # s1, s2 for each section
        self.stable = True

    def process(self, x):
        """Filter one block. The state carries over, so consecutive blocks join seamlessly."""
        y = np.asarray(x, float).copy()
        for i, (b0, b1, b2, _, a1, a2) in enumerate(self.sos):
            s1, s2 = self.state[i]
            for n in range(len(y)):
                xn = y[n]
                yn = b0 * xn + s1                       # output = feed-forward + stored state
                s1 = b1 * xn - a1 * yn + s2             # update the two delay registers
                s2 = b2 * xn - a2 * yn
                y[n] = yn
            self.state[i] = s1, s2
        # Safety net: a runaway (unstable or numerically blown-up) filter is silenced.
        if not np.all(np.isfinite(y)) or np.max(np.abs(y), initial=0) > 1e4:
            self.state[:] = 0
            return np.zeros_like(y), False
        return y, True

    def adopt_state(self, old):
        """Keep the filter memory when the user nudges a slider, to avoid clicks."""
        if old.state.shape == self.state.shape:
            self.state[:] = old.state


class FIRFilter:
    """Direct-form FIR with a 'doubled' circular delay line, so every dot product is contiguous."""

    def __init__(self, taps):
        self.h = np.asarray(taps, float)
        self.n = len(self.h)
        self.delay = np.zeros(2 * self.n)
        self.pos = 0

    def process(self, x):
        y = np.empty(len(x))
        for i, v in enumerate(x):
            self.pos = self.n - 1 if self.pos == 0 else self.pos - 1
            self.delay[self.pos] = v                    # write each sample twice ...
            self.delay[self.pos + self.n] = v
            y[i] = self.h @ self.delay[self.pos:self.pos + self.n]   # ... so this slice never wraps
        return y


# ---------------------------------------------------------------------------
# 2. Test signals                                            (Audio/Sources.swift)
# ---------------------------------------------------------------------------


class Generator:
    """Phase-continuous test signals, rendered one block at a time."""

    def __init__(self, fs):
        self.fs = fs
        self.phase = 0.0
        self.freq = 440.0                               # smoothed frequency (no zipper noise)
        self.sweep_phase = 0.0
        self.sweep_time = 0.0
        self.rng = 0x9E3779B97F4A7C15
        self.pink = [0.0] * 7
        self.click = 0

    def sine(self, n, target_hz):
        glide = 1 - math.exp(-1 / (0.02 * self.fs))     # 20 ms one-pole glide
        out = np.empty(n)
        for i in range(n):
            self.freq += (target_hz - self.freq) * glide
            self.phase = (self.phase + self.freq / self.fs) % 1.0   # phase accumulator in cycles
            out[i] = 0.35 * math.sin(2 * math.pi * self.phase)
        return out

    @staticmethod
    def poly_blep(t, dt):
        """Polynomial band-limited step: smooths each jump so the square wave barely aliases."""
        if t < dt:
            x = t / dt
            return x + x - x * x - 1
        if t > 1 - dt:
            x = (t - 1) / dt
            return x * x + x + x + 1
        return 0.0

    def square(self, n, hz):
        dt = hz / self.fs
        out = np.empty(n)
        for i in range(n):
            self.phase = (self.phase + dt) % 1.0
            v = 1.0 if self.phase < 0.5 else -1.0
            v += self.poly_blep(self.phase, dt)                 # rising edge at t = 0
            v -= self.poly_blep((self.phase + 0.5) % 1.0, dt)   # falling edge at t = 0.5
            out[i] = 0.3 * v
        return out

    def sweep(self, n, f0=20.0, seconds=8.0):
        """Logarithmic chirp f0 -> 0.45 fs, equal time per octave, then repeat."""
        f1 = 0.45 * self.fs
        out = np.empty(n)
        for i in range(n):
            f = f0 * (f1 / f0) ** (self.sweep_time / seconds)
            self.sweep_phase = (self.sweep_phase + f / self.fs) % 1.0
            out[i] = 0.35 * math.sin(2 * math.pi * self.sweep_phase)
            self.sweep_time = (self.sweep_time + 1 / self.fs) % seconds
        return out

    def white(self):
        """xorshift64*: a tiny, fast, allocation-free random generator, uniform in [-1, 1)."""
        mask = (1 << 64) - 1
        r = self.rng
        r ^= r >> 12
        r ^= (r << 25) & mask
        r ^= r >> 27
        self.rng = r
        v = (r * 2685821657736338717) & mask
        return (v >> 11) / float(1 << 53) * 2 - 1

    def white_noise(self, n):
        return np.array([0.3 * self.white() for _ in range(n)])

    def pink_noise(self, n):
        """Paul Kellet's filter: six one-pole low-passes summed give about -3 dB per octave."""
        b, out = self.pink, np.empty(n)
        for i in range(n):
            w = self.white() * 0.08
            b[0] = 0.99886 * b[0] + w * 0.0555179
            b[1] = 0.99332 * b[1] + w * 0.0750759
            b[2] = 0.96900 * b[2] + w * 0.1538520
            b[3] = 0.86650 * b[3] + w * 0.3104856
            b[4] = 0.55000 * b[4] + w * 0.5329522
            b[5] = -0.7616 * b[5] - w * 0.0168980
            out[i] = sum(b) + w * 0.5362
            b[6] = w * 0.115926
        return out

    def clicks(self, n):
        """A single-sample impulse every half second: the filter's impulse response, audible."""
        period = int(self.fs / 2)
        out = np.zeros(n)
        for i in range(n):
            if self.click == 0:
                out[i] = 0.8
            self.click = (self.click + 1) % period
        return out


def hum(n, fs, start_phase=0.0):
    """50 Hz mains hum with harmonics up to 250 Hz (added on top of the music loop)."""
    t = start_phase + 50 * np.arange(1, n + 1) / fs
    amps = [0.10, 0.05, 0.07, 0.03, 0.05]
    return sum(a * np.sin(2 * np.pi * t * (k + 1)) for k, a in enumerate(amps))


# ---------------------------------------------------------------------------
# 3. Moving data between threads                            (Audio/RTSupport.swift)
# ---------------------------------------------------------------------------


class SampleRing:
    """Single-writer ring buffer. Readers ask for the newest samples by absolute index."""

    def __init__(self, capacity):
        assert capacity & (capacity - 1) == 0, "power of two, so index & mask replaces modulo"
        self.buf = np.zeros(capacity, np.float32)
        self.mask = capacity - 1
        self.total_written = 0          # in Swift this counter is an Atomic<Int>

    def write(self, x):
        idx = (self.total_written + np.arange(len(x))) & self.mask
        self.buf[idx] = x
        self.total_written += len(x)    # publish AFTER the data is in place ("release" ordering)

    def copy(self, start, count):
        return self.buf[(start + np.arange(count)) & self.mask]

    def copy_latest(self, count):
        return self.copy(self.total_written - count, count)


class Exchange:
    """Hands a new object (filter kernel, audio loop) to the audio thread without ever blocking it.

    The audio thread only *tries* the lock; if the UI holds it, it keeps the old object for one
    more block. The object it replaces is parked in `retired` so the memory is freed later by
    the UI thread, never inside the audio callback.
    """

    def __init__(self):
        self.lock = threading.Lock()
        self.pending = None
        self.retired = None

    def publish(self, obj):             # UI thread
        with self.lock:
            self.pending, self.retired = obj, None

    def drain(self):                    # UI thread, ~60 times a second
        with self.lock:
            self.retired = None

    def take(self, current):            # audio thread, start of every block
        if not self.lock.acquire(blocking=False):
            return current              # busy: try again next block (a few ms later)
        try:
            if self.pending is None or self.retired is not None:
                return current
            self.retired, new = current, self.pending
            self.pending = None
            return new
        finally:
            self.lock.release()


# ---------------------------------------------------------------------------
# 4. The output callback                                     (Audio/LabAudio.swift)
# ---------------------------------------------------------------------------


def render_block(x, kernel, wet, gain, filter_on, listen_volume, fs, in_ring, out_ring):
    """What happens to one block: filter, A/B crossfade, volume ramp, clip, feed the displays."""
    y, ok = kernel.process(x)
    step = 1 / (0.01 * fs)                              # 10 ms ramps -> no clicks
    out = np.empty(len(x))
    heard = np.empty(len(x))
    for i in range(len(x)):
        wet += max(-step, min(step, (1.0 if filter_on else 0.0) - wet))
        gain += max(-step, min(step, listen_volume - gain))
        heard[i] = x[i] + (y[i] - x[i]) * wet           # crossfade original <-> filtered
        out[i] = min(1.0, max(-1.0, heard[i] * gain))   # what goes to the speakers
    in_ring.write(x)                                    # displays see input ...
    out_ring.write(heard)                               # ... and what you hear
    return out, wet, gain


# ---------------------------------------------------------------------------
# 5. Microphone: rate conversion with clock-drift control      (Audio/MicCapture.swift)
# ---------------------------------------------------------------------------


class MicReader:
    """Reads the mic ring at the lab's rate with 4-point Hermite interpolation.

    The mic and the speakers run on different clocks, so the read speed is nudged by up to
    +-0.3 % to keep about 40 ms of audio waiting in the ring.
    """

    def __init__(self, ring, mic_rate, out_rate):
        self.ring = ring
        self.ratio = mic_rate / out_rate              # mic samples per output sample
        self.target = mic_rate * 0.04 + 1024
        self.position = -1.0
        self.adjust = 1.0

    def read(self, n):
        written = float(self.ring.total_written)
        self.target = max(self.target, n * self.ratio * 2 + 256)
        if self.position < 0 or written - self.position > len(self.ring.buf) / 2:
            self.position = written - self.target
        if written - self.position < n * self.ratio * self.adjust + 4:
            self.position = max(0.0, written - self.target)   # underrun: output silence, wait
            return np.zeros(n)
        error = (written - self.position - self.target) / self.target
        self.adjust += (1 + max(-0.003, min(0.003, error * 0.01)) - self.adjust) * 0.05
        step = self.ratio * self.adjust
        out = np.empty(n)
        for i in range(n):
            base = int(self.position)
            t = self.position - base
            y0, y1, y2, y3 = self.ring.copy(base - 1, 4)
            c1 = 0.5 * (y2 - y0)                          # Catmull-Rom / Hermite spline
            c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3
            c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2)
            out[i] = ((c3 * t + c2) * t + c1) * t + y1
            self.position += step
        return out


# ---------------------------------------------------------------------------
# 6. Live analysis                                           (UI/LiveViews.swift)
# ---------------------------------------------------------------------------


def fft_size(fs, divisor):
    n = 256
    while n < fs / divisor:
        n *= 2
    return n


def power_spectrum(frame):
    """Hann-windowed FFT power, scaled so a full-scale sine reads 0 dBFS (power 1)."""
    n = len(frame)
    window = 0.5 * (1 - np.cos(2 * np.pi * np.arange(n) / n))   # periodic Hann (vDSP_HANN_DENORM)
    spectrum = np.fft.rfft(frame * window)
    power = 4 * np.abs(spectrum) ** 2 / np.sum(window) ** 2
    power[0] /= 4                                       # DC and Nyquist bins have no mirror image
    power[-1] /= 4
    return power


def smooth(acc, p):
    """Analyser ballistics: fast attack, gentle release, and a fast fall when a signal vanishes."""
    if acc is None or len(acc) != len(p):
        return p.copy()
    acc = acc.copy()
    up = p > acc
    gone = (~up) & (p < acc * 0.01)
    rest = ~(up | gone)
    acc[up] += (p[up] - acc[up]) * 0.6
    acc[gone] = np.maximum(p[gone], acc[gone] * 0.5)
    acc[rest] += (p[rest] - acc[rest]) * 0.25
    return acc


def spectrogram_columns(signal, fs):
    """STFT as the spectrogram draws it: one column every 10 ms, FFT of the last n samples."""
    n = fft_size(fs, 24)
    hop = max(32, int(fs / 100))
    cols = []
    for end in range(n, len(signal) + 1, hop):
        p = power_spectrum(signal[end - n:end])
        cols.append(10 * np.log10(np.maximum(p, 1e-20)))
    return np.array(cols).T                             # rows = frequency bins, columns = time


def scope_trigger(x, window, pre):
    """Newest rising zero crossing that leaves a full window after it (like a scope's 'normal' mode)."""
    peak = np.max(np.abs(x))
    hyst = 0.02 * peak
    for i in range(len(x) - window + pre, pre + 1, -1):
        if x[i - 1] <= 0 < x[i] and x[i] > hyst * 0.2:
            dipped = np.any(x[max(0, i - window // 4):i] < -hyst)
            if dipped or x[i] > 0.3 * peak:
                return i - pre                            # start of the window to draw
    return None
