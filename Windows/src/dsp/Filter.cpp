#include "Filter.h"
#include "Polynomial.h"
#include <algorithm>
#include <limits>

double Biquad::groupDelay(double w) const {
    return polyGroupDelay({b0, b1, b2}, w) - polyGroupDelay({1, a1, a2}, w);
}

double polyGroupDelay(const std::vector<double>& b, double w) {
    Complex num, den;
    for (size_t k = 0; k < b.size(); k++) {
        double bk = b[k];
        if (bk == 0) continue;
        Complex e = Complex::polar(1, -w * double(k));
        num += e * (double(k) * bk);
        den += e * bk;
    }
    Complex r = num / den;
    return std::isfinite(r.re) ? r.re : 0;
}

void splitConjugates(const std::vector<Complex>& roots, std::vector<Complex>& pairs, std::vector<double>& reals) {
    pairs.clear();
    reals.clear();
    for (const auto& r : roots) {
        if (r.isReal(1e-9)) {
            reals.push_back(r.re);
        } else if (r.im > 0) {
            pairs.push_back(r);
        }
    }
}

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

double distanceToCircle(Complex z) { return std::fabs(1 - z.magnitude()); }

Biquad makeSection(const std::vector<Complex>& zeros, const std::vector<Complex>& poles) {
    auto b = realPolyFromRoots(zeros);
    b.resize(3, 0.0);
    auto a = realPolyFromRoots(poles);
    a.resize(3, 0.0);
    return Biquad{b[0], b[1], b[2], a[1], a[2]};
}

/// Index of the first minimum of key(i) over [0, n), or -1.
template <class F>
int argmin(size_t n, F key) {
    int best = -1;
    double bestV = 0;
    for (size_t i = 0; i < n; i++) {
        double v = key(i);
        if (best < 0 || v < bestV) {
            best = int(i);
            bestV = v;
        }
    }
    return best;
}

} // namespace

std::vector<Biquad> zpkToSOS(const ZPK& zpk) {
    std::vector<Complex> pPairs, zPairs;
    std::vector<double> pReals, zReals;
    splitConjugates(zpk.poles, pPairs, pReals);
    splitConjugates(zpk.zeros, zPairs, zReals);

    std::vector<Biquad> sections;

    auto takeZeros = [&](Complex target) -> std::vector<Complex> {
        // Prefer a conjugate pair or two reals (whichever is nearer), else whatever is left.
        int bestPair = argmin(zPairs.size(), [&](size_t i) { return (zPairs[i] - target).magnitude(); });
        int bestReal = argmin(zReals.size(), [&](size_t i) { return std::fabs(zReals[i] - target.re) + std::fabs(target.im); });
        double pairDist = bestPair >= 0 ? (zPairs[bestPair] - target).magnitude() : kInf;
        double realDist = bestReal >= 0 ? (Complex(zReals[bestReal]) - target).magnitude() : kInf;
        if (bestPair >= 0 && (pairDist <= realDist || zReals.empty())) {
            Complex z = zPairs[bestPair];
            zPairs.erase(zPairs.begin() + bestPair);
            return {z, z.conj()};
        }
        if (bestReal >= 0) {
            double first = zReals[bestReal];
            zReals.erase(zReals.begin() + bestReal);
            int ir2 = argmin(zReals.size(), [&](size_t i) { return std::fabs(zReals[i] - target.re); });
            if (ir2 >= 0) {
                double second = zReals[ir2];
                zReals.erase(zReals.begin() + ir2);
                return {Complex(first), Complex(second)};
            }
            return {Complex(first)};
        }
        return {};
    };

    while (!pPairs.empty() || !pReals.empty()) {
        int bestPair = argmin(pPairs.size(), [&](size_t i) { return distanceToCircle(pPairs[i]); });
        int bestReal = argmin(pReals.size(), [&](size_t i) { return std::fabs(1 - std::fabs(pReals[i])); });
        double pairDist = bestPair >= 0 ? distanceToCircle(pPairs[bestPair]) : kInf;
        double realDist = bestReal >= 0 ? std::fabs(1 - std::fabs(pReals[bestReal])) : kInf;
        std::vector<Complex> poles;
        if (bestPair >= 0 && pairDist <= realDist) {
            Complex p = pPairs[bestPair];
            pPairs.erase(pPairs.begin() + bestPair);
            poles = {p, p.conj()};
        } else {
            double p = pReals[bestReal];
            pReals.erase(pReals.begin() + bestReal);
            poles = {Complex(p)};
            int ir2 = argmin(pReals.size(), [&](size_t i) { return std::fabs(1 - std::fabs(pReals[i])); });
            if (ir2 >= 0) {
                poles.push_back(Complex(pReals[ir2]));
                pReals.erase(pReals.begin() + ir2);
            }
        }
        auto zeros = takeZeros(poles[0]);
        // A single real pole should not swallow a complex pair if a real zero exists.
        if (poles.size() == 1 && zeros.size() == 2 && !zeros[0].isReal() && !zReals.empty()) {
            zPairs.push_back(zeros[0].im > 0 ? zeros[0] : zeros[1]);
            zeros = {Complex(zReals.front())};
            zReals.erase(zReals.begin());
        }
        sections.push_back(makeSection(zeros, poles));
    }
    // Zeros left over (more zeros than poles) become FIR-like sections.
    while (!zPairs.empty() || !zReals.empty()) {
        auto zeros = takeZeros(Complex(1));
        sections.push_back(makeSection(zeros, {}));
    }
    if (sections.empty()) sections.push_back(Biquad{1, 0, 0, 0, 0});
    sections[0].b0 *= zpk.gain;
    sections[0].b1 *= zpk.gain;
    sections[0].b2 *= zpk.gain;
    return sections;
}

