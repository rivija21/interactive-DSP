#include "LiveViews.h"
#include "../platform/Dispatch.h"
#include "../util/Format.h"
#include "../util/Text.h"
#include <algorithm>
#include <cmath>

// MARK: - FFT

SpectrumAnalyzer::SpectrumAnalyzer(int n) : size(n), window_(size_t(n)), bitrev_(size_t(n)), buffer_(size_t(n)) {
    // Periodic Hann window, like vDSP_hann_window(..., vDSP_HANN_DENORM).
    windowSum_ = 0;
    for (int i = 0; i < n; i++) {
        window_[size_t(i)] = float(0.5 * (1 - std::cos(2 * kPi * double(i) / double(n))));
        windowSum_ += window_[size_t(i)];
    }
    int bits = 0;
    while ((1 << bits) < n) bits++;
    for (int i = 0; i < n; i++) {
        int r = 0;
        for (int b = 0; b < bits; b++)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        bitrev_[size_t(i)] = r;
    }
    twiddle_.resize(size_t(n / 2));
    for (int k = 0; k < n / 2; k++) twiddle_[size_t(k)] = std::polar(1.0, -2 * kPi * double(k) / double(n));
}

void SpectrumAnalyzer::power(const float* samples, std::vector<float>& out) {
    const int n = size;
    if (int(out.size()) != n / 2 + 1) out.assign(size_t(n / 2 + 1), 0.0f);
    for (int i = 0; i < n; i++) buffer_[size_t(bitrev_[size_t(i)])] = std::complex<double>(double(samples[i] * window_[size_t(i)]), 0);
    for (int len = 2; len <= n; len <<= 1) {
        int half = len / 2, stride = n / len;
        for (int i = 0; i < n; i += len) {
            for (int j = 0; j < half; j++) {
                std::complex<double> t = twiddle_[size_t(j * stride)] * buffer_[size_t(i + j + half)];
                std::complex<double> u = buffer_[size_t(i + j)];
                buffer_[size_t(i + j)] = u + t;
                buffer_[size_t(i + j + half)] = u - t;
            }
        }
    }
    // vDSP's packed real FFT returns twice the DFT; these scalings reproduce its output.
    double norm = 1.0 / (double(windowSum_) * double(windowSum_));
    out[0] = float(std::norm(buffer_[0]) * norm);
    out[size_t(n / 2)] = float(std::norm(buffer_[size_t(n / 2)]) * norm);
    for (int k = 1; k < n / 2; k++) out[size_t(k)] = float(4 * std::norm(buffer_[size_t(k)]) * norm);
}

int fftSize(double fs, double divisor) {
    int n = 256;
    while (double(n) < fs / divisor) n *= 2;
    return n;
}

BinMap::BinMap(const std::vector<std::pair<double, double>>& frequencies, double binHz, int bins) {
    for (const auto& [f0, f1] : frequencies) {
        double b0 = f0 / binHz, b1 = f1 / binHz;
        int i0 = std::max(0, std::min(bins - 1, int(std::ceil(b0))));
        int i1 = std::max(0, std::min(bins - 1, int(std::floor(b1))));
        if (i1 >= i0) {
            lo.push_back(i0);
            hi.push_back(i1);
            frac.push_back(-1);
        } else {
            double c = std::max(0.0, std::min(double(bins - 1) - 1e-6, (b0 + b1) / 2));
            lo.push_back(int(c));
            hi.push_back(int(c) + 1);
            frac.push_back(float(c - double(int(c))));
        }
    }
}

float BinMap::value(size_t column, const float* p) const {
    float f = frac[column];
    if (f < 0) {
        float m = 0;
        for (int i = lo[column]; i <= hi[column]; i++) m = std::max(m, p[i]);
        return m;
    }
    return p[lo[column]] * (1 - f) + p[hi[column]] * f;
}

// MARK: - Spectrum

SpectrumView::SpectrumView(LabModel& m) : PlotView(m) { insets = {20, 46, 24, 14}; }

void SpectrumView::tick() {
    if (model.hold()) return;
    int n = fftSize(model.fs(), 12);
    if (!analyzer_ || analyzer_->size != n) {
        analyzer_ = std::make_unique<SpectrumAnalyzer>(n);
        samples_.assign(size_t(n), 0.0f);
        inSmooth_.clear();
        outSmooth_.clear();
    }
    model.audio.inputRing.copyLatest(n, samples_.data());
    analyzer_->power(samples_.data(), inPower_);
    model.audio.outputRing.copyLatest(n, samples_.data());
    analyzer_->power(samples_.data(), outPower_);
    smooth(inSmooth_, inPower_);
    smooth(outSmooth_, outPower_);
    setNeedsDisplay();
}

