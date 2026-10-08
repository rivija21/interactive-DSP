#pragma once
#include "PlotView.h"
#include <complex>
#include <optional>

/// Windowed real FFT giving magnitude in dBFS (a full-scale sine reads 0 dB).
class SpectrumAnalyzer {
public:
    explicit SpectrumAnalyzer(int size);
    const int size;
    /// Power spectrum (linear, |A|^2) of `samples` into out[0...size/2].
    void power(const float* samples, std::vector<float>& out);

private:
    std::vector<float> window_;
    float windowSum_ = 0;
    std::vector<int> bitrev_;
    std::vector<std::complex<double>> twiddle_;
    std::vector<std::complex<double>> buffer_;
};

int fftSize(double fs, double divisor);

/// Maps plot columns to FFT bins (max over the bins a column covers, or interpolation
/// where bins are wider than a column).
struct BinMap {
    std::vector<int> lo, hi;
    std::vector<float> frac;
    BinMap() = default;
    BinMap(const std::vector<std::pair<double, double>>& frequencies, double binHz, int bins);
    float value(size_t column, const float* p) const;
};

/// Live spectrum of the input (grey) and output (teal).
class SpectrumView : public PlotView {
public:
    explicit SpectrumView(LabModel& model);
    /// Forget the averaged spectrum (after the source or filter changes), so the old
    /// sound doesn't linger as a fading ghost.
    void resetSmoothing() {
        inSmooth_.clear();
        outSmooth_.clear();
    }
    void tick();
    void draw(Canvas& c) override;
    void mouseMoved(const MouseEvent& e) override;
    void mouseExited() override;

private:
    void smooth(std::vector<float>& acc, const std::vector<float>& p);
    void ensureMap(Rect r);
    double db(float p) const { return std::max(dbFloor_ - 10, 10 * std::log10(double(std::max(p, 1e-20f)))); }

    std::unique_ptr<SpectrumAnalyzer> analyzer_;
    std::vector<float> samples_, inPower_, outPower_, inSmooth_, outSmooth_;
    std::vector<std::pair<double, double>> columns_;
    BinMap map_;
    std::string mapKey_;
    std::optional<Point> hover_;
    const double dbFloor_ = -120.0;
};

/// Scrolling time-frequency picture (short-time Fourier transform), newest on the right.
class SpectrogramView : public PlotView {
public:
    explicit SpectrogramView(LabModel& model);
    void reset() {
        key_.clear();
        setNeedsDisplay();
    }
    void tick();
    void draw(Canvas& c) override;
    void mouseMoved(const MouseEvent& e) override;
    void mouseExited() override;

private:
    SampleRing& ring();
    void setup(Rect r);
    double frequencyAtY(float y, Rect r) const;
    float yForFrequency(double f, Rect r) const;

    std::unique_ptr<SpectrumAnalyzer> analyzer_;
    std::vector<float> fftFrame_, power_;
    std::vector<uint32_t> pixels_;
    int imageWidth_ = 0, imageHeight_ = 0;
    int writeColumn_ = 0;
    int64_t cursor_ = -1;
    BinMap rowMap_;
    std::string key_;
    int hop_ = 480;
    int pixelVersion_ = 0;
    Com<ID2D1Bitmap> bitmap_;
    int bitmapDevice_ = 0, bitmapVersion_ = -1, bitmapW_ = 0, bitmapH_ = 0;
    std::optional<Point> hover_;
};

/// Input and output waveforms, triggered on the input's rising zero crossing.
class ScopeView : public PlotView {
public:
    explicit ScopeView(LabModel& model);
    void tick();
    void draw(Canvas& c) override;
    void mouseMoved(const MouseEvent& e) override;
    void mouseExited() override;

private:
    double pick(double p, double current) const;
    double outputGain() const { return outScale_ < scale_ ? scale_ / outScale_ : 1; }

    std::vector<float> input_, output_;
    int shownStart_ = 0, shownCount_ = 0;
    double scale_ = 0.5, outScale_ = 0.5;
    struct Trigger {
        std::vector<float> input, output;
        double time;
    };
    std::optional<Trigger> lastTrigger_;
    std::optional<Point> hover_;
};
