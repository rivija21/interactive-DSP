#include "FIRDesign.h"
#include <utility>

const char* windowTitle(WindowType w) {
    switch (w) {
    case WindowType::rectangular: return "Rectangular";
    case WindowType::hann: return "Hann";
    case WindowType::hamming: return "Hamming";
    case WindowType::blackman: return "Blackman";
    case WindowType::kaiser: return "Kaiser";
    }
    return "";
}

const char* windowRawValue(WindowType w) {
    switch (w) {
    case WindowType::rectangular: return "rectangular";
    case WindowType::hann: return "hann";
    case WindowType::hamming: return "hamming";
    case WindowType::blackman: return "blackman";
    case WindowType::kaiser: return "kaiser";
    }
    return "";
}

std::optional<double> windowTypicalStopbandDB(WindowType w) {
    switch (w) {
    case WindowType::rectangular: return 21;
    case WindowType::hann: return 44;
    case WindowType::hamming: return 53;
    case WindowType::blackman: return 74;
    case WindowType::kaiser: return std::nullopt;
    }
    return std::nullopt;
}

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    double y = x * x / 4;
    for (int k = 1; k < 300; k++) {
        term *= y / double(k * k);
        sum += term;
        if (term < 1e-17 * sum) break;
    }
    return sum;
}

std::vector<double> makeWindow(WindowType type, int n, double beta) {
    if (n <= 1) return {1};
    double d = double(n - 1);
    std::vector<double> w(n);
    for (int i = 0; i < n; i++) {
        double x = double(i);
        switch (type) {
        case WindowType::rectangular: w[i] = 1; break;
        case WindowType::hann: w[i] = 0.5 - 0.5 * std::cos(2 * kPi * x / d); break;
        case WindowType::hamming: w[i] = 0.54 - 0.46 * std::cos(2 * kPi * x / d); break;
        case WindowType::blackman:
            w[i] = 0.42 - 0.5 * std::cos(2 * kPi * x / d) + 0.08 * std::cos(4 * kPi * x / d);
            break;
        case WindowType::kaiser: {
            double r = 2 * x / d - 1;
            w[i] = besselI0(beta * std::sqrt(std::fmax(0.0, 1 - r * r))) / besselI0(beta);
            break;
        }
        }
    }
    return w;
}

double sinc(double x) { return x == 0 ? 1 : std::sin(kPi * x) / (kPi * x); }

std::vector<double> designFIR(BandType band, int n, double f1, double f2, WindowType window, double beta, double fs) {
    double nyq = fs / 2;
    double c1 = f1 / nyq, c2 = f2 / nyq;
    std::vector<std::pair<double, double>> bands;
    switch (band) {
    case BandType::lowpass: bands = {{0, c1}}; break;
    case BandType::highpass: bands = {{c1, 1}}; break;
    case BandType::bandpass: bands = {{c1, c2}}; break;
    case BandType::bandstop: bands = {{0, c1}, {c2, 1}}; break;
    }
    double alpha = 0.5 * double(n - 1);
    std::vector<double> m(n);
    for (int i = 0; i < n; i++) m[i] = double(i) - alpha;
    std::vector<double> h(n, 0.0);
    for (const auto& [left, right] : bands) {
        for (int i = 0; i < n; i++) h[i] += right * sinc(right * m[i]) - left * sinc(left * m[i]);
    }
    auto w = makeWindow(window, n, beta);
    for (int i = 0; i < n; i++) h[i] *= w[i];

    // Scale for exactly 0 dB at the centre of the first passband.
    double left = bands[0].first, right = bands[0].second;
    double scaleFrequency = left == 0 ? 0 : (right == 1 ? 1 : 0.5 * (left + right));
    double s = 0.0;
    for (int i = 0; i < n; i++) s += h[i] * std::cos(kPi * m[i] * scaleFrequency);
    if (s != 0) {
        for (int i = 0; i < n; i++) h[i] /= s;
    }
    return h;
}

double kaiserAttenuation(double beta) {
    // Inverse of beta = 0.1102(A - 8.7) for A > 50, and the mid-range formula below that.
    if (beta > 4.5513) return beta / 0.1102 + 8.7;
    // Solve 0.5842(A-21)^0.4 + 0.07886(A-21) = beta numerically.
    double lo = 21.0, hi = 50.0;
    for (int i = 0; i < 60; i++) {
        double mid = (lo + hi) / 2;
        double b = 0.5842 * std::pow(mid - 21, 0.4) + 0.07886 * (mid - 21);
        if (b < beta) lo = mid; else hi = mid;
    }
    return beta <= 0 ? 21 : (lo + hi) / 2;
}
