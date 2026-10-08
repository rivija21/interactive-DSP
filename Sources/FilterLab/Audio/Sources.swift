import Foundation

/// What feeds the filter.
enum SourceKind: Int, CaseIterable, Codable {
    case music, musicHum, sine, square, sweep, whiteNoise, pinkNoise, clicks, microphone, file

    var title: String {
        switch self {
        case .music: "Music loop"
        case .musicHum: "Music + 50 Hz hum"
        case .sine: "Sine wave"
        case .square: "Square wave"
        case .sweep: "Frequency sweep"
        case .whiteNoise: "White noise"
        case .pinkNoise: "Pink noise"
        case .clicks: "Clicks (impulses)"
        case .microphone: "Microphone"
        case .file: "Audio file…"
        }
    }

    var symbol: String {
        switch self {
        case .music, .musicHum: "music.note"
        case .sine: "waveform"
        case .square: "square.on.square"
        case .sweep: "arrow.up.right"
        case .whiteNoise, .pinkNoise: "aqi.medium"
        case .clicks: "metronome"
        case .microphone: "mic"
        case .file: "doc"
        }
    }

    var usesFrequency: Bool { self == .sine || self == .square }
    var usesLoop: Bool { self == .music || self == .musicHum || self == .file }
}

/// Generates the test signals. Lives on the audio thread.
final class SourceGenerator {
    let fs: Double
    private var phase = 0.0
    private var smoothedFrequency = 440.0
    private var sweepPhase = 0.0
    private var sweepTime = 0.0
    private var rng: UInt64 = 0x9E37_79B9_7F4A_7C15
    private var pink = [Double](repeating: 0, count: 7)
    private var clickCounter = 0
    private var humPhase = 0.0
    var loopPosition = 0

    static let sweepStart = 20.0
    static let sweepSeconds = 8.0
    var sweepEnd: Double { fs * 0.45 }

    init(fs: Double) {
        self.fs = fs
    }

    @inline(__always) private func white() -> Double {
        // xorshift64*
        rng ^= rng >> 12
        rng ^= rng << 25
        rng ^= rng >> 27
        let v = rng &* 2_685_821_657_736_338_717
        return Double(v >> 11) / Double(1 << 53) * 2 - 1
    }

    @inline(__always) private func polyBLEP(_ t: Double, _ dt: Double) -> Double {
        if t < dt {
            let x = t / dt
            return x + x - x * x - 1
        } else if t > 1 - dt {
            let x = (t - 1) / dt
            return x * x + x + x + 1
        }
        return 0
    }