void SpectrumView::smooth(std::vector<float>& acc, const std::vector<float>& p) {
    if (acc.size() != p.size()) {
        acc = p;
        return;
    }
    for (size_t i = 0; i < p.size(); i++) {
        // Fast attack and gentle release, like a hardware analyser. When a signal
        // vanishes (more than 20 dB down) fall at 3 dB per frame so no ghost lingers.
        if (p[i] > acc[i]) acc[i] += (p[i] - acc[i]) * 0.6f;
        else if (p[i] < acc[i] * 0.01f) acc[i] = std::max(p[i], acc[i] * 0.5f);
        else acc[i] += (p[i] - acc[i]) * 0.25f;
    }
}

void SpectrumView::ensureMap(Rect r) {
    if (!analyzer_) return;
    std::string key = strf("%d-%d-%g-%d", int(r.w), model.logAxis() ? 1 : 0, model.fs(), analyzer_->size);
    if (key == mapKey_) return;
    mapKey_ = key;
    int count = std::max(10, int(r.w));
    columns_.clear();
    for (int i = 0; i < count; i++)
        columns_.push_back({frequencyForX(r.minX() + float(i), r), frequencyForX(r.minX() + float(i + 1), r)});
    map_ = BinMap(columns_, model.fs() / double(analyzer_->size), analyzer_->size / 2 + 1);
}

void SpectrumView::draw(Canvas& c) {
    c.fillRect(bounds(), theme().panel);
    Rect r = plotRect();
    c.fillRect(r, theme().plotBackground);
    drawFrequencyGrid(c, r);
    drawValueGrid(c, r, dbFloor_, 0, 20, [](double v) { return v == 0 ? std::string("0") : formatSigned(v, "%.0f"); });
    drawText(c, "dBFS", {r.minX() - 5, r.minY() - 12}, HAlign::right);
    ensureMap(r);
    if (inSmooth_.empty() || map_.lo.size() != columns_.size()) {
        drawFrame(c, r);
        return;
    }
    size_t cols = columns_.size();
    std::vector<float> xs(cols), yIn(cols), yOut(cols);
    std::vector<double> inDB(cols), outDB(cols);
    for (size_t i = 0; i < cols; i++) {
        xs[i] = r.minX() + float(i) + 0.5f;
        inDB[i] = db(map_.value(i, inSmooth_.data()));
        outDB[i] = db(map_.value(i, outSmooth_.data()));
        yIn[i] = yForValue(inDB[i], dbFloor_, 0, r);
        yOut[i] = yForValue(outDB[i], dbFloor_, 0, r);
    }

    c.save();
    c.clip(r);
    // Input: filled grey area.
    Path area;
    area.moveTo({xs[0], r.maxY()});
    for (size_t i = 0; i < cols; i++) area.lineTo({xs[i], yIn[i]});
    area.lineTo({xs.back(), r.maxY()});
    area.close();
    c.fillPath(area, theme().input.withAlpha(0.22f), false);
    strokeCurve(c, xs, yIn, theme().input.withAlpha(0.85f), 1);

    if (model.showPrediction()) {
        const DigitalFilter& filter = model.filter();
        std::vector<float> predicted(cols);
        for (size_t i = 0; i < cols; i++) {
            double f = (columns_[i].first + columns_[i].second) / 2;
            predicted[i] = yForValue(std::max(dbFloor_ - 10, inDB[i] + filter.magnitudeDB(f)), dbFloor_, 0, r);
        }
        strokeCurve(c, xs, predicted, theme().response.withAlpha(0.9f), 1.3f, {5, 4});
    }
    // Output: bright teal with a faint fill.
    Path outArea;
    outArea.moveTo({xs[0], r.maxY()});
    for (size_t i = 0; i < cols; i++) outArea.lineTo({xs[i], yOut[i]});
    outArea.lineTo({xs.back(), r.maxY()});
    outArea.close();
    c.fillPath(outArea, theme().output.withAlpha(0.10f), false);
    strokeCurve(c, xs, yOut, theme().output, 1.6f);
    c.restore();
    drawFrame(c, r);

    // Legend.
    float lx = r.minX() + 10;
    std::vector<std::pair<std::string, Color>> legend{{"input", theme().input}, {"output", theme().output}};
    if (model.showPrediction()) legend.push_back({"predicted = input + |H|", theme().response});
    for (const auto& [name, color] : legend) {
        c.fillRect({lx, r.minY() + 9, 10, 3}, color);
        Rect rect = drawText(c, name, {lx + 14, r.minY() + 10}, HAlign::left, VAlign::middle, Fonts::smallText(), theme().secondaryText);
        lx = rect.maxX() + 14;
    }

    if (hover_ && r.contains(*hover_) && !exporting) {
        size_t col = size_t(std::max(0, std::min(int(cols) - 1, int(hover_->x - r.minX()))));
        double f = (columns_[col].first + columns_[col].second) / 2;
        c.line({hover_->x, r.minY()}, {hover_->x, r.maxY()}, theme().cursor.withAlpha(0.35f), 1);
        drawReadout(c, {formatHz(f), "in  " + formatDB(inDB[col]), "out " + formatDB(outDB[col]), "gain " + formatDB(outDB[col] - inDB[col])},
                    {hover_->x, yOut[col]}, r);
    }
}

