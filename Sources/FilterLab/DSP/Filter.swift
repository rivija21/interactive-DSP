import Foundation

/// A digital filter as zeros, poles and gain: H(z) = k·∏(z − zᵢ) / ∏(z − pᵢ).
struct ZPK: Codable, Equatable {
    var zeros: [Complex]
    var poles: [Complex]
    var gain: Double

    var maxPoleRadius: Double { poles.map(\.magnitude).max() ?? 0 }
    var isStable: Bool { maxPoleRadius < 1 - 1e-9 }

    func response(at z: Complex) -> Complex {
        var h = Complex(gain)
        for q in zeros { h *= z - q }
        for p in poles { h /= z - p }
        return h
    }
}

/// One second-order section: (b0 + b1 z⁻¹ + b2 z⁻²) / (1 + a1 z⁻¹ + a2 z⁻²).
struct Biquad: Codable, Equatable {
    var b0, b1, b2, a1, a2: Double

    func response(_ zInv: Complex) -> Complex {
        let zInv2 = zInv * zInv
        let num = b0 + b1 * zInv + b2 * zInv2
        let den = 1 + a1 * zInv + a2 * zInv2
        return num / den
    }

    /// Group delay in samples at angle ω.
    func groupDelay(_ w: Double) -> Double {
        polyGroupDelay([b0, b1, b2], w) - polyGroupDelay([1, a1, a2], w)
    }
}

/// Group delay of B(z) = Σ bₖ z⁻ᵏ at angle ω: Re{ Σ k·bₖ e^{−jωk} / Σ bₖ e^{−jωk} }.
func polyGroupDelay(_ b: [Double], _ w: Double) -> Double {
    var num = Complex.zero, den = Complex.zero
    for (k, bk) in b.enumerated() where bk != 0 {
        let e = Complex(polar: 1, -w * Double(k))
        num += e * (Double(k) * bk)
        den += e * bk
    }
    let r = num / den
    return r.re.isFinite ? r.re : 0
}

/// Groups roots into conjugate pairs (positive-imaginary member first) and real roots.
func splitConjugates(_ roots: [Complex]) -> (pairs: [Complex], reals: [Double]) {
    var pairs: [Complex] = []
    var reals: [Double] = []
    for r in roots {
        if r.isReal(tolerance: 1e-9) {
            reals.append(r.re)
        } else if r.im > 0 {
            pairs.append(r)
        }
    }
    return (pairs, reals)
}

