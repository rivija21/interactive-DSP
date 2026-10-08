import Foundation

// Classic IIR design: analog prototype → frequency transformation → bilinear transform.
// The steps mirror scipy.signal.iirfilter so results can be checked against SciPy.

enum BandType: String, CaseIterable, Codable {
    case lowpass, highpass, bandpass, bandstop

    var title: String {
        switch self {
        case .lowpass: "Low-pass"
        case .highpass: "High-pass"
        case .bandpass: "Band-pass"
        case .bandstop: "Band-stop"
        }
    }

    var isBand: Bool { self == .bandpass || self == .bandstop }
}

enum IIRFamily: String, CaseIterable, Codable {
    case butterworth, chebyshev1, chebyshev2, elliptic, bessel

    var title: String {
        switch self {
        case .butterworth: "Butterworth"
        case .chebyshev1: "Chebyshev I"
        case .chebyshev2: "Chebyshev II"
        case .elliptic: "Elliptic (Cauer)"
        case .bessel: "Bessel"
        }
    }

    var usesPassbandRipple: Bool { self == .chebyshev1 || self == .elliptic }
    var usesStopbandAttenuation: Bool { self == .chebyshev2 || self == .elliptic }

    /// What the cutoff frequency means for this family.
    var cutoffMeaning: String {
        switch self {
        case .butterworth: "−3 dB point"
        case .chebyshev1, .elliptic: "edge of the ripple band"
        case .chebyshev2: "start of the stopband"
        case .bessel: "phase midpoint"
        }
    }
}

/// Zeros, poles and gain of a continuous-time (s-domain) filter.
struct AnalogZPK {
    var z: [Complex]
    var p: [Complex]
    var k: Double
}

// MARK: - Analog low-pass prototypes (cutoff 1 rad/s)

func buttap(_ n: Int) -> AnalogZPK {
    let p = stride(from: -n + 1, to: n, by: 2).map { m in
        -cexp(Complex(0, Double.pi * Double(m) / Double(2 * n)))
    }
    return AnalogZPK(z: [], p: p, k: 1)
}

func cheb1ap(_ n: Int, rp: Double) -> AnalogZPK {
    let eps = sqrt(pow(10, 0.1 * rp) - 1)
    let mu = asinh(1 / eps) / Double(n)
    let p = stride(from: -n + 1, to: n, by: 2).map { m in
        -csinh(Complex(mu, Double.pi * Double(m) / Double(2 * n)))
    }
    var k = cprod(p.map { -$0 }).re
    if n % 2 == 0 { k /= sqrt(1 + eps * eps) }
    return AnalogZPK(z: [], p: p, k: k)
}

func cheb2ap(_ n: Int, rs: Double) -> AnalogZPK {
    let de = 1 / sqrt(pow(10, 0.1 * rs) - 1)
    let mu = asinh(1 / de) / Double(n)
    let ms: [Int] = n % 2 == 1
        ? Array(stride(from: -n + 1, to: 0, by: 2)) + Array(stride(from: 2, to: n, by: 2))
        : Array(stride(from: -n + 1, to: n, by: 2))
    let z = ms.map { m in Complex(0, 1 / sin(Double(m) * .pi / Double(2 * n))) }
    let p = stride(from: -n + 1, to: n, by: 2).map { m -> Complex in
        let b = -cexp(Complex(0, Double.pi * Double(m) / Double(2 * n)))
        return 1 / Complex(sinh(mu) * b.re, cosh(mu) * b.im)
    }
    let k = (cprod(p.map { -$0 }) / cprod(z.map { -$0 })).re
    return AnalogZPK(z: z, p: p, k: k)
}

func ellipap(_ n: Int, rp: Double, rs: Double) -> AnalogZPK {
    if n == 1 {
        let p = -sqrt(1 / (pow(10, 0.1 * rp) - 1))
        return AnalogZPK(z: [], p: [Complex(p)], k: -p)
    }
    let epsSq = pow(10, 0.1 * rp) - 1
    let eps = sqrt(epsSq)
    let ck1Sq = epsSq / (pow(10, 0.1 * rs) - 1)
    let val0 = ellipK(ck1Sq)
    let m = ellipDegree(n, ck1Sq)
    let capk = ellipK(m)

    let js = Array(stride(from: 1 - n % 2, to: n, by: 2))
    let sncndn = js.map { j in ellipj(Double(j) * capk / Double(n), m) }
    var z: [Complex] = []
    for v in sncndn where abs(v.sn) > 2e-16 {
        z.append(Complex(0, 1 / (sqrt(m) * v.sn)))
    }
    z += z.map(\.conj)

    let r = arcJacSc1(1 / eps, ck1Sq)
    let v0 = capk * r / (Double(n) * val0)
    let (sv, cv, dv) = ellipj(v0, 1 - m)
    var p = sncndn.map { v in
        -Complex(v.cn * v.dn * sv * cv, v.sn * dv) / (1 - (v.dn * sv) * (v.dn * sv))
    }
    if n % 2 == 1 {
        let scale = sqrt(p.reduce(0) { $0 + $1.norm2 })
        let newp = p.filter { abs($0.im) > 2e-16 * scale }
        p += newp.map(\.conj)
    } else {
        p += p.map(\.conj)
    }
    var k = (cprod(p.map { -$0 }) / cprod(z.map { -$0 })).re
    if n % 2 == 0 { k /= sqrt(1 + epsSq) }
    return AnalogZPK(z: z, p: p, k: k)
}

