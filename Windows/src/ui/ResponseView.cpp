#include "ResponseView.h"
#include "../util/Format.h"
#include "../util/Text.h"
#include <algorithm>
#include <cmath>

ResponseView::ResponseView(LabModel& m) : PlotView(m) { insets = {22, 48, 26, 44}; }

// MARK: Data

void ResponseView::recompute() {
    dirty_ = false;
    Rect r = plotRect();
    const DigitalFilter& filter = model.filter();
    switch (mode()) {
    case ResponseMode::magnitude:
    case ResponseMode::phase:
    case ResponseMode::groupDelay: {
        int n = std::max(200, int(r.w * 1.5f));
        freqs_ = frequencyGrid(n);
        if (mode() == ResponseMode::magnitude) {
            // Narrow notches fall between grid points, so add each unit-circle zero's
            // exact frequency (and its shoulders) to draw the full depth.
            std::vector<double> extra;
            for (const auto& z : filter.zpk.zeros) {
                if (!(std::fabs(z.magnitude() - 1) < 1e-3 && z.im >= 0)) continue;
                double f = std::fabs(z.phase()) / (2 * kPi) * model.fs();
                for (double d : {-0.02, -0.005, 0.0, 0.005, 0.02}) extra.push_back(f * (1 + d));
            }
            if (!extra.empty()) {
                for (double f : extra)
                    if (f > fMin() && f < fMax()) freqs_.push_back(f);
                std::sort(freqs_.begin(), freqs_.end());
            }
        }
        xs_.resize(freqs_.size());
        for (size_t i = 0; i < freqs_.size(); i++) xs_[i] = xForFrequency(freqs_[i], r);
        values_.resize(freqs_.size());
        if (mode() == ResponseMode::magnitude) {
            for (size_t i = 0; i < freqs_.size(); i++) values_[i] = std::max(-300.0, filter.magnitudeDB(freqs_[i]));
            updateMagnitudeRange();
        } else if (mode() == ResponseMode::phase) {
            double last = 0.0, offset = 0.0;
            for (size_t i = 0; i < freqs_.size(); i++) {
                double ph = filter.response(freqs_[i]).phase();
                if (i > 0) {
                    double d = ph + offset - last;
                    if (d > kPi) offset -= 2 * kPi;
                    else if (d < -kPi) offset += 2 * kPi;
                }
                ph += offset;
                last = ph;
                values_[i] = ph * 180 / kPi;
            }
            double lo = values_.empty() ? -180 : *std::min_element(values_.begin(), values_.end());
            double hi = values_.empty() ? 180 : *std::max_element(values_.begin(), values_.end());
            double span = std::max(180.0, hi - lo);
            const double steps[] = {15, 30, 45, 90, 180, 360, 720, 1440, 2880, 5760, 11520, 23040, 46080};
            double step = 92160;
            for (double s : steps)
                if (s >= span / 7) {
                    step = s;
                    break;
                }
            double lo2 = std::floor(lo / step) * step;
            range_ = {lo2, std::max(std::ceil(hi / step) * step, lo2 + step), step};
        } else {
            for (size_t i = 0; i < freqs_.size(); i++) values_[i] = filter.groupDelay(freqs_[i]);
            std::vector<double> sorted;
            for (double v : values_)
                if (std::isfinite(v)) sorted.push_back(v);
            std::sort(sorted.begin(), sorted.end());
            auto p = [&](double q) {
                return sorted.empty() ? 0.0 : sorted[std::min(sorted.size() - 1, size_t(q * double(sorted.size() - 1)))];
            };
            double lo = std::min(0.0, p(0.01)), hi = std::max(p(0.985) * 1.2, lo + 2);
            double step = niceStep((hi - lo) / 5);
            lo = std::floor(lo / step) * step;
            hi = std::ceil(hi / step) * step;
            range_ = {lo, hi, step};
        }
        break;
    }
    case ResponseMode::impulse:
    case ResponseMode::step: {
        int length = std::min(filter.suggestedResponseLength(), 1500);
        values_ = mode() == ResponseMode::impulse ? filter.impulseResponse(length) : filter.stepResponse(length);
        double lo = 0, hi = 0;
        bool first = true;
        for (double v : values_) {
            double f = std::isfinite(v) ? v : 0;
            if (first) {
                lo = hi = f;
                first = false;
            }
            lo = std::min(lo, f);
            hi = std::max(hi, f);
        }
        if (first) hi = 1;
        lo = std::min(0.0, lo);
        hi = std::max(0.0, hi);
        if (hi - lo < 1e-9) hi = lo + 1;
        double pad = (hi - lo) * 0.08;
        double step = niceStep((hi - lo + 2 * pad) / 5);
        lo = std::floor((lo - pad) / step) * step;
        hi = std::ceil((hi + pad) / step) * step;
        range_ = {lo, hi, step};
        size_t n = values_.size();
        xs_.resize(n);
        for (size_t i = 0; i < n; i++) xs_[i] = r.minX() + float(double(i) + 0.5) / float(n) * r.w;
        break;
    }
    }
}

