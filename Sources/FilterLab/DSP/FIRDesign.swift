import Foundation

// FIR design by the window method (windowed sinc), matching scipy.signal.firwin.

enum WindowType: String, CaseIterable, Codable {
    case rectangular, hann, hamming, blackman, kaiser

    var title: String {
        switch self {
        case .rectangular: "Rectangular"
        case .hann: "Hann"
        case .hamming: "Hamming"
        case .blackman: "Blackman"
        case .kaiser: "Kaiser"
        }
    }

    /// Approximate peak sidelobe level of the designed filter (stopband attenuation), in dB.
    var typicalStopbandDB: Double? {
        switch self {
        case .rectangular: 21
        case .hann: 44
        case .hamming: 53
        case .blackman: 74
        case .kaiser: nil
        }
    }
}

/// Zeroth-order modified Bessel function of the first kind (power series).
func besselI0(_ x: Double) -> Double {
    var sum = 1.0, term = 1.0
    let y = x * x / 4
    for k in 1..<300 {
        term *= y / Double(k * k)
        sum += term
        if term < 1e-17 * sum { break }
    }
    return sum
}

/// Symmetric window of length n.
func makeWindow(_ type: WindowType, length n: Int, beta: Double = 8.6) -> [Double] {
    guard n > 1 else { return [1] }
    let d = Double(n - 1)
    return (0..<n).map { i in
        let x = Double(i)
        switch type {
        case .rectangular:
            return 1
        case .hann:
            return 0.5 - 0.5 * cos(2 * .pi * x / d)
        case .hamming:
            return 0.54 - 0.46 * cos(2 * .pi * x / d)
        case .blackman:
            return 0.42 - 0.5 * cos(2 * .pi * x / d) + 0.08 * cos(4 * .pi * x / d)
        case .kaiser:
            let r = 2 * x / d - 1
            return besselI0(beta * sqrt(max(0, 1 - r * r))) / besselI0(beta)
        }
    }
}

func sinc(_ x: Double) -> Double {
    x == 0 ? 1 : sin(.pi * x) / (.pi * x)
}

/// High-pass and band-stop FIR filters need an odd number of taps (a type I filter):
/// an even-length symmetric filter always has a zero at Nyquist.
func firNeedsOddTaps(_ band: BandType) -> Bool {
    band == .highpass || band == .bandstop
}

/// Window-method FIR. Frequencies in Hz. Returns the taps h[0…n−1].
func designFIR(band: BandType, taps n: Int, f1: Double, f2: Double, window: WindowType,
               beta: Double, fs: Double) -> [Double] {
    let nyq = fs / 2
    let c1 = f1 / nyq, c2 = f2 / nyq
    let bands: [(Double, Double)]
    switch band {
    case .lowpass: bands = [(0, c1)]
    case .highpass: bands = [(c1, 1)]
    case .bandpass: bands = [(c1, c2)]
    case .bandstop: bands = [(0, c1), (c2, 1)]
    }
    let alpha = 0.5 * Double(n - 1)
    let m = (0..<n).map { Double($0) - alpha }
    var h = [Double](repeating: 0, count: n)
    for (left, right) in bands {
        for i in 0..<n {
            h[i] += right * sinc(right * m[i]) - left * sinc(left * m[i])
        }
    }
    let w = makeWindow(window, length: n, beta: beta)
    for i in 0..<n { h[i] *= w[i] }

    // Scale for exactly 0 dB at the centre of the first passband.
    let (left, right) = bands[0]
    let scaleFrequency = left == 0 ? 0 : (right == 1 ? 1 : 0.5 * (left + right))
    var s = 0.0
    for i in 0..<n { s += h[i] * cos(.pi * m[i] * scaleFrequency) }
    if s != 0 {
        for i in 0..<n { h[i] /= s }
    }
    return h
}

/// Kaiser's formula for the stopband attenuation (dB) that a given β achieves.
func kaiserAttenuation(beta: Double) -> Double {
    // Inverse of β = 0.1102(A − 8.7) for A > 50, and the mid-range formula below that.
    if beta > 4.5513 { return beta / 0.1102 + 8.7 }
    // Solve 0.5842(A−21)^0.4 + 0.07886(A−21) = β numerically.
    var lo = 21.0, hi = 50.0
    for _ in 0..<60 {
        let mid = (lo + hi) / 2
        let b = 0.5842 * pow(mid - 21, 0.4) + 0.07886 * (mid - 21)
        if b < beta { lo = mid } else { hi = mid }
    }
    return beta <= 0 ? 21 : (lo + hi) / 2
}