    /// Fills x with n samples. Returns the instantaneous frequency for sine/square/sweep.
    func render(_ kind: SourceKind, frequency: Double, loop: LoopBuffer?, into x: UnsafeMutablePointer<Float>, _ n: Int) -> Double {
        let glide = 1 - exp(-1 / (0.02 * fs))
        switch kind {
        case .sine:
            for i in 0..<n {
                smoothedFrequency += (frequency - smoothedFrequency) * glide
                phase += smoothedFrequency / fs
                if phase >= 1 { phase -= 1 }
                x[i] = Float(0.35 * sin(2 * .pi * phase))
            }
            return smoothedFrequency
        case .square:
            for i in 0..<n {
                smoothedFrequency += (frequency - smoothedFrequency) * glide
                let dt = smoothedFrequency / fs
                phase += dt
                if phase >= 1 { phase -= 1 }
                var v = phase < 0.5 ? 1.0 : -1.0
                v += polyBLEP(phase, dt)
                var t2 = phase + 0.5
                if t2 >= 1 { t2 -= 1 }
                v -= polyBLEP(t2, dt)
                x[i] = Float(0.3 * v)
            }
            return smoothedFrequency
        case .sweep:
            let ratio = sweepEnd / Self.sweepStart
            var f = Self.sweepStart
            for i in 0..<n {
                f = Self.sweepStart * pow(ratio, sweepTime / Self.sweepSeconds)
                sweepPhase += f / fs
                if sweepPhase >= 1 { sweepPhase -= 1 }
                x[i] = Float(0.35 * sin(2 * .pi * sweepPhase))
                sweepTime += 1 / fs
                if sweepTime >= Self.sweepSeconds { sweepTime = 0 }
            }
            return f
        case .whiteNoise:
            for i in 0..<n { x[i] = Float(0.3 * white()) }
        case .pinkNoise:
            // Paul Kellet's pink-noise filter.
            for i in 0..<n {
                let w = white() * 0.08
                pink[0] = 0.99886 * pink[0] + w * 0.0555179
                pink[1] = 0.99332 * pink[1] + w * 0.0750759
                pink[2] = 0.96900 * pink[2] + w * 0.1538520
                pink[3] = 0.86650 * pink[3] + w * 0.3104856
                pink[4] = 0.55000 * pink[4] + w * 0.5329522
                pink[5] = -0.7616 * pink[5] - w * 0.0168980
                let v = pink[0] + pink[1] + pink[2] + pink[3] + pink[4] + pink[5] + pink[6] + w * 0.5362
                pink[6] = w * 0.115926
                x[i] = Float(v)
            }
        case .clicks:
            let period = Int(fs / 2)
            for i in 0..<n {
                x[i] = clickCounter == 0 ? 0.8 : 0
                clickCounter += 1
                if clickCounter >= period { clickCounter = 0 }
            }
        case .music, .musicHum, .file:
            if let loop {
                for i in 0..<n {
                    if loopPosition >= loop.count { loopPosition = 0 }
                    x[i] = loop.samples[loopPosition]
                    loopPosition += 1
                }
            } else {
                x.update(repeating: 0, count: n)
            }
            if kind == .musicHum {
                // 50 Hz mains hum with harmonics up to 250 Hz.
                let amps = [0.10, 0.05, 0.07, 0.03, 0.05]
                for i in 0..<n {
                    humPhase += 50 / fs
                    if humPhase >= 1 { humPhase -= 1 }
                    var h = 0.0
                    for (k, a) in amps.enumerated() {
                        h += a * sin(2 * .pi * humPhase * Double(k + 1))
                    }
                    x[i] += Float(h)
                }
            }
        case .microphone:
            x.update(repeating: 0, count: n)
        }
        return 0
    }
}

// MARK: - Music loop

