#include "Polynomial.h"
#include <algorithm>
#include <optional>

std::vector<Complex> polyFromRoots(const std::vector<Complex>& roots) {
    std::vector<Complex> c{Complex(1, 0)};
    for (const auto& r : roots) {
        std::vector<Complex> next(c.size() + 1, Complex());
        for (size_t k = 0; k < c.size(); k++) {
            next[k] += c[k];
            next[k + 1] -= c[k] * r;
        }
        c = std::move(next);
    }
    return c;
}

std::vector<double> realPolyFromRoots(const std::vector<Complex>& roots) {
    return mapv(polyFromRoots(roots), [](const Complex& z) { return z.re; });
}

Complex horner(const std::vector<Complex>& c, Complex z) {
    Complex acc;
    for (const auto& ck : c) acc = acc * z + ck;
    return acc;
}

Complex horner(const std::vector<double>& c, Complex z) {
    Complex acc;
    for (double ck : c) acc = acc * z + ck;
    return acc;
}

std::vector<Complex> polyRoots(const std::vector<double>& coeffs) {
    auto roots = polyRootsComplex(mapv(coeffs, [](double v) { return Complex(v); }));
    return pairConjugates(roots);
}

std::vector<Complex> polyRootsComplex(std::vector<Complex> c) {
    while (!c.empty() && c.front().magnitude() == 0) c.erase(c.begin());
    std::vector<Complex> roots;
    while (c.size() > 1 && c.back().magnitude() == 0) {
        c.pop_back();
        roots.push_back(Complex());
    }
    int n = (int)c.size() - 1;
    if (n < 1) return roots;
    Complex lead = c[0];
    for (auto& v : c) v = v / lead;

    if (n == 1) {
        roots.push_back(-c[1]);
        return roots;
    }
    if (n == 2) {
        Complex b = c[1], cc = c[2];
        Complex disc = csqrt(b * b - 4.0 * cc);
        // Pick the sign that avoids cancellation, then use Vieta for the other root.
        Complex q = (b.re * disc.re + b.im * disc.im) >= 0 ? -0.5 * (b + disc) : -0.5 * (b - disc);
        if (q.magnitude() == 0) {
            roots.push_back(Complex());
            roots.push_back(Complex());
            return roots;
        }
        roots.push_back(q);
        roots.push_back(cc / q);
        return roots;
    }

    std::vector<Complex> dc(n);
    for (int k = 0; k < n; k++) dc[k] = c[k] * double(n - k);
    // Reversed polynomial q(w) = w^n p(1/w), used for |z| > 1 so high powers never overflow.
    std::vector<Complex> rc(c.rbegin(), c.rend());
    std::vector<Complex> rdc(n);
    for (int k = 0; k < n; k++) rdc[k] = rc[k] * double(n - k);

    // Newton correction p(z)/p'(z), evaluated stably inside or outside the unit circle.
    auto newtonRatio = [&](Complex z) -> std::optional<Complex> {
        if (z.norm2() <= 1) {
            Complex p = horner(c, z);
            if (p.norm2() == 0) return std::nullopt;
            Complex dp = horner(dc, z);
            return dp.norm2() == 0 ? Complex(1e-12) : p / dp;
        }
        // p/p' = z / (n - w q'(w)/q(w)) with w = 1/z.
        Complex w = 1.0 / z;
        Complex q = horner(rc, w);
        if (q.norm2() == 0) return std::nullopt;
        Complex dq = horner(rdc, w);
        Complex den = double(n) - w * dq / q;
        return den.norm2() == 0 ? Complex(1e-12) : z / den;
    };

    // Start on a circle whose radius is the geometric mean of the root magnitudes.
    double radius = std::max(1e-6, std::pow(c[n].magnitude(), 1.0 / double(n)));
    std::vector<Complex> z(n);
    for (int k = 0; k < n; k++) z[k] = Complex::polar(radius, 2 * kPi * double(k) / double(n) + 0.4);
    std::vector<bool> done(n, false);

    for (int iter = 0; iter < 600; iter++) {
        double maxStep = 0;
        for (int k = 0; k < n; k++) {
            if (done[k]) continue;
            Complex zk = z[k];
            auto ratio = newtonRatio(zk);
            if (!ratio) {
                done[k] = true;
                continue;
            }
            Complex s;
            for (int j = 0; j < n; j++) {
                if (j == k) continue;
                Complex d = zk - z[j];
                if (d.norm2() > 0) s += 1.0 / d;
            }
            Complex w = *ratio / (1.0 - *ratio * s);
            if (!w.isFinite()) continue;
            z[k] = zk - w;
            double step = w.magnitude() / std::max(1.0, z[k].magnitude());
            if (step < 1e-15) done[k] = true;
            maxStep = std::max(maxStep, step);
        }
        if (maxStep < 1e-14) break;
    }

    // Polish each root with Newton's method.
    for (int k = 0; k < n; k++) {
        for (int i = 0; i < 3; i++) {
            auto step = newtonRatio(z[k]);
            if (!step || !step->isFinite()) break;
            z[k] -= *step;
        }
    }
    roots.insert(roots.end(), z.begin(), z.end());
    return roots;
}

std::vector<Complex> pairConjugates(const std::vector<Complex>& roots, double tolerance) {
    std::vector<Complex> result, upper, lower;
    for (const auto& r : roots) {
        if (std::fabs(r.im) <= tolerance * std::max(1.0, r.magnitude())) {
            result.push_back(Complex(r.re, 0));
        } else if (r.im > 0) {
            upper.push_back(r);
        } else {
            lower.push_back(r);
        }
    }
    while (!upper.empty() && !lower.empty()) {
        Complex u = upper.back();
        upper.pop_back();
        size_t idx = 0;
        for (size_t i = 1; i < lower.size(); i++) {
            if ((lower[i] - u.conj()).magnitude() < (lower[idx] - u.conj()).magnitude()) idx = i;
        }
        Complex l = lower[idx];
        lower.erase(lower.begin() + idx);
        Complex avg((u.re + l.re) / 2, (u.im - l.im) / 2);
        result.push_back(avg);
        result.push_back(avg.conj());
    }
    result.insert(result.end(), upper.begin(), upper.end());
    result.insert(result.end(), lower.begin(), lower.end());
    return result;
}