void ResponseView::updateMagnitudeRange() {
    std::vector<double> sorted = values_;
    std::sort(sorted.begin(), sorted.end());
    double p10 = sorted.empty() ? -60 : sorted[sorted.size() / 10];
    double peak = sorted.empty() ? 0 : sorted.back();
    double top = std::max(5.0, std::ceil((peak + 1) / 5) * 5);
    double desired = std::max(-160.0, std::min(-40.0, std::floor((p10 - 10) / 20) * 20));
    const auto& spec = model.spec();
    if (spec.method == DesignMethod::iir && familyUsesStopbandAttenuation(spec.family)) {
        desired = std::min(desired, -std::ceil((spec.stopDB + 20) / 20) * 20);
    }
    // Only move the floor for big changes, so the axis doesn't jump while dragging.
    if (std::fabs(desired - magnitudeFloor_) > 20 || magnitudeFloor_ > p10) magnitudeFloor_ = desired;
    double span = top - magnitudeFloor_;
    range_ = {magnitudeFloor_, top, span <= 60 ? 10.0 : 20.0};
}

// MARK: Drawing

void ResponseView::draw(Canvas& c) {
    if (dirty_) recompute();
    c.fillRect(bounds(), theme().panel);
    Rect r = plotRect();
    c.fillRect(r, theme().plotBackground);
    if (isTimeMode()) drawTimeResponse(c, r);
    else drawFrequencyResponse(c, r);
    drawFrame(c, r);
}

std::string ResponseView::valueLabel(double v) const {
    switch (mode()) {
    case ResponseMode::magnitude: return v == 0 ? "0" : formatSigned(v, "%.0f");
    case ResponseMode::phase: return formatSigned(v, "%.0f") + "\xC2\xB0";
    case ResponseMode::groupDelay: return v == std::round(v) ? formatSigned(v, "%.0f") : formatSigned(v, "%.1f");
    default: {
        int decimals = std::max(0, int(std::ceil(-std::log10(std::fabs(range_.step)) - 1e-9)));
        std::string f = strf("%%.%df", decimals);
        return std::fabs(v) < std::fabs(range_.step) * 1e-6 ? "0" : formatSigned(v, f.c_str());
    }
    }
}