/// Bessel–Thomson prototype, normalised like SciPy's norm="phase".
func besselap(_ n: Int) -> AnalogZPK {
    // Reverse Bessel polynomial θₙ(s) = Σ aₖ sᵏ, aₖ = (2n−k)! / (2ⁿ⁻ᵏ k! (n−k)!).
    func factorial(_ x: Int) -> Double { x < 2 ? 1 : (2...x).reduce(1.0) { $0 * Double($1) } }
    var coeffs: [Double] = []
    for k in stride(from: n, through: 0, by: -1) {
        coeffs.append(factorial(2 * n - k) / (pow(2, Double(n - k)) * factorial(k) * factorial(n - k)))
    }
    let a0 = coeffs.last!
    let scale = pow(a0, -1 / Double(n))
    let p = polyRoots(coeffs).map { $0 * scale }
    return AnalogZPK(z: [], p: p, k: 1)
}

func analogPrototype(_ family: IIRFamily, order n: Int, rp: Double, rs: Double) -> AnalogZPK {
    switch family {
    case .butterworth: buttap(n)
    case .chebyshev1: cheb1ap(n, rp: rp)
    case .chebyshev2: cheb2ap(n, rs: rs)
    case .elliptic: ellipap(n, rp: rp, rs: rs)
    case .bessel: besselap(n)
    }
}

// MARK: - Frequency transformations (s-domain)

func lp2lp(_ a: AnalogZPK, wo: Double) -> AnalogZPK {
    let degree = a.p.count - a.z.count
    return AnalogZPK(z: a.z.map { $0 * wo }, p: a.p.map { $0 * wo }, k: a.k * pow(wo, Double(degree)))
}

func lp2hp(_ a: AnalogZPK, wo: Double) -> AnalogZPK {
    let degree = a.p.count - a.z.count
    let z = a.z.map { wo / $0 } + Array(repeating: .zero, count: degree)
    let p = a.p.map { wo / $0 }
    let k = a.k * (cprod(a.z.map { -$0 }) / cprod(a.p.map { -$0 })).re
    return AnalogZPK(z: z, p: p, k: k)
}

func lp2bp(_ a: AnalogZPK, wo: Double, bw: Double) -> AnalogZPK {
    let degree = a.p.count - a.z.count
    func split(_ xs: [Complex]) -> [Complex] {
        let lp = xs.map { $0 * (bw / 2) }
        let roots = lp.map { csqrt($0 * $0 - wo * wo) }
        return zip(lp, roots).map { $0 + $1 } + zip(lp, roots).map { $0 - $1 }
    }
    let z = split(a.z) + Array(repeating: .zero, count: degree)
    return AnalogZPK(z: z, p: split(a.p), k: a.k * pow(bw, Double(degree)))
}

func lp2bs(_ a: AnalogZPK, wo: Double, bw: Double) -> AnalogZPK {
    let degree = a.p.count - a.z.count
    func split(_ xs: [Complex]) -> [Complex] {
        let hp = xs.map { (bw / 2) / $0 }
        let roots = hp.map { csqrt($0 * $0 - wo * wo) }
        return zip(hp, roots).map { $0 + $1 } + zip(hp, roots).map { $0 - $1 }
    }
    let z = split(a.z)
        + Array(repeating: Complex(0, wo), count: degree)
        + Array(repeating: Complex(0, -wo), count: degree)
    let k = a.k * (cprod(a.z.map { -$0 }) / cprod(a.p.map { -$0 })).re
    return AnalogZPK(z: z, p: split(a.p), k: k)
}

/// s → z with s = 2·fs·(z − 1)/(z + 1).
func bilinear(_ a: AnalogZPK, fs: Double) -> ZPK {
    let degree = a.p.count - a.z.count
    let fs2 = 2 * fs
    let z = a.z.map { (fs2 + $0) / (fs2 - $0) } + Array(repeating: Complex(-1), count: degree)
    let p = a.p.map { (fs2 + $0) / (fs2 - $0) }
    let k = a.k * (cprod(a.z.map { fs2 - $0 }) / cprod(a.p.map { fs2 - $0 })).re
    return ZPK(zeros: pairConjugates(z), poles: pairConjugates(p), gain: k)
}

/// Pre-warped analog frequency (rad/s) that the bilinear transform maps onto f (Hz).
func prewarp(_ f: Double, fs: Double) -> Double {
    2 * fs * tan(.pi * f / fs)
}

/// Designs a digital IIR filter. For band filters the prototype order is doubled.
func designIIR(band: BandType, family: IIRFamily, order: Int, f1: Double, f2: Double,
               rp: Double, rs: Double, fs: Double) -> ZPK {
    let proto = analogPrototype(family, order: order, rp: rp, rs: rs)
    let analog: AnalogZPK
    switch band {
    case .lowpass:
        analog = lp2lp(proto, wo: prewarp(f1, fs: fs))
    case .highpass:
        analog = lp2hp(proto, wo: prewarp(f1, fs: fs))
    case .bandpass, .bandstop:
        let w1 = prewarp(f1, fs: fs), w2 = prewarp(f2, fs: fs)
        let wo = sqrt(w1 * w2), bw = w2 - w1
        analog = band == .bandpass ? lp2bp(proto, wo: wo, bw: bw) : lp2bs(proto, wo: wo, bw: bw)
    }
    return bilinear(analog, fs: fs)
}
