#include "IIRDesign.h"
#include "Elliptic.h"
#include "Filter.h"
#include "Polynomial.h"

const char* bandTitle(BandType b) {
    switch (b) {
    case BandType::lowpass: return "Low-pass";
    case BandType::highpass: return "High-pass";
    case BandType::bandpass: return "Band-pass";
    case BandType::bandstop: return "Band-stop";
    }
    return "";
}

const char* bandRawValue(BandType b) {
    switch (b) {
    case BandType::lowpass: return "lowpass";
    case BandType::highpass: return "highpass";
    case BandType::bandpass: return "bandpass";
    case BandType::bandstop: return "bandstop";
    }
    return "";
}

const char* familyTitle(IIRFamily f) {
    switch (f) {
    case IIRFamily::butterworth: return "Butterworth";
    case IIRFamily::chebyshev1: return "Chebyshev I";
    case IIRFamily::chebyshev2: return "Chebyshev II";
    case IIRFamily::elliptic: return "Elliptic (Cauer)";
    case IIRFamily::bessel: return "Bessel";
    }
    return "";
}

const char* familyRawValue(IIRFamily f) {
    switch (f) {
    case IIRFamily::butterworth: return "butterworth";
    case IIRFamily::chebyshev1: return "chebyshev1";
    case IIRFamily::chebyshev2: return "chebyshev2";
    case IIRFamily::elliptic: return "elliptic";
    case IIRFamily::bessel: return "bessel";
    }
    return "";
}

const char* familyCutoffMeaning(IIRFamily f) {
    switch (f) {
    case IIRFamily::butterworth: return "\xE2\x88\x92" "3 dB point";
    case IIRFamily::chebyshev1:
    case IIRFamily::elliptic: return "edge of the ripple band";
    case IIRFamily::chebyshev2: return "start of the stopband";
    case IIRFamily::bessel: return "phase midpoint";
    }
    return "";
}

// MARK: - Analog low-pass prototypes (cutoff 1 rad/s)

AnalogZPK buttap(int n) {
    AnalogZPK a;
    for (int m = -n + 1; m < n; m += 2) a.p.push_back(-cexp(Complex(0, kPi * double(m) / double(2 * n))));
    a.k = 1;
    return a;
}

AnalogZPK cheb1ap(int n, double rp) {
    double eps = std::sqrt(std::pow(10.0, 0.1 * rp) - 1);
    double mu = std::asinh(1 / eps) / double(n);
    AnalogZPK a;
    for (int m = -n + 1; m < n; m += 2) a.p.push_back(-csinh(Complex(mu, kPi * double(m) / double(2 * n))));
    double k = cprod(negated(a.p)).re;
    if (n % 2 == 0) k /= std::sqrt(1 + eps * eps);
    a.k = k;
    return a;
}

AnalogZPK cheb2ap(int n, double rs) {
    double de = 1 / std::sqrt(std::pow(10.0, 0.1 * rs) - 1);
    double mu = std::asinh(1 / de) / double(n);
    std::vector<int> ms;
    if (n % 2 == 1) {
        for (int m = -n + 1; m < 0; m += 2) ms.push_back(m);
        for (int m = 2; m < n; m += 2) ms.push_back(m);
    } else {
        for (int m = -n + 1; m < n; m += 2) ms.push_back(m);
    }
    AnalogZPK a;
    for (int m : ms) a.z.push_back(Complex(0, 1 / std::sin(double(m) * kPi / double(2 * n))));
    for (int m = -n + 1; m < n; m += 2) {
        Complex b = -cexp(Complex(0, kPi * double(m) / double(2 * n)));
        a.p.push_back(1.0 / Complex(std::sinh(mu) * b.re, std::cosh(mu) * b.im));
    }
    a.k = (cprod(negated(a.p)) / cprod(negated(a.z))).re;
    return a;
}

