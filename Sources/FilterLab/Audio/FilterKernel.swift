import Accelerate
import Foundation

/// A filter ready to run on the audio thread: coefficients and state in plain memory,
/// processed in Double precision. IIR filters run as a cascade of transposed direct-form II
/// biquads; FIR filters as a dot product over a doubled circular delay line.
final class FilterKernel {
    static let maxBlock = 2048

    let isStable: Bool
    let isFIR: Bool
    private let sectionCount: Int
    private let coeffs: UnsafeMutablePointer<Double>
    private let state: UnsafeMutablePointer<Double>
    private let tapCount: Int
    private let taps: UnsafeMutablePointer<Double>
    private let delay: UnsafeMutablePointer<Double>
    private var pos = 0
    private let work: UnsafeMutablePointer<Double>

    init(_ filter: DigitalFilter) {
        isStable = filter.isStable
        work = .allocate(capacity: Self.maxBlock)
        work.initialize(repeating: 0, count: Self.maxBlock)
        switch filter.structure {
        case .iir(let sos):
            isFIR = false
            sectionCount = sos.count
            coeffs = .allocate(capacity: max(1, sos.count * 5))
            for (i, s) in sos.enumerated() {
                coeffs[i * 5 + 0] = s.b0
                coeffs[i * 5 + 1] = s.b1
                coeffs[i * 5 + 2] = s.b2
                coeffs[i * 5 + 3] = s.a1
                coeffs[i * 5 + 4] = s.a2
            }
            state = .allocate(capacity: max(1, sos.count * 2))
            state.initialize(repeating: 0, count: max(1, sos.count * 2))
            tapCount = 0
            taps = .allocate(capacity: 1)
            delay = .allocate(capacity: 1)
        case .fir(let h):
            isFIR = true
            sectionCount = 0
            coeffs = .allocate(capacity: 1)
            state = .allocate(capacity: 1)
            tapCount = h.count
            taps = .allocate(capacity: h.count)
            for (i, v) in h.enumerated() { taps[i] = v }
            delay = .allocate(capacity: 2 * h.count)
            delay.initialize(repeating: 0, count: 2 * h.count)
        }
    }

    deinit {
        coeffs.deallocate()
        state.deallocate()
        taps.deallocate()
        delay.deallocate()
        work.deallocate()
    }

    func reset() {
        if isFIR {
            delay.update(repeating: 0, count: 2 * tapCount)
        } else {
            state.update(repeating: 0, count: max(1, sectionCount * 2))
        }
    }

    /// Carries the filter memory over from the kernel being replaced, so that moving a
    /// slider changes the sound smoothly instead of clicking.
    func adoptState(from old: FilterKernel) {
        if isFIR && old.isFIR && old.tapCount == tapCount {
            delay.update(from: old.delay, count: 2 * tapCount)
            pos = old.pos
        } else if !isFIR && !old.isFIR && old.sectionCount == sectionCount {
            state.update(from: old.state, count: sectionCount * 2)
        }
    }

    /// Filters n ≤ maxBlock samples. Returns false (and outputs silence) if the filter
    /// is unstable or its output ran away.
    @discardableResult
    func process(_ x: UnsafePointer<Float>, _ y: UnsafeMutablePointer<Float>, _ n: Int) -> Bool {
        guard isStable else {
            y.update(repeating: 0, count: n)
            return false
        }
        if isFIR {
            let count = tapCount
            for i in 0..<n {
                pos = pos == 0 ? count - 1 : pos - 1
                let v = Double(x[i])
                delay[pos] = v
                delay[pos + count] = v
                var acc = 0.0
                vDSP_dotprD(taps, 1, delay + pos, 1, &acc, vDSP_Length(count))
                work[i] = acc
            }
        } else {
            for i in 0..<n { work[i] = Double(x[i]) }
            for s in 0..<sectionCount {
                let c = coeffs + s * 5
                let b0 = c[0], b1 = c[1], b2 = c[2], a1 = c[3], a2 = c[4]
                var s1 = state[s * 2], s2 = state[s * 2 + 1]
                for i in 0..<n {
                    let xn = work[i]
                    let yn = b0 * xn + s1
                    s1 = b1 * xn - a1 * yn + s2
                    s2 = b2 * xn - a2 * yn
                    work[i] = yn
                }
                // Flush denormals so a decaying tail never slows the CPU down.
                if abs(s1) < 1e-200 { s1 = 0 }
                if abs(s2) < 1e-200 { s2 = 0 }
                state[s * 2] = s1
                state[s * 2 + 1] = s2
            }
        }
        var peak = 0.0
        vDSP_maxmgvD(work, 1, &peak, vDSP_Length(n))
        guard peak.isFinite && peak < 1e4 else {
            reset()
            y.update(repeating: 0, count: n)
            return false
        }
        for i in 0..<n { y[i] = Float(work[i]) }
        return true
    }
}