/// Converts ZPK to a cascade of biquads. Poles closest to the unit circle are paired with
/// their nearest zeros, which keeps each section's gain well behaved.
func zpkToSOS(_ zpk: ZPK) -> [Biquad] {
    var (pPairs, pReals) = splitConjugates(zpk.poles)
    var (zPairs, zReals) = splitConjugates(zpk.zeros)

    enum Item { case pair(Complex), real(Double) }
    func distanceToCircle(_ z: Complex) -> Double { abs(1 - z.magnitude) }

    var sections: [Biquad] = []
    func makeSection(zeros: [Complex], poles: [Complex]) -> Biquad {
        let b = realPolyFromRoots(zeros) + Array(repeating: 0, count: 2 - zeros.count)
        let a = realPolyFromRoots(poles) + Array(repeating: 0, count: 2 - poles.count)
        return Biquad(b0: b[0], b1: b[1], b2: b[2], a1: a[1], a2: a[2])
    }

    func takeZeros(near target: Complex) -> [Complex] {
        // Prefer a conjugate pair or two reals (whichever is nearer), else whatever is left.
        let bestPair = zPairs.indices.min { (zPairs[$0] - target).magnitude < (zPairs[$1] - target).magnitude }
        let bestReal = zReals.indices.min { abs(zReals[$0] - target.re) + abs(target.im) < abs(zReals[$1] - target.re) + abs(target.im) }
        let pairDist = bestPair.map { (zPairs[$0] - target).magnitude } ?? .infinity
        let realDist = bestReal.map { (Complex(zReals[$0]) - target).magnitude } ?? .infinity
        if let ip = bestPair, pairDist <= realDist || zReals.isEmpty {
            let z = zPairs.remove(at: ip)
            return [z, z.conj]
        }
        if let ir = bestReal {
            let first = zReals.remove(at: ir)
            if let ir2 = zReals.indices.min(by: { abs(zReals[$0] - target.re) < abs(zReals[$1] - target.re) }) {
                return [Complex(first), Complex(zReals.remove(at: ir2))]
            }
            return [Complex(first)]
        }
        return []
    }

    while !pPairs.isEmpty || !pReals.isEmpty {
        let bestPair = pPairs.indices.min { distanceToCircle(pPairs[$0]) < distanceToCircle(pPairs[$1]) }
        let bestReal = pReals.indices.min { abs(1 - abs(pReals[$0])) < abs(1 - abs(pReals[$1])) }
        let pairDist = bestPair.map { distanceToCircle(pPairs[$0]) } ?? .infinity
        let realDist = bestReal.map { abs(1 - abs(pReals[$0])) } ?? .infinity
        var poles: [Complex]
        if let ip = bestPair, pairDist <= realDist {
            let p = pPairs.remove(at: ip)
            poles = [p, p.conj]
        } else {
            let p = pReals.remove(at: bestReal!)
            poles = [Complex(p)]
            if let ir2 = pReals.indices.min(by: { abs(1 - abs(pReals[$0])) < abs(1 - abs(pReals[$1])) }) {
                poles.append(Complex(pReals.remove(at: ir2)))
            }
        }
        var zeros = takeZeros(near: poles[0])
        // A single real pole should not swallow a complex pair if a real zero exists.
        if poles.count == 1 && zeros.count == 2 && !zeros[0].isReal() && !zReals.isEmpty {
            zPairs.append(zeros[0].im > 0 ? zeros[0] : zeros[1])
            zeros = [Complex(zReals.removeFirst())]
        }
        sections.append(makeSection(zeros: zeros, poles: poles))
    }
    // Zeros left over (more zeros than poles) become FIR-like sections.
    while !zPairs.isEmpty || !zReals.isEmpty {
        let zeros = takeZeros(near: Complex(1))
        sections.append(makeSection(zeros: zeros, poles: []))
    }
    if sections.isEmpty {
        sections.append(Biquad(b0: 1, b1: 0, b2: 0, a1: 0, a2: 0))
    }
    sections[0].b0 *= zpk.gain
    sections[0].b1 *= zpk.gain
    sections[0].b2 *= zpk.gain
    return sections
}

/// Direct-form numerator and denominator coefficients b[k], a[k] (a[0] = 1) of the whole filter.
func sosToTransferFunction(_ sos: [Biquad]) -> (b: [Double], a: [Double]) {
    var b: [Double] = [1], a: [Double] = [1]
    func mul(_ x: [Double], _ y: [Double]) -> [Double] {
        var r = [Double](repeating: 0, count: x.count + y.count - 1)
        for (i, xi) in x.enumerated() { for (j, yj) in y.enumerated() { r[i + j] += xi * yj } }
        return r
    }
    for s in sos {
        b = mul(b, [s.b0, s.b1, s.b2])
        a = mul(a, [1, s.a1, s.a2])
    }
    while b.count > 1 && b.last == 0 { b.removeLast() }
    while a.count > 1 && a.last == 0 { a.removeLast() }
    return (b, a)
}

/// The runnable form of a filter.
enum FilterStructure: Equatable {
    case iir([Biquad])
    case fir([Double])
}

/// A designed digital filter with everything needed to draw, explain and run it.
struct DigitalFilter {
    var zpk: ZPK
    var structure: FilterStructure
    var sampleRate: Double

    static func passthrough(fs: Double) -> DigitalFilter {
        DigitalFilter(zpk: ZPK(zeros: [], poles: [], gain: 1), structure: .iir([Biquad(b0: 1, b1: 0, b2: 0, a1: 0, a2: 0)]), sampleRate: fs)
    }

    var isStable: Bool {
        if case .fir = structure { return true }
        return zpk.isStable
    }

    var order: Int {
        switch structure {
        case .iir: max(zpk.poles.count, zpk.zeros.count)
        case .fir(let h): h.count - 1
        }
    }

    /// Multiplications per output sample in the implemented structure.
    var multipliesPerSample: Int {
        switch structure {
        case .iir(let s): s.count * 5
        case .fir(let h): h.count
        }
    }

