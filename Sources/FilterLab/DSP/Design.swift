import Foundation

enum DesignMethod: String, CaseIterable, Codable {
    case iir, fir, poleZero

    var title: String {
        switch self {
        case .iir: "IIR"
        case .fir: "FIR"
        case .poleZero: "Pole–Zero"
        }
    }
}

/// A pole or zero placed by hand. A paired item stands for p and its conjugate p*.
struct PZItem: Codable, Equatable, Identifiable {
    enum Kind: String, Codable { case pole, zero }
    var id = UUID()
    var kind: Kind
    var position: Complex
    var paired: Bool

    init(_ kind: Kind, _ position: Complex, paired: Bool? = nil) {
        self.kind = kind
        let isPair = paired ?? !position.isReal(tolerance: 1e-9)
        self.paired = isPair
        self.position = isPair ? Complex(position.re, abs(position.im)) : Complex(position.re, 0)
    }

    static func pole(r: Double, f: Double, fs: Double) -> PZItem {
        PZItem(.pole, Complex(polar: r, 2 * .pi * f / fs), paired: true)
    }

    static func zero(r: Double, f: Double, fs: Double) -> PZItem {
        PZItem(.zero, Complex(polar: r, 2 * .pi * f / fs), paired: true)
    }

    var roots: [Complex] { paired ? [position, position.conj] : [position] }
}

/// Everything the user chose in the design panel.
struct DesignSpec: Codable, Equatable {
    var method: DesignMethod = .iir
    var band: BandType = .lowpass
    var family: IIRFamily = .butterworth
    var order: Int = 4
    var f1: Double = 1000
    var f2: Double = 4000
    var rippleDB: Double = 1
    var stopDB: Double = 60
    var taps: Int = 63
    var window: WindowType = .hamming
    var kaiserBeta: Double = 8
    var items: [PZItem] = []
    var presetName: String? = nil

    static let orderRange = 1...12
    static let tapRange = 3...255

    /// Keeps frequencies inside (0, Nyquist) and band edges in order.
    func clamped(fs: Double) -> DesignSpec {
        var s = self
        let lo = 5.0, hi = fs / 2 * 0.98
        s.order = min(max(s.order, Self.orderRange.lowerBound), Self.orderRange.upperBound)
        s.taps = min(max(s.taps, Self.tapRange.lowerBound), Self.tapRange.upperBound)
        if s.method == .fir && firNeedsOddTaps(s.band) && s.taps % 2 == 0 { s.taps += 1 }
        s.f1 = min(max(s.f1, lo), hi)
        s.f2 = min(max(s.f2, lo), hi)
        if s.band.isBand {
            if s.f2 < s.f1 * 1.02 {
                s.f2 = min(hi, s.f1 * 1.02)
                if s.f2 < s.f1 * 1.02 { s.f1 = s.f2 / 1.02 }
            }
        }
        s.rippleDB = min(max(s.rippleDB, 0.01), 6)
        s.stopDB = min(max(s.stopDB, 10), 120)
        s.kaiserBeta = min(max(s.kaiserBeta, 0), 16)
        return s
    }
}

/// Builds the filter for a spec. FIR zeros are left empty here because finding them is
/// slow for long filters; use `firZeros` (off the main thread) to fill them in.
func buildFilter(_ rawSpec: DesignSpec, fs: Double) -> DigitalFilter {
    let spec = rawSpec.clamped(fs: fs)
    switch spec.method {
    case .iir:
        let zpk = designIIR(band: spec.band, family: spec.family, order: spec.order, f1: spec.f1, f2: spec.f2,
                            rp: spec.rippleDB, rs: spec.stopDB, fs: fs)
        return DigitalFilter(zpk: zpk, structure: .iir(zpkToSOS(zpk)), sampleRate: fs)
    case .fir:
        let h = designFIR(band: spec.band, taps: spec.taps, f1: spec.f1, f2: spec.f2, window: spec.window,
                          beta: spec.kaiserBeta, fs: fs)
        let zpk = ZPK(zeros: [], poles: Array(repeating: .zero, count: h.count - 1), gain: h[0])
        return DigitalFilter(zpk: zpk, structure: .fir(h), sampleRate: fs)
    case .poleZero:
        return buildPoleZeroFilter(spec.items, fs: fs)
    }
}

/// The zeros of an FIR filter (roots of its tap polynomial).
func firZeros(_ h: [Double]) -> [Complex] {
    // Leading/trailing taps that are zero (to rounding) are just delay; dropping them
    // avoids roots at 0 and enormous spurious roots.
    let tiny = 1e-12 * (h.map(abs).max() ?? 0)
    var taps = h
    while taps.count > 1, abs(taps.first!) <= tiny { taps.removeFirst() }
    while taps.count > 1, abs(taps.last!) <= tiny { taps.removeLast() }
    return polyRoots(taps)
}