void SpectrumView::mouseMoved(const MouseEvent& e) {
    hover_ = e.location;
    Rect r = plotRect();
    if (r.contains(e.location)) model.setCursorFrequency(frequencyForX(e.location.x, r));
    else model.setCursorFrequency(std::nullopt);
    setNeedsDisplay();
}

void SpectrumView::mouseExited() {
    hover_.reset();
    model.setCursorFrequency(std::nullopt);
    setNeedsDisplay();
}

// MARK: - Spectrogram

namespace {

std::vector<uint32_t> makeColormap() {
    // Paper to ink: quiet levels fade into the page, loud ones darken through
    // amber and crimson to deep indigo.
    const std::pair<double, uint32_t> stops[] = {{0, 0xFFFDF8},    {0.22, 0xF4E6BE}, {0.42, 0xEBB66F}, {0.6, 0xD7743F},
                                                 {0.76, 0xAE3D3B}, {0.89, 0x632452}, {1, 0x231A3B}};
    const int count = 7;
    std::vector<uint32_t> lut(256);
    for (int i = 0; i < 256; i++) {
        double t = double(i) / 255;
        int k = 0;
        while (k < count - 2 && t > stops[k + 1].first) k++;
        double t0 = stops[k].first, t1 = stops[k + 1].first;
        uint32_t c0 = stops[k].second, c1 = stops[k + 1].second;
        double u = (t - t0) / (t1 - t0);
        auto ch = [](uint32_t c, int s) { return double((c >> s) & 0xFF); };
        uint32_t rr = uint32_t(ch(c0, 16) + (ch(c1, 16) - ch(c0, 16)) * u);
        uint32_t gg = uint32_t(ch(c0, 8) + (ch(c1, 8) - ch(c0, 8)) * u);
        uint32_t bb = uint32_t(ch(c0, 0) + (ch(c1, 0) - ch(c0, 0)) * u);
        // Memory order B, G, R, X (Direct2D's BGRA).
        lut[size_t(i)] = bb | (gg << 8) | (rr << 16) | 0xFF000000u;
    }
    return lut;
}

const std::vector<uint32_t>& colormap() {
    static const std::vector<uint32_t> lut = makeColormap();
    return lut;
}

} // namespace

SpectrogramView::SpectrogramView(LabModel& m) : PlotView(m) { insets = {20, 46, 24, 14}; }

SampleRing& SpectrogramView::ring() {
    return model.spectrogramShowsInput() ? model.audio.inputRing : model.audio.outputRing;
}

