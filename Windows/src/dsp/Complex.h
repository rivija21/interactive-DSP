#pragma once
#include <cmath>
#include <vector>
#include <string>
#include <cstdio>

/// A complex number. Poles, zeros and frequency responses all live here.
struct Complex {
    double re = 0;
    double im = 0;

    constexpr Complex() = default;
    constexpr Complex(double r, double i = 0) : re(r), im(i) {}

    static Complex polar(double r, double theta) { return Complex(r * std::cos(theta), r * std::sin(theta)); }

    double magnitude() const { return std::hypot(re, im); }
    double phase() const { return std::atan2(im, re); }
    Complex conj() const { return Complex(re, -im); }
    double norm2() const { return re * re + im * im; }
    bool isFinite() const { return std::isfinite(re) && std::isfinite(im); }

    bool isReal(double tolerance = 1e-10) const {
        return std::fabs(im) <= tolerance * std::fmax(1.0, magnitude());
    }

    std::string description() const {
        char buf[64];
        if (im >= 0) std::snprintf(buf, sizeof buf, "%.6g+%.6gj", re, im);
        else std::snprintf(buf, sizeof buf, "%.6g-%.6gj", re, -im);
        return buf;
    }

    bool operator==(const Complex& o) const { return re == o.re && im == o.im; }
    bool operator!=(const Complex& o) const { return !(*this == o); }

    Complex& operator+=(const Complex& b) { re += b.re; im += b.im; return *this; }
    Complex& operator-=(const Complex& b) { re -= b.re; im -= b.im; return *this; }
    Complex& operator*=(const Complex& b);
    Complex& operator*=(double b) { re *= b; im *= b; return *this; }
    Complex& operator/=(const Complex& b);
};

inline Complex operator+(Complex a, Complex b) { return Complex(a.re + b.re, a.im + b.im); }
inline Complex operator-(Complex a, Complex b) { return Complex(a.re - b.re, a.im - b.im); }
inline Complex operator*(Complex a, Complex b) {
    return Complex(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}
inline Complex operator/(Complex a, Complex b) {
    // Smith's algorithm avoids overflow for large or small denominators.
    if (std::fabs(b.re) >= std::fabs(b.im)) {
        double r = b.im / b.re, d = b.re + b.im * r;
        return Complex((a.re + a.im * r) / d, (a.im - a.re * r) / d);
    } else {
        double r = b.re / b.im, d = b.re * r + b.im;
        return Complex((a.re * r + a.im) / d, (a.im * r - a.re) / d);
    }
}
inline Complex operator-(Complex a) { return Complex(-a.re, -a.im); }

inline Complex operator+(Complex a, double b) { return Complex(a.re + b, a.im); }
inline Complex operator+(double a, Complex b) { return Complex(a + b.re, b.im); }
inline Complex operator-(Complex a, double b) { return Complex(a.re - b, a.im); }
inline Complex operator-(double a, Complex b) { return Complex(a - b.re, -b.im); }
inline Complex operator*(Complex a, double b) { return Complex(a.re * b, a.im * b); }
inline Complex operator*(double a, Complex b) { return Complex(a * b.re, a * b.im); }
inline Complex operator/(Complex a, double b) { return Complex(a.re / b, a.im / b); }
inline Complex operator/(double a, Complex b) { return Complex(a, 0) / b; }

inline Complex& Complex::operator*=(const Complex& b) { *this = *this * b; return *this; }
inline Complex& Complex::operator/=(const Complex& b) { *this = *this / b; return *this; }

inline Complex cexp(Complex z) {
    double e = std::exp(z.re);
    return Complex(e * std::cos(z.im), e * std::sin(z.im));
}

inline Complex clog(Complex z) { return Complex(std::log(z.magnitude()), z.phase()); }

/// Principal square root.
inline Complex csqrt(Complex z) {
    if (z.re == 0 && z.im == 0) return Complex();
    double r = z.magnitude();
    double a = std::sqrt((r + std::fabs(z.re)) / 2);
    if (z.re >= 0) return Complex(a, z.im / (2 * a));
    return Complex(std::fabs(z.im) / (2 * a), z.im >= 0 ? a : -a);
}

inline Complex csinh(Complex z) {
    return Complex(std::sinh(z.re) * std::cos(z.im), std::cosh(z.re) * std::sin(z.im));
}

/// Principal arcsine: -i*log(iz + sqrt(1 - z^2)).
inline Complex casin(Complex z) {
    Complex w = clog(Complex(0, 1) * z + csqrt(1.0 - z * z));
    return Complex(w.im, -w.re);
}

/// Product of a list of complex numbers.
inline Complex cprod(const std::vector<Complex>& zs) {
    Complex acc(1, 0);
    for (const auto& z : zs) acc = acc * z;
    return acc;
}

/// Maps a list through a function.
template <class T, class F>
inline auto mapv(const std::vector<T>& xs, F f) {
    std::vector<decltype(f(xs[0]))> out;
    out.reserve(xs.size());
    for (const auto& x : xs) out.push_back(f(x));
    return out;
}

inline std::vector<Complex> negated(const std::vector<Complex>& xs) {
    return mapv(xs, [](const Complex& z) { return -z; });
}

inline constexpr double kPi = 3.14159265358979323846;