/// Hand-placed poles and zeros, balanced with poles/zeros at the origin so the filter is
/// causal, and scaled so the loudest frequency sits at 0 dB.
func buildPoleZeroFilter(_ items: [PZItem], fs: Double) -> DigitalFilter {
    var zeros = items.filter { $0.kind == .zero }.flatMap(\.roots)
    var poles = items.filter { $0.kind == .pole }.flatMap(\.roots)
    if zeros.count > poles.count {
        poles += Array(repeating: .zero, count: zeros.count - poles.count)
    } else if poles.count > zeros.count {
        zeros += Array(repeating: .zero, count: poles.count - zeros.count)
    }
    var zpk = ZPK(zeros: zeros, poles: poles, gain: 1)
    var filter = DigitalFilter(zpk: zpk, structure: .iir(zpkToSOS(zpk)), sampleRate: fs)
    // Normalise the peak of |H| on the unit circle to 1.
    var peak = 0.0
    let n = 4096
    for i in 0...n {
        // Dense near DC so very low notches/resonances are not missed.
        let f = fs / 2 * pow(Double(i) / Double(n), 2)
        peak = max(peak, filter.response(at: f).magnitude)
    }
    for p in poles where p.magnitude > 0.5 {
        let f = abs(p.phase) / (2 * .pi) * fs
        peak = max(peak, filter.response(at: f).magnitude)
    }
    if peak.isFinite && peak > 1e-12 {
        zpk.gain = 1 / peak
        filter = DigitalFilter(zpk: zpk, structure: .iir(zpkToSOS(zpk)), sampleRate: fs)
    }
    return filter
}

/// Hand-made filters that show one idea each.
enum PoleZeroPreset: String, CaseIterable {
    case resonator, notch, humRemover, comb, allpass, movingAverage, dcBlocker

    var title: String {
        switch self {
        case .resonator: "Resonator (1 kHz ping)"
        case .notch: "Notch (1 kHz)"
        case .humRemover: "Mains hum remover (50 Hz)"
        case .comb: "Comb filter (metallic)"
        case .allpass: "All-pass (phase only)"
        case .movingAverage: "Moving average (8 samples)"
        case .dcBlocker: "DC blocker"
        }
    }

    var summary: String {
        switch self {
        case .resonator: "A pole pair close to the unit circle makes a sharp peak; zeros at DC and Nyquist."
        case .notch: "Zeros on the circle remove one frequency completely; nearby poles keep the notch narrow."
        case .humRemover: "Notches at 50, 100, 150, 200 and 250 Hz remove Sri Lankan mains hum and its harmonics."
        case .comb: "Poles evenly spaced round the circle: y[n] = x[n] + 0.8·y[n−16]. Peaks every fs/16."
        case .allpass: "Each zero mirrors its pole across the circle (1/p*), so |H| is flat but the phase bends."
        case .movingAverage: "The average of the last 8 samples: zeros at every multiple of fs/8 except DC."
        case .dcBlocker: "A zero at z = 1 kills DC; a pole just inside keeps everything else."
        }
    }

    func items(fs: Double) -> [PZItem] {
        switch self {
        case .resonator:
            return [.pole(r: 0.995, f: 1000, fs: fs), PZItem(.zero, Complex(1)), PZItem(.zero, Complex(-1))]
        case .notch:
            return [.zero(r: 1, f: 1000, fs: fs), .pole(r: 0.97, f: 1000, fs: fs)]
        case .humRemover:
            return [50.0, 100, 150, 200, 250].flatMap { f -> [PZItem] in
                // Pole radius sets the notch width: about (1 − r)·fs/π Hz.
                let r = 1 - 4 * .pi / fs
                return [.zero(r: 1, f: f, fs: fs), .pole(r: r, f: f, fs: fs)]
            }
        case .comb:
            let d = 16, g = 0.8
            let r = pow(g, 1 / Double(d))
            var items: [PZItem] = [PZItem(.pole, Complex(r)), PZItem(.pole, Complex(-r))]
            for k in 1..<(d / 2) {
                items.append(PZItem(.pole, Complex(polar: r, 2 * .pi * Double(k) / Double(d)), paired: true))
            }
            return items
        case .allpass:
            let p = Complex(polar: 0.9, 2 * .pi * 2000 / fs)
            return [PZItem(.pole, p, paired: true), PZItem(.zero, Complex(polar: 1 / 0.9, p.phase), paired: true)]
        case .movingAverage:
            var items: [PZItem] = [PZItem(.zero, Complex(-1))]
            for k in 1...3 {
                items.append(PZItem(.zero, Complex(polar: 1, 2 * .pi * Double(k) / 8), paired: true))
            }
            return items
        case .dcBlocker:
            return [PZItem(.zero, Complex(1)), PZItem(.pole, Complex(0.995))]
        }
    }
}

/// Turns any filter's poles and zeros into editable items (origin roots are dropped,
/// because the balancing step adds them back).
func editableItems(from zpk: ZPK) -> [PZItem] {
    var items: [PZItem] = []
    for (kind, roots) in [(PZItem.Kind.zero, zpk.zeros), (.pole, zpk.poles)] {
        let (pairs, reals) = splitConjugates(roots)
        for p in pairs where p.magnitude > 1e-9 { items.append(PZItem(kind, p, paired: true)) }
        for r in reals where abs(r) > 1e-9 { items.append(PZItem(kind, Complex(r), paired: false)) }
    }
    return items
}