void SpectrogramView::setup(Rect r) {
    int n = fftSize(model.fs(), 24);
    std::string k = strf("%dx%d-%d-%g-%d", int(r.w), int(r.h), model.logAxis() ? 1 : 0, model.fs(),
                         model.spectrogramShowsInput() ? 1 : 0);
    if (k == key_ && analyzer_ && analyzer_->size == n) return;
    key_ = k;
    analyzer_ = std::make_unique<SpectrumAnalyzer>(n);
    fftFrame_.assign(size_t(n), 0.0f);
    hop_ = std::max(32, int(model.fs() / 100));
    imageWidth_ = std::max(10, int(r.w));
    imageHeight_ = std::max(10, int(r.h));
    pixels_.assign(size_t(imageWidth_ * imageHeight_), colormap()[0]);
    pixelVersion_++;
    writeColumn_ = 0;
    cursor_ = -1;
    // Row 0 is the top of the image = highest frequency.
    std::vector<std::pair<double, double>> rows;
    for (int row = 0; row < imageHeight_; row++) {
        float yTop = r.minY() + float(row), yBottom = r.minY() + float(row + 1);
        rows.push_back({frequencyAtY(yBottom, r), frequencyAtY(yTop, r)});
    }
    rowMap_ = BinMap(rows, model.fs() / double(n), n / 2 + 1);
}

double SpectrogramView::frequencyAtY(float y, Rect r) const {
    double t = double((r.maxY() - y) / r.h);
    return model.logAxis() ? fMin() * std::pow(fMax() / fMin(), t) : fMin() + t * (fMax() - fMin());
}

float SpectrogramView::yForFrequency(double f, Rect r) const {
    double t = model.logAxis() ? std::log(std::max(f, fMin()) / fMin()) / std::log(fMax() / fMin()) : (f - fMin()) / (fMax() - fMin());
    return r.maxY() - float(t) * r.h;
}

void SpectrogramView::tick() {
    if (model.hold()) return;
    Rect r = plotRect();
    setup(r);
    if (!analyzer_) return;
    SampleRing& rg = ring();
    int n = analyzer_->size;
    int64_t total = rg.totalWritten();
    if (cursor_ < 0 || total - cursor_ > rg.capacity() / 2 || cursor_ > total) cursor_ = total;
    const auto& lut = colormap();
    int columns = 0;
    while (cursor_ + hop_ <= total && columns < 40) {
        cursor_ += hop_;
        rg.copy(cursor_ - n, n, fftFrame_.data());
        analyzer_->power(fftFrame_.data(), power_);
        for (int row = 0; row < imageHeight_; row++) {
            float v = rowMap_.value(size_t(row), power_.data());
            double dbv = 10 * std::log10(double(std::max(v, 1e-20f)));
            double t = std::max(0.0, std::min(1.0, (dbv + 110) / 100));
            pixels_[size_t(row * imageWidth_ + writeColumn_)] = lut[size_t(t * 255)];
        }
        writeColumn_ = (writeColumn_ + 1) % imageWidth_;
        columns++;
    }
    if (columns > 0) {
        pixelVersion_++;
        setNeedsDisplay();
    }
}

void SpectrogramView::draw(Canvas& c) {
    c.fillRect(bounds(), theme().panel);
    Rect r = plotRect();
    setup(r);
    if (!pixels_.empty()) {
        if (!bitmap_ || bitmapDevice_ != c.deviceId() || bitmapW_ != imageWidth_ || bitmapH_ != imageHeight_) {
            bitmap_ = makeBitmap(c.rt(), pixels_.data(), imageWidth_, imageHeight_);
            bitmapDevice_ = c.deviceId();
            bitmapW_ = imageWidth_;
            bitmapH_ = imageHeight_;
            bitmapVersion_ = pixelVersion_;
        } else if (bitmapVersion_ != pixelVersion_) {
            bitmap_->CopyFromMemory(nullptr, pixels_.data(), UINT32(imageWidth_ * 4));
            bitmapVersion_ = pixelVersion_;
        }
        c.save();
        c.clip(r);
        // Oldest columns start at writeColumn; draw them first so "now" is at the right edge.
        float w = float(imageWidth_), h = float(imageHeight_);
        float split = float(writeColumn_);
        float sx = r.w / w;
        Rect olderSrc{split, 0, w - split, h};
        c.drawBitmap(bitmap_.get(), {r.minX(), r.minY(), (w - split) * sx, r.h}, &olderSrc, false);
        if (split > 0) {
            Rect newerSrc{0, 0, split, h};
            c.drawBitmap(bitmap_.get(), {r.minX() + (w - split) * sx, r.minY(), split * sx, r.h}, &newerSrc, false);
        }
        c.restore();
    }
    // Frequency labels on the left.
    std::vector<double> ticks;
    if (model.logAxis()) {
        ticks = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    } else {
        double step = niceStep(fMax() / 6);
        for (double f = 0; f <= fMax() + 1e-9; f += step) ticks.push_back(f);
    }
    for (double f : ticks) {
        if (!(f >= fMin() && f <= fMax())) continue;
        float y = yForFrequency(f, r);
        c.line({r.minX(), y}, {r.maxX(), y}, theme().gridMajor.withAlpha(0.55f), 1);
        drawText(c, formatHzTick(f), {r.minX() - 5, y}, HAlign::right);
    }
    drawText(c, "Hz", {r.minX() - 5, r.minY() - 12}, HAlign::right);
    // Time labels underneath.
    double seconds = double(imageWidth_) * double(hop_) / model.fs();
    double step = niceStep(seconds / 6);
    double t = 0.0;
    while (t <= seconds + 1e-9) {
        float x = r.maxX() - float(t / seconds) * r.w;
        drawText(c, t == 0 ? std::string("now") : std::string(kMinus) + strf("%gs", t), {x, r.maxY() + 4},
                 t == 0 ? HAlign::right : HAlign::center, VAlign::top);
        t += step;
    }
    drawText(c, model.spectrogramShowsInput() ? "input" : "output (what you hear)", {r.minX() + 8, r.minY() + 6}, HAlign::left,
             VAlign::top, Fonts::smallBold(), theme().secondaryText);
    drawFrame(c, r);
    if (hover_ && r.contains(*hover_) && !exporting) {
        double f = frequencyAtY(hover_->y, r);
        double ago = double((r.maxX() - hover_->x) / r.w) * seconds;
        drawReadout(c, {formatHz(f), strf("%.2f s ago", ago)}, *hover_, r);
    }
}