/// Synthesizes a 4-bar loop (drums, bass, pad, arpeggio) with energy across the whole
/// spectrum, so every kind of filter has something audible to work on.
func synthesizeMusicLoop(fs: Double) -> [Float] {
    let bpm = 112.0
    let beat = 60 / bpm
    let bars = 4
    let total = Int((Double(bars * 4) * beat * fs).rounded())
    var out = [Double](repeating: 0, count: total)
    var seed: UInt64 = 12345
    func noise() -> Double {
        seed = seed &* 6_364_136_223_846_793_005 &+ 1_442_695_040_888_963_407
        return Double(seed >> 11) / Double(1 << 53) * 2 - 1
    }
    func add(_ start: Int, _ i: Int, _ v: Double) {
        out[(start + i) % total] += v
    }
    func midi(_ n: Int) -> Double { 440 * pow(2, Double(n - 69) / 12) }
    func blep(_ t: Double, _ dt: Double) -> Double {
        if t < dt { let x = t / dt; return x + x - x * x - 1 }
        if t > 1 - dt { let x = (t - 1) / dt; return x * x + x + x + 1 }
        return 0
    }
    func at(_ beats: Double) -> Int { Int((beats * beat * fs).rounded()) }

    func kick(_ s: Int) {
        var ph = 0.0
        for i in 0..<Int(0.45 * fs) {
            let t = Double(i) / fs
            let f = 48 + 100 * exp(-t / 0.03)
            ph += 2 * .pi * f / fs
            var v = 0.95 * sin(ph) * exp(-t / 0.2)
            if t < 0.003 { v += 0.25 * noise() * (1 - t / 0.003) }
            add(s, i, v)
        }
    }
    func snare(_ s: Int) {
        var prev = 0.0, ph = 0.0
        for i in 0..<Int(0.3 * fs) {
            let t = Double(i) / fs
            let n = noise()
            let hp = n - prev
            prev = n
            ph += 2 * .pi * 185 / fs
            add(s, i, 0.32 * hp * exp(-t / 0.07) + 0.35 * sin(ph) * exp(-t / 0.045))
        }
    }
    func hat(_ s: Int, open: Bool, level: Double) {
        var p1 = 0.0, p2 = 0.0
        let decay = open ? 0.16 : 0.035
        for i in 0..<Int((open ? 0.4 : 0.1) * fs) {
            let t = Double(i) / fs
            let n = noise()
            let d1 = n - p1
            p1 = n
            let d2 = d1 - p2
            p2 = d1
            add(s, i, level * 0.5 * d2 * exp(-t / decay))
        }
    }
    func bass(_ note: Int, _ s: Int, _ dur: Double) {
        let f = midi(note), dt = f / fs
        var ph = 0.0, lp = 0.0
        let len = Int((dur + 0.05) * fs)
        for i in 0..<len {
            let t = Double(i) / fs
            ph += dt
            if ph >= 1 { ph -= 1 }
            let saw = 2 * ph - 1 - blep(ph, dt)
            let cutoff = 220 + 900 * exp(-t / 0.08)
            lp += (1 - exp(-2 * .pi * cutoff / fs)) * (saw - lp)
            let env = min(1, t / 0.004) * (t < dur ? 1 : max(0, 1 - (t - dur) / 0.05))
            add(s, i, 0.42 * lp * env)
        }
    }
    func pad(_ notes: [Int], _ s: Int, _ dur: Double) {
        for note in notes {
            for detune in [-0.004, 0.004] {
                let f = midi(note) * (1 + detune), dt = f / fs
                var ph = Double(note % 7) / 7, l1 = 0.0, l2 = 0.0
                let len = Int((dur + 0.5) * fs)
                let a = 1 - exp(-2 * .pi * 1600 / fs)
                for i in 0..<len {
                    let t = Double(i) / fs
                    ph += dt
                    if ph >= 1 { ph -= 1 }
                    let saw = 2 * ph - 1 - blep(ph, dt)
                    l1 += a * (saw - l1)
                    l2 += a * (l1 - l2)
                    let env = min(1, t / 0.25) * (t < dur ? 1 : max(0, 1 - (t - dur) / 0.5))
                    add(s, i, 0.055 * l2 * env)
                }
            }
        }
    }
    func pluck(_ note: Int, _ s: Int) {
        let f = midi(note), dt = f / fs
        var ph = 0.0, lp = 0.0
        let a = 1 - exp(-2 * .pi * 4200 / fs)
        for i in 0..<Int(0.3 * fs) {
            let t = Double(i) / fs
            ph += dt
            if ph >= 1 { ph -= 1 }
            var v = ph < 0.3 ? 1.0 : -1.0
            v += blep(ph, dt)
            var t2 = ph + 0.7
            if t2 >= 1 { t2 -= 1 }
            v -= blep(t2, dt)
            lp += a * (v - lp)
            add(s, i, 0.075 * lp * exp(-t / 0.09))
        }
    }

    // Am – F – C – G
    let progression: [(bass: Int, chord: [Int])] = [
        (45, [57, 60, 64]), (41, [53, 57, 60]), (48, [55, 60, 64]), (43, [55, 59, 62]),
    ]
    for bar in 0..<bars {
        let b0 = Double(bar * 4)
        for k in [0.0, 2.0, 2.5] { kick(at(b0 + k)) }
        for k in [1.0, 3.0] { snare(at(b0 + k)) }
        for e in 0..<8 {
            let open = e == 7
            hat(at(b0 + Double(e) * 0.5), open: open, level: e % 2 == 0 ? 0.8 : 0.5)
        }
        let (root, chord) = progression[bar]
        for e in 0..<8 where e != 3 {
            let note = e % 4 == 2 ? root + 12 : root
            bass(note, at(b0 + Double(e) * 0.5), beat * 0.42)
        }
        pad(chord, at(b0), beat * 4 - 0.1)
        let pattern = [0, 1, 2, 1, 0, 2, 1, 2]
        for s in 0..<16 {
            let note = chord[pattern[s % 8] % chord.count] + 12 + (s >= 8 && s % 4 == 3 ? 12 : 0)
            pluck(note, at(b0 + Double(s) * 0.25))
        }
    }
    let peak = out.map(abs).max() ?? 1
    let scale = 0.6 / max(peak, 1e-9)
    return out.map { Float($0 * scale) }
}
