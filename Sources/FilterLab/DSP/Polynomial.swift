import Foundation

// Polynomials are stored as coefficient arrays in descending powers:
// [c0, c1, …, cn] means c0·xⁿ + c1·xⁿ⁻¹ + … + cn.

/// Multiplies out ∏(x − rᵢ).
func polyFromRoots(_ roots: [Complex]) -> [Complex] {
    var c: [Complex] = [.one]
    for r in roots {
        var next = [Complex](repeating: .zero, count: c.count + 1)
        for (k, ck) in c.enumerated() {
            next[k] += ck
            next[k + 1] -= ck * r
        }
        c = next
    }
    return c
}

/// Real coefficients of ∏(x − rᵢ) for a root set that is closed under conjugation.
func realPolyFromRoots(_ roots: [Complex]) -> [Double] {
    polyFromRoots(roots).map(\.re)
}

func horner(_ c: [Complex], _ z: Complex) -> Complex {
    var acc = Complex.zero
    for ck in c { acc = acc * z + ck }
    return acc
}

func horner(_ c: [Double], _ z: Complex) -> Complex {
    var acc = Complex.zero
    for ck in c { acc = acc * z + ck }
    return acc
}

/// All roots of a real polynomial.
func polyRoots(_ coeffs: [Double]) -> [Complex] {
    let roots = polyRoots(coeffs.map { Complex($0) })
    return pairConjugates(roots)
}

/// All roots of a complex polynomial, found with the Aberth–Ehrlich method.
func polyRoots(_ input: [Complex]) -> [Complex] {
    var c = input
    while let first = c.first, first.magnitude == 0 { c.removeFirst() }
    var roots: [Complex] = []
    while c.count > 1, let last = c.last, last.magnitude == 0 {
        c.removeLast()
        roots.append(.zero)
    }
    let n = c.count - 1
    guard n >= 1 else { return roots }
    let lead = c[0]
    c = c.map { $0 / lead }

    if n == 1 {
        return roots + [-c[1]]
    }
    if n == 2 {
        let b = c[1], cc = c[2]
        let disc = csqrt(b * b - 4 * cc)
        // Pick the sign that avoids cancellation, then use Vieta for the other root.
        let q = (b.re * disc.re + b.im * disc.im) >= 0 ? -0.5 * (b + disc) : -0.5 * (b - disc)
        if q.magnitude == 0 { return roots + [.zero, .zero] }
        return roots + [q, cc / q]
    }

    let dc = (0..<n).map { k in c[k] * Double(n - k) }
    // Reversed polynomial q(w) = wⁿ·p(1/w), used for |z| > 1 so high powers never overflow.
    let rc = Array(c.reversed())
    let rdc = (0..<n).map { k in rc[k] * Double(n - k) }

    /// Newton correction p(z)/p′(z), evaluated stably inside or outside the unit circle.
    func newtonRatio(_ z: Complex) -> Complex? {
        if z.norm2 <= 1 {
            let p = horner(c, z)
            if p.norm2 == 0 { return nil }
            let dp = horner(dc, z)
            return dp.norm2 == 0 ? Complex(1e-12) : p / dp
        }
        // p/p′ = z / (n − w·q′(w)/q(w)) with w = 1/z.
        let w = 1 / z
        let q = horner(rc, w)
        if q.norm2 == 0 { return nil }
        let dq = horner(rdc, w)
        let den = Double(n) - w * dq / q
        return den.norm2 == 0 ? Complex(1e-12) : z / den
    }

    // Start on a circle whose radius is the geometric mean of the root magnitudes.
    let radius = max(1e-6, pow(c[n].magnitude, 1 / Double(n)))
    var z = (0..<n).map { k in Complex(polar: radius, 2 * .pi * Double(k) / Double(n) + 0.4) }
    var done = [Bool](repeating: false, count: n)

    for _ in 0..<600 {
        var maxStep = 0.0
        for k in 0..<n where !done[k] {
            let zk = z[k]
            guard let ratio = newtonRatio(zk) else {
                done[k] = true
                continue
            }
            var s = Complex.zero
            for j in 0..<n where j != k {
                let d = zk - z[j]
                if d.norm2 > 0 { s += 1 / d }
            }
            let w = ratio / (1 - ratio * s)
            guard w.isFinite else { continue }
            z[k] = zk - w
            let step = w.magnitude / max(1, z[k].magnitude)
            if step < 1e-15 { done[k] = true }
            maxStep = max(maxStep, step)
        }
        if maxStep < 1e-14 { break }
    }

    // Polish each root with Newton's method.
    for k in 0..<n {
        for _ in 0..<3 {
            guard let step = newtonRatio(z[k]), step.isFinite else { break }
            z[k] -= step
        }
    }
    return roots + z
}

/// For roots of a real polynomial: snaps near-real roots onto the real axis and makes
/// each complex root's partner its exact conjugate. Never changes the number of roots.
func pairConjugates(_ roots: [Complex], tolerance: Double = 1e-8) -> [Complex] {
    var result: [Complex] = []
    var upper: [Complex] = []
    var lower: [Complex] = []
    for r in roots {
        if abs(r.im) <= tolerance * max(1, r.magnitude) {
            result.append(Complex(r.re, 0))
        } else if r.im > 0 {
            upper.append(r)
        } else {
            lower.append(r)
        }
    }
    while !upper.isEmpty && !lower.isEmpty {
        let u = upper.removeLast()
        let idx = lower.indices.min { (lower[$0] - u.conj).magnitude < (lower[$1] - u.conj).magnitude }!
        let l = lower.remove(at: idx)
        let avg = Complex((u.re + l.re) / 2, (u.im - l.im) / 2)
        result.append(avg)
        result.append(avg.conj)
    }
    return result + upper + lower
}
