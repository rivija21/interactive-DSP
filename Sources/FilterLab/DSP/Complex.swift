import Foundation

/// A complex number. Poles, zeros and frequency responses all live here.
struct Complex: Hashable, Codable, CustomStringConvertible {
    var re: Double
    var im: Double

    init(_ re: Double, _ im: Double = 0) {
        self.re = re
        self.im = im
    }

    init(polar r: Double, _ theta: Double) {
        re = r * cos(theta)
        im = r * sin(theta)
    }

    static let zero = Complex(0, 0)
    static let one = Complex(1, 0)
    static let i = Complex(0, 1)

    var magnitude: Double { hypot(re, im) }
    var phase: Double { atan2(im, re) }
    var conj: Complex { Complex(re, -im) }
    var norm2: Double { re * re + im * im }
    var isFinite: Bool { re.isFinite && im.isFinite }

    func isReal(tolerance: Double = 1e-10) -> Bool {
        abs(im) <= tolerance * max(1, magnitude)
    }

    var description: String {
        im >= 0 ? String(format: "%.6g+%.6gj", re, im) : String(format: "%.6g-%.6gj", re, -im)
    }

    static func + (a: Complex, b: Complex) -> Complex { Complex(a.re + b.re, a.im + b.im) }
    static func - (a: Complex, b: Complex) -> Complex { Complex(a.re - b.re, a.im - b.im) }
    static func * (a: Complex, b: Complex) -> Complex {
        Complex(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re)
    }
    static func / (a: Complex, b: Complex) -> Complex {
        // Smith's algorithm avoids overflow for large or small denominators.
        if abs(b.re) >= abs(b.im) {
            let r = b.im / b.re, d = b.re + b.im * r
            return Complex((a.re + a.im * r) / d, (a.im - a.re * r) / d)
        } else {
            let r = b.re / b.im, d = b.re * r + b.im
            return Complex((a.re * r + a.im) / d, (a.im * r - a.re) / d)
        }
    }
    static prefix func - (a: Complex) -> Complex { Complex(-a.re, -a.im) }

    static func + (a: Complex, b: Double) -> Complex { Complex(a.re + b, a.im) }
    static func + (a: Double, b: Complex) -> Complex { Complex(a + b.re, b.im) }
    static func - (a: Complex, b: Double) -> Complex { Complex(a.re - b, a.im) }
    static func - (a: Double, b: Complex) -> Complex { Complex(a - b.re, -b.im) }
    static func * (a: Complex, b: Double) -> Complex { Complex(a.re * b, a.im * b) }
    static func * (a: Double, b: Complex) -> Complex { Complex(a * b.re, a * b.im) }
    static func / (a: Complex, b: Double) -> Complex { Complex(a.re / b, a.im / b) }
    static func / (a: Double, b: Complex) -> Complex { Complex(a, 0) / b }

    static func += (a: inout Complex, b: Complex) { a = a + b }
    static func -= (a: inout Complex, b: Complex) { a = a - b }
    static func *= (a: inout Complex, b: Complex) { a = a * b }
    static func *= (a: inout Complex, b: Double) { a = a * b }
    static func /= (a: inout Complex, b: Complex) { a = a / b }
}

func cexp(_ z: Complex) -> Complex {
    let e = exp(z.re)
    return Complex(e * cos(z.im), e * sin(z.im))
}

func clog(_ z: Complex) -> Complex {
    Complex(log(z.magnitude), z.phase)
}

/// Principal square root.
func csqrt(_ z: Complex) -> Complex {
    if z.re == 0 && z.im == 0 { return .zero }
    let r = z.magnitude
    let a = sqrt((r + abs(z.re)) / 2)
    if z.re >= 0 {
        return Complex(a, z.im / (2 * a))
    }
    return Complex(abs(z.im) / (2 * a), z.im >= 0 ? a : -a)
}

func csinh(_ z: Complex) -> Complex {
    Complex(sinh(z.re) * cos(z.im), cosh(z.re) * sin(z.im))
}

/// Principal arcsine: −i·log(iz + √(1 − z²)).
func casin(_ z: Complex) -> Complex {
    let w = clog(Complex.i * z + csqrt(1 - z * z))
    return Complex(w.im, -w.re)
}

/// Product of a list of complex numbers.
func cprod(_ zs: [Complex]) -> Complex {
    zs.reduce(Complex.one, *)
}