void SpectrogramView::mouseMoved(const MouseEvent& e) {
    hover_ = e.location;
    setNeedsDisplay();
}

void SpectrogramView::mouseExited() {
    hover_.reset();
    setNeedsDisplay();
}

// MARK: - Oscilloscope

static const double kLevels[] = {0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0};

ScopeView::ScopeView(LabModel& m) : PlotView(m) { insets = {20, 46, 24, 14}; }

void ScopeView::tick() {
    // Like a scope in "normal" trigger mode: show the most recent triggered sweep, and
    // keep showing it for a while if no new trigger arrives (so sparse clicks stay visible).
    if (model.hold()) return;
    int window = std::max(16, int(model.scopeWindowMs() / 1000 * model.fs()));
    int history = window + int(0.6 * model.fs());
    if (int(input_.size()) != history) {
        input_.assign(size_t(history), 0.0f);
        output_.assign(size_t(history), 0.0f);
        lastTrigger_.reset();
    }
    // Both rings are written in the same audio block, so the same indices line up.
    int64_t end = model.audio.inputRing.totalWritten();
    model.audio.inputRing.copy(end - history, history, input_.data());
    model.audio.outputRing.copy(end - history, history, output_.data());
    int pre = window / 10;
    float peak = 0;
    for (float v : input_) peak = std::max(peak, std::fabs(v));
    float hysteresis = 0.02f * peak;
    int trigger = -1;
    int i = history - window + pre;
    while (i > pre + 1 && peak > 1e-4f) {
        if (input_[size_t(i - 1)] <= 0 && input_[size_t(i)] > hysteresis * 0.2f) {
            bool dipped = false;
            for (int j = std::max(0, i - window / 4); j < i; j++) {
                if (input_[size_t(j)] < -hysteresis) {
                    dipped = true;
                    break;
                }
            }
            if (dipped || input_[size_t(i)] > 0.3f * peak) {
                trigger = i;
                break;
            }
        }
        i--;
    }
    double now = mediaTime();
    if (trigger >= 0) {
        int start = trigger - pre;
        lastTrigger_ = Trigger{std::vector<float>(input_.begin() + start, input_.begin() + start + window),
                               std::vector<float>(output_.begin() + start, output_.begin() + start + window), now};
    } else if (lastTrigger_ && (now - lastTrigger_->time > 1.5 || int(lastTrigger_->input.size()) != window)) {
        lastTrigger_.reset();
    }
    if (lastTrigger_) {
        std::copy(lastTrigger_->input.begin(), lastTrigger_->input.end(), input_.begin() + (history - window));
        std::copy(lastTrigger_->output.begin(), lastTrigger_->output.end(), output_.begin() + (history - window));
    }
    shownStart_ = history - window;
    shownCount_ = window;

    float inPeak = 0, outPeak = 0;
    for (int k = shownStart_; k < shownStart_ + window; k++) {
        inPeak = std::max(inPeak, std::fabs(input_[size_t(k)]));
        outPeak = std::max(outPeak, std::fabs(output_[size_t(k)]));
    }
    // One shared scale so shapes compare fairly, unless the output is so much quieter
    // than the input (a narrow resonator fed clicks) that it needs its own gain.
    scale_ = pick(double(std::max(inPeak, outPeak)), scale_);
    outScale_ = outPeak < inPeak / 4 ? pick(double(outPeak), outScale_) : scale_;
    setNeedsDisplay();
}

