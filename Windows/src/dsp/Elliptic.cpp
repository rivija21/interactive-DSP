#include "Elliptic.h"
#include <vector>

double agm(double a0, double b0) {
    double a = a0, b = b0;
    for (int i = 0; i < 64; i++) {
        double an = (a + b) / 2;
        double bn = std::sqrt(a * b);
        if (std::fabs(an - bn) <= 1e-16 * an) return an;
        a = an;
        b = bn;
    }
    return a;
}

double ellipK(double m) { return kPi / (2 * agm(1, std::sqrt(1 - m))); }

double ellipKm1(double p) { return kPi / (2 * agm(1, std::sqrt(p))); }

SnCnDn ellipj(double u, double m) {
    if (m < 1e-9) {
        double t = std::sin(u), b = std::cos(u);
        double ai = 0.25 * m * (u - t * b);
        return {t - ai * b, b + ai * t, 1 - 0.5 * m * t * t};
    }
    if (m >= 0.9999999999) {
        double ai = 0.25 * (1 - m);
        double b = std::cosh(u), t = std::tanh(u);
        double phi = 1 / b;
        double twon = b * std::sinh(u);
        double sn = t + ai * (twon - u) / (b * b);
        ai *= t * phi;
        return {sn, phi - ai * (twon - u), phi + ai * (twon + u)};
    }
    double a[9] = {0}, c[9] = {0};
    a[0] = 1;
    double b = std::sqrt(1 - m);
    c[0] = std::sqrt(m);
    double twon = 1.0;
    int i = 0;
    while (std::fabs(c[i] / a[i]) > 1.11e-16) {
        if (i > 7) break;
        double ai = a[i];
        i += 1;
        c[i] = (ai - b) / 2;
        double t = std::sqrt(ai * b);
        a[i] = (ai + b) / 2;
        b = t;
        twon *= 2;
    }
    double phi = twon * a[i] * u;
    double last = 0.0;
    do {
        double t = c[i] * std::sin(phi) / a[i];
        last = phi;
        phi = (std::asin(t) + phi) / 2;
        i -= 1;
    } while (i > 0);
    double sn = std::sin(phi);
    double cn = std::cos(phi);
    double dnfix = cn / std::cos(phi - last);
    double dn = std::fabs(dnfix) < 0.1 ? std::sqrt(1 - m * sn * sn) : dnfix;
    return {sn, cn, dn};
}

double ellipDegree(int n, double m1) {
    double k1 = ellipK(m1);
    double k1p = ellipKm1(m1);
    double q1 = std::exp(-kPi * k1p / k1);
    double q = std::pow(q1, 1 / double(n));
    double num = 0.0;
    for (int m = 0; m <= 7; m++) num += std::pow(q, double(m * (m + 1)));
    double den = 1.0;
    for (int m = 1; m <= 8; m++) den += 2 * std::pow(q, double(m * m));
    return 16 * q * std::pow(num / den, 4);
}

static double complementReal(double kx) { return std::sqrt((1 - kx) * (1 + kx)); }
static Complex complementComplex(Complex kx) { return csqrt((1.0 - kx) * (1.0 + kx)); }

Complex arcJacSn(Complex w, double m) {
    double k = std::sqrt(m);
    std::vector<double> ks{k};
    int iterations = 0;
    while (ks.back() != 0 && iterations < 12) {
        double kp = complementReal(ks.back());
        ks.push_back((1 - kp) / (1 + kp));
        iterations += 1;
    }
    double prod = 1.0;
    for (size_t i = 1; i < ks.size(); i++) prod *= (1 + ks[i]);
    double bigK = prod * kPi / 2;
    Complex wn = w;
    for (size_t idx = 0; idx + 1 < ks.size(); idx++) {
        double kn = ks[idx], knext = ks[idx + 1];
        wn = 2.0 * wn / ((1 + knext) * (1.0 + complementComplex(kn * wn)));
    }
    Complex u = 2 / kPi * casin(wn);
    return bigK * u;
}

double arcJacSc1(double w, double m) { return arcJacSn(Complex(0, w), m).im; }