    /// Complex frequency response at f Hz.
    func response(at f: Double) -> Complex {
        let w = 2 * .pi * f / sampleRate
        switch structure {
        case .iir(let sos):
            let zInv = Complex(polar: 1, -w)
            return sos.reduce(Complex.one) { $0 * $1.response(zInv) }
        case .fir(let h):
            var acc = Complex.zero
            let rot = Complex(polar: 1, -w)
            var e = Complex.one
            for (k, hk) in h.enumerated() {
                if k % 32 == 0 { e = Complex(polar: 1, -w * Double(k)) }
                acc += e * hk
                e *= rot
            }
            return acc
        }
    }

    func magnitudeDB(at f: Double) -> Double {
        20 * log10(max(response(at: f).magnitude, 1e-12))
    }

    /// Group delay (samples) at f Hz.
    func groupDelay(at f: Double) -> Double {
        let w = 2 * .pi * f / sampleRate
        switch structure {
        case .iir(let sos): return sos.reduce(0) { $0 + $1.groupDelay(w) }
        case .fir(let h): return polyGroupDelay(h, w)
        }
    }

    /// Simulates the filter on an input sequence (Double precision).
    func simulate(_ x: [Double]) -> [Double] {
        switch structure {
        case .iir(let sos):
            var y = x
            for s in sos {
                var s1 = 0.0, s2 = 0.0
                for n in 0..<y.count {
                    let xn = y[n]
                    let yn = s.b0 * xn + s1
                    s1 = s.b1 * xn - s.a1 * yn + s2
                    s2 = s.b2 * xn - s.a2 * yn
                    y[n] = yn
                }
            }
            return y
        case .fir(let h):
            return (0..<x.count).map { n in
                var acc = 0.0
                for k in 0..<min(h.count, n + 1) { acc += h[k] * x[n - k] }
                return acc
            }
        }
    }

    func impulseResponse(length: Int) -> [Double] {
        var x = [Double](repeating: 0, count: length)
        if length > 0 { x[0] = 1 }
        return simulate(x)
    }

    func stepResponse(length: Int) -> [Double] {
        simulate([Double](repeating: 1, count: length))
    }

    /// A sensible number of samples to show for the impulse response.
    func suggestedResponseLength() -> Int {
        switch structure {
        case .fir(let h):
            return max(16, h.count + 8)
        case .iir:
            guard isStable else { return 120 }
            let r = zpk.maxPoleRadius
            if r <= 0 { return 32 }
            // Samples until the slowest pole decays to 0.1 % (−60 dB).
            let n = log(1e-3) / log(r)
            return Int(min(4096, max(32, n * 1.2)))
        }
    }
}

// MARK: - Measurements

struct FilterMeasurements {
    var minus3dB: [Double] = []   // frequencies (Hz) where the response crosses −3 dB
    var passbandPeakDB: Double = 0
    var groupDelayMs: Double = 0  // in the passband
}

/// Finds −3 dB crossings by scanning a log grid and refining with bisection.
func measure(_ filter: DigitalFilter, band: BandType?) -> FilterMeasurements {
    let fs = filter.sampleRate
    let fMin = 1.0, fMax = fs / 2 * 0.9999
    let n = 2000
    let grid = (0...n).map { i in fMin * pow(fMax / fMin, Double(i) / Double(n)) }
    let mags = grid.map { filter.magnitudeDB(at: $0) }
    var m = FilterMeasurements()
    m.passbandPeakDB = mags.max() ?? 0
    let level = m.passbandPeakDB - 3.0103
    for i in 1..<grid.count where (mags[i - 1] - level) * (mags[i] - level) < 0 {
        var lo = grid[i - 1], hi = grid[i]
        let rising = mags[i] > mags[i - 1]
        for _ in 0..<40 {
            let mid = sqrt(lo * hi)
            let v = filter.magnitudeDB(at: mid)
            if (v > level) == rising { hi = mid } else { lo = mid }
        }
        m.minus3dB.append(sqrt(lo * hi))
        if m.minus3dB.count >= 6 { break }
    }
    if let peakIndex = mags.indices.max(by: { mags[$0] < mags[$1] }) {
        m.groupDelayMs = filter.groupDelay(at: grid[peakIndex]) / fs * 1000
    }
    return m
}
