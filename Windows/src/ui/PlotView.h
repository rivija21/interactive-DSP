#pragma once
#include "../app/LabModel.h"
#include "Theme.h"
#include "Widget.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

/// Base class for the plots: theme, axes and text helpers.
class PlotView : public Widget {
public:
    explicit PlotView(LabModel& model) : model(model) {}
    LabModel& model;
    /// When true the view draws with the light print theme (used for figure export).
    bool exporting = false;
    const Theme& theme() const { return exporting ? Theme::print() : Theme::app(); }
    struct Insets {
        float top, left, bottom, right;
    };
    Insets insets{12, 46, 24, 14};

    Rect plotRect() const;
    void fillBackground(Canvas& c);

    Rect drawText(Canvas& c, const std::string& s, Point p, HAlign h = HAlign::left, VAlign v = VAlign::middle,
                  const Font& font = Fonts::axis(), std::optional<Color> color = std::nullopt);
    /// A rounded readout box (for hover values).
    void drawReadout(Canvas& c, const std::vector<std::string>& lines, Point p, Rect in);

    double fMin() const { return model.logAxis() ? 10 : 0; }
    double fMax() const { return model.fs() / 2; }
    float xForFrequency(double f, Rect r) const;
    double frequencyForX(float x, Rect r) const;
    /// Frequencies at evenly spaced x positions across the plot (log or linear).
    std::vector<double> frequencyGrid(int count) const;
    void drawFrequencyGrid(Canvas& c, Rect r, bool labels = true);
    void drawValueGrid(Canvas& c, Rect r, double lo, double hi, double step, const std::function<std::string(double)>& format,
                       const std::function<std::string(double)>& rightLabels = nullptr);
    float yForValue(double v, double lo, double hi, Rect r) const { return r.maxY() - float((v - lo) / (hi - lo)) * r.h; }
    void drawFrame(Canvas& c, Rect r);
    /// Draws a polyline through points, breaking where values are not finite.
    void strokeCurve(Canvas& c, const std::vector<float>& xs, const std::vector<float>& ys, Color color, float width,
                     const std::vector<float>& dash = {});

    /// Renders the view at its current size into a PNG file using the light print theme,
    /// so the figure drops straight into a lab report.
    void exportPNG(const std::string& suggestedName, bool light = true);
    /// Hook for subclasses to rebuild caches before an off-screen render.
    virtual void prepareForExport() {}
};

/// 1, 2 or 5 x 10^n step close to `raw`.
double niceStep(double raw);
