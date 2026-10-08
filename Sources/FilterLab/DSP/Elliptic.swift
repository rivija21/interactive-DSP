import Foundation

// Jacobi elliptic functions and integrals, used by the elliptic (Cauer) filter design.
// The parameter m is the square of the modulus k.

/// Arithmetic–geometric mean.
func agm(_ a0: Double, _ b0: Double) -> Double {
    var a = a0, b = b0
    for _ in 0..<64 {
        let an = (a + b) / 2
        let bn = sqrt(a * b)
        if abs(an - bn) <= 1e-16 * an {
            return an
        }
        a = an
        b = bn
    }
    return a
}

/// Complete elliptic integral of the first kind, K(m).
func ellipK(_ m: Double) -> Double {
    .pi / (2 * agm(1, sqrt(1 - m)))
}

/// K(1 − p), accurate when p is tiny.
func ellipKm1(_ p: Double) -> Double {
    .pi / (2 * agm(1, sqrt(p)))
}

/// Jacobi elliptic functions sn, cn, dn of real argument u (Cephes ellpj).
func ellipj(_ u: Double, _ m: Double) -> (sn: Double, cn: Double, dn: Double) {
    if m < 1e-9 {
        let t = sin(u), b = cos(u)
        let ai = 0.25 * m * (u - t * b)
        return (t - ai * b, b + ai * t, 1 - 0.5 * m * t * t)
    }
    if m >= 0.9999999999 {
        var ai = 0.25 * (1 - m)
        let b = cosh(u), t = tanh(u)
        let phi = 1 / b
        let twon = b * sinh(u)
        let sn = t + ai * (twon - u) / (b * b)
        ai *= t * phi
        return (sn, phi - ai * (twon - u), phi + ai * (twon + u))
    }
    var a = [Double](repeating: 0, count: 9)
    var c = [Double](repeating: 0, count: 9)
    a[0] = 1
    var b = sqrt(1 - m)
    c[0] = sqrt(m)
    var twon = 1.0
    var i = 0
    while abs(c[i] / a[i]) > 1.11e-16 {
        if i > 7 { break }
        let ai = a[i]
        i += 1
        c[i] = (ai - b) / 2
        let t = sqrt(ai * b)
        a[i] = (ai + b) / 2
        b = t
        twon *= 2
    }
    var phi = twon * a[i] * u
    var last = 0.0
    repeat {
        let t = c[i] * sin(phi) / a[i]
        last = phi
        phi = (asin(t) + phi) / 2
        i -= 1
    } while i > 0
    let sn = sin(phi)
    let cn = cos(phi)
    let dnfix = cn / cos(phi - last)
    let dn = abs(dnfix) < 0.1 ? sqrt(1 - m * sn * sn) : dnfix
    return (sn, cn, dn)
}

/// Solves the degree equation: the modulus m with K'(m)/K(m) = N·K'(m1)/K(m1).
func ellipDegree(_ n: Int, _ m1: Double) -> Double {
    let k1 = ellipK(m1)
    let k1p = ellipKm1(m1)
    let q1 = exp(-Double.pi * k1p / k1)
    let q = pow(q1, 1 / Double(n))
    var num = 0.0
    for m in 0...7 { num += pow(q, Double(m * (m + 1))) }
    var den = 1.0
    for m in 1...8 { den += 2 * pow(q, Double(m * m)) }
    return 16 * q * pow(num / den, 4)
}

/// Inverse Jacobi sn for complex w, via the descending Landen transformation.
func arcJacSn(_ w: Complex, _ m: Double) -> Complex {
    func complement(_ kx: Double) -> Double { sqrt((1 - kx) * (1 + kx)) }
    func complement(_ kx: Complex) -> Complex { csqrt((1 - kx) * (1 + kx)) }
    let k = sqrt(m)
    var ks = [k]
    var iterations = 0
    while ks.last! != 0 && iterations < 12 {
        let kp = complement(ks.last!)
        ks.append((1 - kp) / (1 + kp))
        iterations += 1
    }
    let bigK = ks.dropFirst().reduce(1.0) { $0 * (1 + $1) } * Double.pi / 2
    var wn = w
    for idx in 0..<(ks.count - 1) {
        let kn = ks[idx], knext = ks[idx + 1]
        wn = 2 * wn / ((1 + knext) * (1 + complement(kn * wn)))
    }
    let u = 2 / Double.pi * casin(wn)
    return bigK * u
}

/// Real inverse of Jacobi sc with complementary parameter: Im(arcsn(i·w, m)).
func arcJacSc1(_ w: Double, _ m: Double) -> Double {
    arcJacSn(Complex(0, w), m).im
}
