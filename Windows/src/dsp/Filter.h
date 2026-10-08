#pragma once
#include "Complex.h"
#include "IIRDesign.h"
#include <optional>
#include <vector>

/// A digital filter as zeros, poles and gain: H(z) = k prod(z - z_i) / prod(z - p_i).
struct ZPK {
    std::vector<Complex> zeros;
    std::vector<Complex> poles;
    double gain = 1;

    double maxPoleRadius() const {
        double m = 0;
        bool any = false;
        for (const auto& p : poles) {
            double r = p.magnitude();
            if (!any || r > m) m = r;
            any = true;
        }
        return any ? m : 0;
    }
    bool isStable() const { return maxPoleRadius() < 1 - 1e-9; }

    Complex response(Complex z) const {
        Complex h(gain);
        for (const auto& q : zeros) h *= z - q;
        for (const auto& p : poles) h /= z - p;
        return h;
    }
    bool operator==(const ZPK& o) const = default;
};

/// One second-order section: (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2).
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    Complex response(Complex zInv) const {
        Complex zInv2 = zInv * zInv;
        Complex num = b0 + b1 * zInv + b2 * zInv2;
        Complex den = 1.0 + a1 * zInv + a2 * zInv2;
        return num / den;
    }
    /// Group delay in samples at angle w.
    double groupDelay(double w) const;
    bool operator==(const Biquad& o) const = default;
};

/// Group delay of B(z) = sum b_k z^-k at angle w: Re{ sum k b_k e^{-jwk} / sum b_k e^{-jwk} }.
double polyGroupDelay(const std::vector<double>& b, double w);

/// Groups roots into conjugate pairs (positive-imaginary member first) and real roots.
void splitConjugates(const std::vector<Complex>& roots, std::vector<Complex>& pairs, std::vector<double>& reals);

/// Converts ZPK to a cascade of biquads. Poles closest to the unit circle are paired with
/// their nearest zeros, which keeps each section's gain well behaved.
std::vector<Biquad> zpkToSOS(const ZPK& zpk);

/// Direct-form numerator and denominator coefficients b[k], a[k] (a[0] = 1) of the whole filter.
void sosToTransferFunction(const std::vector<Biquad>& sos, std::vector<double>& b, std::vector<double>& a);

/// The runnable form of a filter: either IIR (a biquad cascade) or FIR (taps).
struct FilterStructure {
    bool isFIR = false;
    std::vector<Biquad> sos;
    std::vector<double> taps;
    static FilterStructure iir(std::vector<Biquad> s) { FilterStructure f; f.sos = std::move(s); return f; }
    static FilterStructure fir(std::vector<double> h) { FilterStructure f; f.isFIR = true; f.taps = std::move(h); return f; }
    bool operator==(const FilterStructure& o) const = default;
};

/// A designed digital filter with everything needed to draw, explain and run it.
struct DigitalFilter {
    ZPK zpk;
    FilterStructure structure;
    double sampleRate = 48000;

    static DigitalFilter passthrough(double fs);

    bool isStable() const { return structure.isFIR ? true : zpk.isStable(); }
    int order() const;
    /// Multiplications per output sample in the implemented structure.
    int multipliesPerSample() const;
    /// Complex frequency response at f Hz.
    Complex response(double f) const;
    double magnitudeDB(double f) const;
    /// Group delay (samples) at f Hz.
    double groupDelay(double f) const;
    /// Simulates the filter on an input sequence (double precision).
    std::vector<double> simulate(const std::vector<double>& x) const;
    std::vector<double> impulseResponse(int length) const;
    std::vector<double> stepResponse(int length) const;
    /// A sensible number of samples to show for the impulse response.
    int suggestedResponseLength() const;
};

// MARK: - Measurements

struct FilterMeasurements {
    std::vector<double> minus3dB;   // frequencies (Hz) where the response crosses -3 dB
    double passbandPeakDB = 0;
    double groupDelayMs = 0;        // in the passband
};

/// Finds -3 dB crossings by scanning a log grid and refining with bisection.
FilterMeasurements measure(const DigitalFilter& filter, std::optional<BandType> band);
