#pragma once
#include "../dsp/Filter.h"
#include <vector>

/// A filter ready to run on the audio thread: coefficients and state in plain memory,
/// processed in double precision. IIR filters run as a cascade of transposed direct-form II
/// biquads; FIR filters as a dot product over a doubled circular delay line.
class FilterKernel {
public:
    static constexpr int maxBlock = 2048;

    explicit FilterKernel(const DigitalFilter& filter);

    const bool isStable;
    const bool isFIR;

    void reset();
    /// Carries the filter memory over from the kernel being replaced, so that moving a
    /// slider changes the sound smoothly instead of clicking.
    void adoptState(const FilterKernel& old);
    /// Filters n <= maxBlock samples. Returns false (and outputs silence) if the filter
    /// is unstable or its output ran away.
    bool process(const float* x, float* y, int n);

private:
    int sectionCount_ = 0;
    std::vector<double> coeffs_;
    std::vector<double> state_;
    int tapCount_ = 0;
    std::vector<double> taps_;
    std::vector<double> delay_;
    int pos_ = 0;
    std::vector<double> work_;
};
