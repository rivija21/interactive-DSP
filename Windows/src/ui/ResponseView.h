#pragma once
#include "PlotView.h"
#include <optional>

/// Frequency response (magnitude / phase / group delay) and time response (impulse / step)
/// of the designed filter. Cutoff markers can be dragged to redesign the filter.
class ResponseView : public PlotView {
public:
    explicit ResponseView(LabModel& model);
    void invalidate() {
        dirty_ = true;
        setNeedsDisplay();
    }
    void layout() override { dirty_ = true; }
    void prepareForExport() override { dirty_ = true; }
    void draw(Canvas& c) override;
    void mouseMoved(const MouseEvent& e) override;
    void mouseExited() override;
    void mouseDown(const MouseEvent& e) override;
    void mouseDragged(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;
    Cursor cursorAt(Point p) override;

private:
    ResponseMode mode() const { return model.responseMode(); }
    bool isTimeMode() const { return mode() == ResponseMode::impulse || mode() == ResponseMode::step; }
    void recompute();
    void updateMagnitudeRange();
    std::string valueLabel(double v) const;
    void drawFrequencyResponse(Canvas& c, Rect r);
    void drawMagnitudeGuides(Canvas& c, Rect r);
    void drawSourceMarker(Canvas& c, Rect r);
    std::vector<double> markerFrequencies() const;
    void drawCutoffMarkers(Canvas& c, Rect r);
    void drawHover(Canvas& c, Rect r);
    size_t nearestIndex(double f) const;
    void drawTimeResponse(Canvas& c, Rect r);
    std::optional<int> markerHit(Point p) const;

    bool dirty_ = true;
    std::vector<float> xs_;
    std::vector<double> freqs_;
    std::vector<double> values_;
    struct Range {
        double lo, hi, step;
    } range_{-100, 5, 20};
    double magnitudeFloor_ = -100;
    std::optional<Point> hover_;
    std::optional<int> dragMarker_;
};