void sosToTransferFunction(const std::vector<Biquad>& sos, std::vector<double>& b, std::vector<double>& a) {
    auto mul = [](const std::vector<double>& x, const std::vector<double>& y) {
        std::vector<double> r(x.size() + y.size() - 1, 0.0);
        for (size_t i = 0; i < x.size(); i++)
            for (size_t j = 0; j < y.size(); j++) r[i + j] += x[i] * y[j];
        return r;
    };
    b = {1};
    a = {1};
    for (const auto& s : sos) {
        b = mul(b, {s.b0, s.b1, s.b2});
        a = mul(a, {1, s.a1, s.a2});
    }
    while (b.size() > 1 && b.back() == 0) b.pop_back();
    while (a.size() > 1 && a.back() == 0) a.pop_back();
}

DigitalFilter DigitalFilter::passthrough(double fs) {
    DigitalFilter f;
    f.zpk = ZPK{{}, {}, 1};
    f.structure = FilterStructure::iir({Biquad{1, 0, 0, 0, 0}});
    f.sampleRate = fs;
    return f;
}

int DigitalFilter::order() const {
    if (structure.isFIR) return int(structure.taps.size()) - 1;
    return int(std::max(zpk.poles.size(), zpk.zeros.size()));
}

int DigitalFilter::multipliesPerSample() const {
    if (structure.isFIR) return int(structure.taps.size());
    return int(structure.sos.size()) * 5;
}

Complex DigitalFilter::response(double f) const {
    double w = 2 * kPi * f / sampleRate;
    if (!structure.isFIR) {
        Complex zInv = Complex::polar(1, -w);
        Complex acc(1, 0);
        for (const auto& s : structure.sos) acc = acc * s.response(zInv);
        return acc;
    }
    Complex acc;
    Complex rot = Complex::polar(1, -w);
    Complex e(1, 0);
    const auto& h = structure.taps;
    for (size_t k = 0; k < h.size(); k++) {
        if (k % 32 == 0) e = Complex::polar(1, -w * double(k));
        acc += e * h[k];
        e *= rot;
    }
    return acc;
}

double DigitalFilter::magnitudeDB(double f) const {
    return 20 * std::log10(std::max(response(f).magnitude(), 1e-12));
}

double DigitalFilter::groupDelay(double f) const {
    double w = 2 * kPi * f / sampleRate;
    if (structure.isFIR) return polyGroupDelay(structure.taps, w);
    double sum = 0;
    for (const auto& s : structure.sos) sum += s.groupDelay(w);
    return sum;
}

std::vector<double> DigitalFilter::simulate(const std::vector<double>& x) const {
    if (!structure.isFIR) {
        std::vector<double> y = x;
        for (const auto& s : structure.sos) {
            double s1 = 0.0, s2 = 0.0;
            for (size_t n = 0; n < y.size(); n++) {
                double xn = y[n];
                double yn = s.b0 * xn + s1;
                s1 = s.b1 * xn - s.a1 * yn + s2;
                s2 = s.b2 * xn - s.a2 * yn;
                y[n] = yn;
            }
        }
        return y;
    }
    const auto& h = structure.taps;
    std::vector<double> y(x.size());
    for (size_t n = 0; n < x.size(); n++) {
        double acc = 0.0;
        size_t kmax = std::min(h.size(), n + 1);
        for (size_t k = 0; k < kmax; k++) acc += h[k] * x[n - k];
        y[n] = acc;
    }
    return y;
}

std::vector<double> DigitalFilter::impulseResponse(int length) const {
    std::vector<double> x(std::max(0, length), 0.0);
    if (length > 0) x[0] = 1;
    return simulate(x);
}

std::vector<double> DigitalFilter::stepResponse(int length) const {
    return simulate(std::vector<double>(std::max(0, length), 1.0));
}

int DigitalFilter::suggestedResponseLength() const {
    if (structure.isFIR) return std::max(16, int(structure.taps.size()) + 8);
    if (!isStable()) return 120;
    double r = zpk.maxPoleRadius();
    if (r <= 0) return 32;
    // Samples until the slowest pole decays to 0.1 % (-60 dB).
    double n = std::log(1e-3) / std::log(r);
    return int(std::min(4096.0, std::max(32.0, n * 1.2)));
}

FilterMeasurements measure(const DigitalFilter& filter, std::optional<BandType>) {
    double fs = filter.sampleRate;
    double fMin = 1.0, fMax = fs / 2 * 0.9999;
    const int n = 2000;
    std::vector<double> grid(n + 1), mags(n + 1);
    for (int i = 0; i <= n; i++) {
        grid[i] = fMin * std::pow(fMax / fMin, double(i) / double(n));
        mags[i] = filter.magnitudeDB(grid[i]);
    }
    FilterMeasurements m;
    m.passbandPeakDB = *std::max_element(mags.begin(), mags.end());
    double level = m.passbandPeakDB - 3.0103;
    for (size_t i = 1; i < grid.size(); i++) {
        if (!((mags[i - 1] - level) * (mags[i] - level) < 0)) continue;
        double lo = grid[i - 1], hi = grid[i];
        bool rising = mags[i] > mags[i - 1];
        for (int k = 0; k < 40; k++) {
            double mid = std::sqrt(lo * hi);
            double v = filter.magnitudeDB(mid);
            if ((v > level) == rising) hi = mid; else lo = mid;
        }
        m.minus3dB.push_back(std::sqrt(lo * hi));
        if (m.minus3dB.size() >= 6) break;
    }
    size_t peakIndex = size_t(std::max_element(mags.begin(), mags.end()) - mags.begin());
    m.groupDelayMs = filter.groupDelay(grid[peakIndex]) / fs * 1000;
    return m;
}