void ResponseView::drawFrequencyResponse(Canvas& c, Rect r) {
    drawFrequencyGrid(c, r);
    std::function<std::string(double)> rightLabels;
    if (mode() == ResponseMode::groupDelay) rightLabels = [this](double v) { return formatMs(v / model.fs() * 1000); };
    drawValueGrid(c, r, range_.lo, range_.hi, range_.step, [this](double v) { return valueLabel(v); }, rightLabels);
    const char* unit = mode() == ResponseMode::magnitude ? "dB" : (mode() == ResponseMode::phase ? "deg" : "samples");
    drawText(c, unit, {r.minX() - 5, r.minY() - 13}, HAlign::right, VAlign::middle);
    if (mode() == ResponseMode::groupDelay) drawText(c, "time", {r.maxX() + 5, r.minY() - 13}, HAlign::left);

    c.save();
    c.clip(r);
    Color color = mode() == ResponseMode::phase ? theme().phase : theme().response;
    std::vector<float> ys(values_.size());
    double span = range_.hi - range_.lo;
    for (size_t i = 0; i < values_.size(); i++) {
        double v = values_[i];
        if (!std::isfinite(v)) {
            ys[i] = NAN;
            continue;
        }
        double cl = std::max(range_.lo - span, std::min(range_.hi + span, v));
        ys[i] = yForValue(cl, range_.lo, range_.hi, r);
    }
    if (mode() == ResponseMode::magnitude) {
        drawMagnitudeGuides(c, r);
        // Soft fill under the curve.
        Path area;
        area.moveTo({xs_.empty() ? r.minX() : xs_.front(), r.maxY()});
        for (size_t i = 0; i < xs_.size() && i < ys.size(); i++)
            if (std::isfinite(ys[i])) area.lineTo({xs_[i], std::min(ys[i], r.maxY())});
        area.lineTo({xs_.empty() ? r.maxX() : xs_.back(), r.maxY()});
        area.close();
        c.fillPath(area, color.withAlpha(exporting ? 0.10f : 0.13f), exporting);
    }
    strokeCurve(c, xs_, ys, color, 2);
    if (!model.filter().isStable() && mode() == ResponseMode::magnitude) {
        drawText(c, "unstable: |H| on the circle no longer describes the output", {r.midX(), r.minY() + 8}, HAlign::center,
                 VAlign::top, Fonts::smallText(), theme().danger);
    }
    drawSourceMarker(c, r);
    c.restore();
    drawCutoffMarkers(c, r);
    drawHover(c, r);
}