AnalogZPK ellipap(int n, double rp, double rs) {
    if (n == 1) {
        double p = -std::sqrt(1 / (std::pow(10.0, 0.1 * rp) - 1));
        AnalogZPK a;
        a.p = {Complex(p)};
        a.k = -p;
        return a;
    }
    double epsSq = std::pow(10.0, 0.1 * rp) - 1;
    double eps = std::sqrt(epsSq);
    double ck1Sq = epsSq / (std::pow(10.0, 0.1 * rs) - 1);
    double val0 = ellipK(ck1Sq);
    double m = ellipDegree(n, ck1Sq);
    double capk = ellipK(m);

    std::vector<int> js;
    for (int j = 1 - n % 2; j < n; j += 2) js.push_back(j);
    std::vector<SnCnDn> sncndn;
    for (int j : js) sncndn.push_back(ellipj(double(j) * capk / double(n), m));
    std::vector<Complex> z;
    for (const auto& v : sncndn) {
        if (std::fabs(v.sn) > 2e-16) z.push_back(Complex(0, 1 / (std::sqrt(m) * v.sn)));
    }
    {
        size_t count = z.size();
        for (size_t i = 0; i < count; i++) z.push_back(z[i].conj());
    }

    double r = arcJacSc1(1 / eps, ck1Sq);
    double v0 = capk * r / (double(n) * val0);
    SnCnDn s = ellipj(v0, 1 - m);
    double sv = s.sn, cv = s.cn, dv = s.dn;
    std::vector<Complex> p;
    for (const auto& v : sncndn) {
        p.push_back(-Complex(v.cn * v.dn * sv * cv, v.sn * dv) / (1 - (v.dn * sv) * (v.dn * sv)));
    }
    if (n % 2 == 1) {
        double sum = 0;
        for (const auto& q : p) sum += q.norm2();
        double scale = std::sqrt(sum);
        std::vector<Complex> newp;
        for (const auto& q : p)
            if (std::fabs(q.im) > 2e-16 * scale) newp.push_back(q);
        for (const auto& q : newp) p.push_back(q.conj());
    } else {
        size_t count = p.size();
        for (size_t i = 0; i < count; i++) p.push_back(p[i].conj());
    }
    double k = (cprod(negated(p)) / cprod(negated(z))).re;
    if (n % 2 == 0) k /= std::sqrt(1 + epsSq);
    AnalogZPK a;
    a.z = z;
    a.p = p;
    a.k = k;
    return a;
}

/// Bessel-Thomson prototype, normalised like SciPy's norm="phase".
AnalogZPK besselap(int n) {
    // Reverse Bessel polynomial theta_n(s) = sum a_k s^k, a_k = (2n-k)! / (2^(n-k) k! (n-k)!).
    auto factorial = [](int x) {
        double r = 1.0;
        for (int i = 2; i <= x; i++) r *= double(i);
        return r;
    };
    std::vector<double> coeffs;
    for (int k = n; k >= 0; k--) {
        coeffs.push_back(factorial(2 * n - k) / (std::pow(2.0, double(n - k)) * factorial(k) * factorial(n - k)));
    }
    double a0 = coeffs.back();
    double scale = std::pow(a0, -1 / double(n));
    AnalogZPK a;
    for (const auto& r : polyRoots(coeffs)) a.p.push_back(r * scale);
    a.k = 1;
    return a;
}

AnalogZPK analogPrototype(IIRFamily family, int n, double rp, double rs) {
    switch (family) {
    case IIRFamily::butterworth: return buttap(n);
    case IIRFamily::chebyshev1: return cheb1ap(n, rp);
    case IIRFamily::chebyshev2: return cheb2ap(n, rs);
    case IIRFamily::elliptic: return ellipap(n, rp, rs);
    case IIRFamily::bessel: return besselap(n);
    }
    return buttap(n);
}

// MARK: - Frequency transformations (s-domain)

AnalogZPK lp2lp(const AnalogZPK& a, double wo) {
    int degree = int(a.p.size()) - int(a.z.size());
    AnalogZPK r;
    for (const auto& z : a.z) r.z.push_back(z * wo);
    for (const auto& p : a.p) r.p.push_back(p * wo);
    r.k = a.k * std::pow(wo, double(degree));
    return r;
}