double ScopeView::pick(double p, double current) const {
    double want = 1;
    for (double l : kLevels)
        if (l >= p * 1.1) {
            want = l;
            break;
        }
    return want > current || want <= current / 2 ? want : current;
}

void ScopeView::draw(Canvas& c) {
    c.fillRect(bounds(), theme().panel);
    Rect r = plotRect();
    c.fillRect(r, theme().plotBackground);
    // 10 horizontal divisions like a real scope.
    double ms = double(shownCount_) / model.fs() * 1000;
    for (int k = 0; k <= 10; k++) {
        float x = std::floor(r.minX() + float(k) / 10 * r.w) + 0.5f;
        Color col = (k == 0 || k == 10) ? theme().gridMajor : theme().gridMinor.blended(0.4f, theme().gridMajor);
        c.line({x, r.minY()}, {x, r.maxY()}, col, 1);
        if (k % 2 == 0) {
            double t = ms * double(k) / 10;
            drawText(c, t == 0 ? std::string("0") : (ms >= 50 ? strf("%.0f", t) : strf("%.1f", t)), {x, r.maxY() + 4},
                     HAlign::center, VAlign::top);
        }
    }
    drawText(c, "ms", {r.minX() - 6, r.maxY() + 4}, HAlign::right, VAlign::top);
    double sc = scale_;
    drawValueGrid(c, r, -sc, sc, sc / 2, [sc](double v) { return formatSigned(v, sc < 0.04 ? "%.4f" : (sc < 0.2 ? "%.3f" : "%.2f")); });

    if (shownCount_ > 1 && int(input_.size()) >= shownStart_ + shownCount_ && shownStart_ >= 0) {
        int n = shownCount_;
        std::vector<float> xs((size_t)n), yIn((size_t)n), yOut((size_t)n);
        double g = outputGain();
        for (int k = 0; k < n; k++) {
            xs[size_t(k)] = r.minX() + float(k) / float(n - 1) * r.w;
            yIn[size_t(k)] = yForValue(double(input_[size_t(shownStart_ + k)]), -sc, sc, r);
            yOut[size_t(k)] = yForValue(double(output_[size_t(shownStart_ + k)]) * g, -sc, sc, r);
        }
        c.save();
        c.clip(r);
        strokeCurve(c, xs, yIn, theme().input, 1.3f);
        strokeCurve(c, xs, yOut, theme().output, 2);
        c.restore();
    }
    float lx = r.minX() + 10;
    std::string outName = outputGain() > 1 ? strf("output (magnified \xC3\x97%g)", outputGain()) : "output";
    std::pair<std::string, Color> legend[] = {{"input", theme().input}, {outName, theme().output}};
    for (const auto& [name, color] : legend) {
        c.fillRect({lx, r.minY() + 9, 10, 3}, color);
        Rect rect = drawText(c, name, {lx + 14, r.minY() + 10}, HAlign::left, VAlign::middle, Fonts::smallText(), theme().secondaryText);
        lx = rect.maxX() + 14;
    }
    drawFrame(c, r);
    if (hover_ && r.contains(*hover_) && !exporting && shownCount_ > 1) {
        int i = std::max(0, std::min(shownCount_ - 1, int((hover_->x - r.minX()) / r.w * float(shownCount_ - 1))));
        double t = double(i) / model.fs() * 1000;
        if (shownStart_ + i < int(input_.size())) {
            drawReadout(c, {strf("t = %.2f ms", t), "in  " + formatSigned(double(input_[size_t(shownStart_ + i)])),
                            "out " + formatSigned(double(output_[size_t(shownStart_ + i)]))},
                        *hover_, r);
        }
    }
}

void ScopeView::mouseMoved(const MouseEvent& e) {
    hover_ = e.location;
    setNeedsDisplay();
}

void ScopeView::mouseExited() {
    hover_.reset();
    setNeedsDisplay();
}