void ResponseView::drawMagnitudeGuides(Canvas& c, Rect r) {
    const auto& spec = model.spec();
    std::vector<std::pair<double, std::string>> guides{{-3.0103, std::string(kMinus) + "3 dB"}};
    if (spec.method == DesignMethod::iir) {
        if (familyUsesPassbandRipple(spec.family)) guides.push_back({-spec.rippleDB, "ripple " + formatDB(-spec.rippleDB)});
        if (familyUsesStopbandAttenuation(spec.family))
            guides.push_back({-spec.stopDB, "stopband " + formatDB(-spec.stopDB, 0)});
    }
    std::stable_sort(guides.begin(), guides.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    float lastY = -1e9f, lastRight = r.minX();
    for (const auto& [db, label] : guides) {
        if (!(db > range_.lo && db < range_.hi)) continue;
        float y = yForValue(db, range_.lo, range_.hi, r);
        c.line({r.minX(), y}, {r.maxX(), y}, theme().secondaryText.withAlpha(0.45f), 1, {4, 4});
        // Labels sit under their line at the left; close lines share a row.
        float x = std::fabs(y - lastY) < 12 ? lastRight + 10 : r.minX() + 6;
        Rect rect = drawText(c, label, {x, y + 2}, HAlign::left, VAlign::top, Fonts::axis(), theme().secondaryText);
        lastY = y;
        lastRight = rect.maxX();
    }
}

void ResponseView::drawSourceMarker(Canvas& c, Rect r) {
    if (exporting || !sourceIsTone(model.source())) return;
    double f = model.audio.params.currentFrequency.value();
    if (!(f > fMin() && f < fMax())) return;
    float x = xForFrequency(f, r);
    c.line({x, r.minY()}, {x, r.maxY()}, theme().output.withAlpha(0.8f), 1.5f);
    if (mode() == ResponseMode::magnitude) {
        double db = model.filter().magnitudeDB(f);
        float y = yForValue(std::max(range_.lo, std::min(range_.hi, db)), range_.lo, range_.hi, r);
        c.fillEllipse({x - 4, y - 4, 8, 8}, theme().output);
        drawText(c, "\xE2\x99\xAA " + formatHz(f) + "  " + formatDB(db), {x + 6, r.maxY() - 6}, HAlign::left, VAlign::bottom,
                 Fonts::smallBold(), theme().output);
    }
}

std::vector<double> ResponseView::markerFrequencies() const {
    const auto& spec = model.spec();
    if (spec.method == DesignMethod::poleZero) return {};
    if (bandIsBand(spec.band)) return {spec.f1, spec.f2};
    return {spec.f1};
}

void ResponseView::drawCutoffMarkers(Canvas& c, Rect r) {
    auto markers = markerFrequencies();
    for (size_t i = 0; i < markers.size(); i++) {
        double f = markers[i];
        float x = xForFrequency(f, r);
        if (!(x >= r.minX() - 1 && x <= r.maxX() + 1)) continue;
        bool active = dragMarker_ && *dragMarker_ == int(i);
        c.line({x, r.minY()}, {x, r.maxY()}, theme().marker.withAlpha(active ? 0.95f : 0.6f), active ? 1.5f : 1.0f, {2, 3});
        if (!exporting) {
            // Grab handle.
            c.fillRoundedRect({x - 5, r.minY() - 1, 10, 12}, 3, theme().marker.withAlpha(active ? 1.0f : 0.85f));
        }
        std::string name = markers.size() == 2 ? (i == 0 ? "f\xE2\x82\x81" : "f\xE2\x82\x82") : "fc";
        std::string label = name + " " + formatHz(f);
        bool leftSide = x > r.maxX() - 90;
        drawText(c, label, {leftSide ? x - 8 : x + 8, r.minY() + 2}, leftSide ? HAlign::right : HAlign::left, VAlign::top,
                 Fonts::smallBold(), theme().marker);
    }
}

void ResponseView::drawHover(Canvas& c, Rect r) {
    if (!hover_ || !r.contains(*hover_) || exporting || dragMarker_) return;
    Point h = *hover_;
    double f = frequencyForX(h.x, r);
    c.line({h.x, r.minY()}, {h.x, r.maxY()}, theme().cursor.withAlpha(0.35f), 1);
    const DigitalFilter& filter = model.filter();
    std::vector<std::string> lines{formatHz(f)};
    double value = 0;
    switch (mode()) {
    case ResponseMode::magnitude: {
        value = filter.magnitudeDB(f);
        lines.push_back(formatDB(value, 2));
        lines.push_back(strf("|H| = %.4g", filter.response(f).magnitude()));
        break;
    }
    case ResponseMode::phase: {
        if (values_.empty()) return;
        value = values_[nearestIndex(f)];
        lines.push_back(formatSigned(value, "%.1f") + "\xC2\xB0");
        break;
    }
    default:
        value = filter.groupDelay(f);
        lines.push_back(strf("%.2f samples", value));
        lines.push_back(formatMs(value / model.fs() * 1000));
        break;
    }
    if (std::isfinite(value)) {
        float y = yForValue(std::max(range_.lo, std::min(range_.hi, value)), range_.lo, range_.hi, r);
        c.fillEllipse({h.x - 3.5f, y - 3.5f, 7, 7}, mode() == ResponseMode::phase ? theme().phase : theme().response);
        drawReadout(c, lines, {h.x, y}, r);
    }
}

size_t ResponseView::nearestIndex(double f) const {
    if (freqs_.empty()) return 0;
    size_t lo = 0, hi = freqs_.size() - 1;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (freqs_[mid] < f) lo = mid;
        else hi = mid;
    }
    return std::fabs(freqs_[lo] - f) < std::fabs(freqs_[hi] - f) ? lo : hi;
}

