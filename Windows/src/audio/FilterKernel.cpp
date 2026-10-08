#include "FilterKernel.h"
#include <algorithm>
#include <cmath>

FilterKernel::FilterKernel(const DigitalFilter& filter)
    : isStable(filter.isStable()), isFIR(filter.structure.isFIR), work_(maxBlock, 0.0) {
    if (!isFIR) {
        const auto& sos = filter.structure.sos;
        sectionCount_ = int(sos.size());
        coeffs_.assign(std::max<size_t>(1, sos.size() * 5), 0.0);
        for (size_t i = 0; i < sos.size(); i++) {
            coeffs_[i * 5 + 0] = sos[i].b0;
            coeffs_[i * 5 + 1] = sos[i].b1;
            coeffs_[i * 5 + 2] = sos[i].b2;
            coeffs_[i * 5 + 3] = sos[i].a1;
            coeffs_[i * 5 + 4] = sos[i].a2;
        }
        state_.assign(std::max<size_t>(1, sos.size() * 2), 0.0);
    } else {
        const auto& h = filter.structure.taps;
        tapCount_ = int(h.size());
        taps_ = h;
        delay_.assign(size_t(2 * tapCount_), 0.0);
    }
}

void FilterKernel::reset() {
    if (isFIR) std::fill(delay_.begin(), delay_.end(), 0.0);
    else std::fill(state_.begin(), state_.end(), 0.0);
}

void FilterKernel::adoptState(const FilterKernel& old) {
    if (isFIR && old.isFIR && old.tapCount_ == tapCount_) {
        delay_ = old.delay_;   // same size: no allocation
        pos_ = old.pos_;
    } else if (!isFIR && !old.isFIR && old.sectionCount_ == sectionCount_) {
        std::copy(old.state_.begin(), old.state_.begin() + sectionCount_ * 2, state_.begin());
    }
}

bool FilterKernel::process(const float* x, float* y, int n) {
    if (!isStable) {
        std::fill(y, y + n, 0.0f);
        return false;
    }
    double* work = work_.data();
    if (isFIR) {
        const int count = tapCount_;
        const double* taps = taps_.data();
        double* delay = delay_.data();
        for (int i = 0; i < n; i++) {
            pos_ = pos_ == 0 ? count - 1 : pos_ - 1;
            double v = double(x[i]);
            delay[pos_] = v;
            delay[pos_ + count] = v;
            const double* d = delay + pos_;
            double acc = 0.0;
            for (int k = 0; k < count; k++) acc += taps[k] * d[k];
            work[i] = acc;
        }
    } else {
        for (int i = 0; i < n; i++) work[i] = double(x[i]);
        for (int s = 0; s < sectionCount_; s++) {
            const double* c = coeffs_.data() + s * 5;
            const double b0 = c[0], b1 = c[1], b2 = c[2], a1 = c[3], a2 = c[4];
            double s1 = state_[s * 2], s2 = state_[s * 2 + 1];
            for (int i = 0; i < n; i++) {
                double xn = work[i];
                double yn = b0 * xn + s1;
                s1 = b1 * xn - a1 * yn + s2;
                s2 = b2 * xn - a2 * yn;
                work[i] = yn;
            }
            // Flush denormals so a decaying tail never slows the CPU down.
            if (std::fabs(s1) < 1e-200) s1 = 0;
            if (std::fabs(s2) < 1e-200) s2 = 0;
            state_[s * 2] = s1;
            state_[s * 2 + 1] = s2;
        }
    }
    double peak = 0.0;
    for (int i = 0; i < n; i++) {
        double a = std::fabs(work[i]);
        if (a > peak || std::isnan(a)) peak = a;
    }
    if (!(std::isfinite(peak) && peak < 1e4)) {
        reset();
        std::fill(y, y + n, 0.0f);
        return false;
    }
    for (int i = 0; i < n; i++) y[i] = float(work[i]);
    return true;
}