AnalogZPK lp2hp(const AnalogZPK& a, double wo) {
    int degree = int(a.p.size()) - int(a.z.size());
    AnalogZPK r;
    for (const auto& z : a.z) r.z.push_back(wo / z);
    for (int i = 0; i < degree; i++) r.z.push_back(Complex());
    for (const auto& p : a.p) r.p.push_back(wo / p);
    r.k = a.k * (cprod(negated(a.z)) / cprod(negated(a.p))).re;
    return r;
}

AnalogZPK lp2bp(const AnalogZPK& a, double wo, double bw) {
    int degree = int(a.p.size()) - int(a.z.size());
    auto split = [&](const std::vector<Complex>& xs) {
        std::vector<Complex> lp, roots, out;
        for (const auto& x : xs) lp.push_back(x * (bw / 2));
        for (const auto& l : lp) roots.push_back(csqrt(l * l - wo * wo));
        for (size_t i = 0; i < lp.size(); i++) out.push_back(lp[i] + roots[i]);
        for (size_t i = 0; i < lp.size(); i++) out.push_back(lp[i] - roots[i]);
        return out;
    };
    AnalogZPK r;
    r.z = split(a.z);
    for (int i = 0; i < degree; i++) r.z.push_back(Complex());
    r.p = split(a.p);
    r.k = a.k * std::pow(bw, double(degree));
    return r;
}

AnalogZPK lp2bs(const AnalogZPK& a, double wo, double bw) {
    int degree = int(a.p.size()) - int(a.z.size());
    auto split = [&](const std::vector<Complex>& xs) {
        std::vector<Complex> hp, roots, out;
        for (const auto& x : xs) hp.push_back((bw / 2) / x);
        for (const auto& h : hp) roots.push_back(csqrt(h * h - wo * wo));
        for (size_t i = 0; i < hp.size(); i++) out.push_back(hp[i] + roots[i]);
        for (size_t i = 0; i < hp.size(); i++) out.push_back(hp[i] - roots[i]);
        return out;
    };
    AnalogZPK r;
    r.z = split(a.z);
    for (int i = 0; i < degree; i++) r.z.push_back(Complex(0, wo));
    for (int i = 0; i < degree; i++) r.z.push_back(Complex(0, -wo));
    r.p = split(a.p);
    r.k = a.k * (cprod(negated(a.z)) / cprod(negated(a.p))).re;
    return r;
}

ZPK bilinear(const AnalogZPK& a, double fs) {
    int degree = int(a.p.size()) - int(a.z.size());
    double fs2 = 2 * fs;
    std::vector<Complex> z, p, zn, pn;
    for (const auto& q : a.z) z.push_back((fs2 + q) / (fs2 - q));
    for (int i = 0; i < degree; i++) z.push_back(Complex(-1));
    for (const auto& q : a.p) p.push_back((fs2 + q) / (fs2 - q));
    for (const auto& q : a.z) zn.push_back(fs2 - q);
    for (const auto& q : a.p) pn.push_back(fs2 - q);
    double k = a.k * (cprod(zn) / cprod(pn)).re;
    return ZPK{pairConjugates(z), pairConjugates(p), k};
}

ZPK designIIR(BandType band, IIRFamily family, int order, double f1, double f2, double rp, double rs, double fs) {
    AnalogZPK proto = analogPrototype(family, order, rp, rs);
    AnalogZPK analog;
    switch (band) {
    case BandType::lowpass: analog = lp2lp(proto, prewarp(f1, fs)); break;
    case BandType::highpass: analog = lp2hp(proto, prewarp(f1, fs)); break;
    case BandType::bandpass:
    case BandType::bandstop: {
        double w1 = prewarp(f1, fs), w2 = prewarp(f2, fs);
        double wo = std::sqrt(w1 * w2), bw = w2 - w1;
        analog = band == BandType::bandpass ? lp2bp(proto, wo, bw) : lp2bs(proto, wo, bw);
        break;
    }
    }
    return bilinear(analog, fs);
}