void ResponseView::drawTimeResponse(Canvas& c, Rect r) {
    size_t n = values_.size();
    if (n == 0) return;
    // x grid in samples.
    double step = niceStep(double(n) / 8);
    double k = 0.0;
    while (k <= double(n)) {
        float x = std::floor(r.minX() + float(k / double(n)) * r.w) + 0.5f;
        c.line({x, r.minY()}, {x, r.maxY()}, theme().gridMajor, 1);
        drawText(c, strf("%.0f", k), {x, r.maxY() + 4}, HAlign::center, VAlign::top);
        k += step;
    }
    drawText(c, "n", {r.minX() - 6, r.maxY() + 4}, HAlign::right, VAlign::top);
    drawText(c, strf("%d samples = ", int(n)) + formatMs(double(n) / model.fs() * 1000), {r.maxX() - 6, r.minY() + 6},
             HAlign::right, VAlign::top, Fonts::smallText(), theme().secondaryText);
    drawValueGrid(c, r, range_.lo, range_.hi, range_.step, [this](double v) { return valueLabel(v); });

    c.save();
    c.clip(r);
    float zeroY = yForValue(0, range_.lo, range_.hi, r);
    Color color = model.filter().isStable() ? theme().response : theme().danger;
    std::vector<float> ys(n);
    for (size_t i = 0; i < n; i++) ys[i] = yForValue(std::isfinite(values_[i]) ? values_[i] : 0, range_.lo, range_.hi, r);
    if (n <= 160) {
        // Stem plot, as in the textbooks.
        Path stems;
        for (size_t i = 0; i < n; i++) {
            stems.moveTo({xs_[i], zeroY});
            stems.lineTo({xs_[i], ys[i]});
        }
        c.strokePath(stems, color.withAlpha(0.7f), 1.2f);
        float rad = n > 80 ? 2.0f : 3.0f;
        for (size_t i = 0; i < n; i++) c.fillEllipse({xs_[i] - rad, ys[i] - rad, rad * 2, rad * 2}, color);
    } else {
        strokeCurve(c, xs_, ys, color, 1.6f);
    }
    c.restore();

    if (hover_ && r.contains(*hover_) && !exporting) {
        size_t i = size_t(std::max(0, std::min(int(n) - 1, int((hover_->x - r.minX()) / r.w * float(n)))));
        c.line({xs_[i], r.minY()}, {xs_[i], r.maxY()}, theme().cursor.withAlpha(0.35f), 1);
        std::string name = mode() == ResponseMode::impulse ? "h" : "s";
        drawReadout(c, {name + "[" + std::to_string(i) + "] = " + formatSigned(values_[i], "%.5f"), formatMs(double(i) / model.fs() * 1000)},
                    {xs_[i], ys[i]}, r);
    }
}

// MARK: Mouse

std::optional<int> ResponseView::markerHit(Point p) const {
    if (isTimeMode()) return std::nullopt;
    Rect r = plotRect();
    if (!(p.y >= r.minY() - 4 && p.y <= r.maxY())) return std::nullopt;
    auto markers = markerFrequencies();
    for (size_t i = 0; i < markers.size(); i++)
        if (std::fabs(xForFrequency(markers[i], r) - p.x) < 6) return int(i);
    return std::nullopt;
}

Cursor ResponseView::cursorAt(Point p) {
    if (dragMarker_ || markerHit(p)) return Cursor::resizeLeftRight;
    return Cursor::arrow;
}

void ResponseView::mouseMoved(const MouseEvent& e) {
    Point p = e.location;
    hover_ = p;
    Rect r = plotRect();
    if (!isTimeMode() && r.contains(p)) model.setCursorFrequency(frequencyForX(p.x, r));
    else model.setCursorFrequency(std::nullopt);
    setNeedsDisplay();
}

void ResponseView::mouseExited() {
    hover_.reset();
    model.setCursorFrequency(std::nullopt);
    setNeedsDisplay();
}

void ResponseView::mouseDown(const MouseEvent& e) {
    Point p = e.location;
    dragMarker_ = markerHit(p);
    const auto& spec = model.spec();
    if (!dragMarker_ && !isTimeMode() && plotRect().contains(p) && spec.method != DesignMethod::poleZero && !bandIsBand(spec.band)) {
        // Click anywhere to move a single cutoff there.
        dragMarker_ = 0;
        mouseDragged(e);
    }
    setNeedsDisplay();
}

void ResponseView::mouseDragged(const MouseEvent& e) {
    if (!dragMarker_) return;
    int marker = *dragMarker_;
    Point p = e.location;
    Rect r = plotRect();
    double f = frequencyForX(std::min(std::max(p.x, r.minX()), r.maxX()), r);
    model.updateSpec([&](DesignSpec& s) {
        if (marker == 0) s.f1 = bandIsBand(s.band) ? std::min(f, s.f2 / 1.02) : f;
        else s.f2 = std::max(f, s.f1 * 1.02);
    });
    hover_ = p;
    model.setCursorFrequency(f);
}

void ResponseView::mouseUp(const MouseEvent&) {
    dragMarker_.reset();
    setNeedsDisplay();
}
