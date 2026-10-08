#pragma once
#include "PlotView.h"
#include <memory>
#include <optional>

/// The z-plane: unit circle, poles (x) and zeros (o), an optional |H(z)| heat map, and the
/// geometric link between a frequency and its point e^{jw} on the circle.
class ZPlaneView : public PlotView {
public:
    explicit ZPlaneView(LabModel& model);
    void invalidate();
    void resetView();

    void draw(Canvas& c) override;
    void prepareForExport() override { heatDirty_ = true; }
    void mouseMoved(const MouseEvent& e) override;
    void mouseExited() override;
    bool scrollWheel(const ScrollEvent& e) override;
    void mouseDown(const MouseEvent& e) override;
    void mouseDragged(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;
    bool rightMouseDown(const MouseEvent& e) override;

private:
    struct Mark {
        Complex z;
        PZItem::Kind kind;
        int count;
        std::optional<ItemID> itemID;
        bool conjugate;
    };
    struct Drag {
        enum class Kind { item, root, pan } kind;
        ItemID id = 0;
        bool conjugate = false;
        Complex z;
        PZItem::Kind rootKind = PZItem::Kind::pole;
        Point start;
        Complex center;
    };

    double range() const { return autoRange_ / zoom_; }
    Rect square() const;
    Point point(Complex z) const;
    Complex complex(Point p) const;
    float pixelsPerUnit() const { return square().w / float(2 * range()); }
    void updateAutoRange();
    std::vector<Mark> marks() const;

    void drawGrid(Canvas& c, Rect s);
    void drawMark(Canvas& c, const Mark& m);
    void drawCursorLink(Canvas& c);
    void drawSourceDot(Canvas& c);
    void drawHoverInfo(Canvas& c, const std::vector<Mark>& all);
    std::optional<Mark> draggedMark(const std::vector<Mark>& all) const;
    std::optional<Mark> hitMark(Point p, const std::vector<Mark>& all) const;

    bool heatMapAvailable() const;
    void requestHeatMap();
    void zoomBy(double factor, Point p);
    Complex snapped(Complex z, PZItem::Kind kind, bool paired, bool snap) const;
    void moveItem(ItemID id, bool conjugate, Point p, bool snap);
    void addItem(Point p, PZItem::Kind kind, bool snap);
    void reflectSelected();
    void moveSelectedOntoCircle();

    double zoom_ = 1;
    Complex center_;
    double autoRange_ = 1.3;
    // Heat map pixels (BGRX) and their bitmap on the current device.
    std::shared_ptr<std::vector<uint32_t>> heat_;
    int heatN_ = 150;
    int heatVersion_ = 0;
    Com<ID2D1Bitmap> heatBitmap_;
    int heatBitmapDevice_ = 0;
    int heatBitmapVersion_ = -1;
    bool heatDirty_ = true;
    float heatSize_ = 0;
    std::shared_ptr<std::atomic<int>> heatGeneration_ = std::make_shared<std::atomic<int>>(0);
    std::optional<Point> hover_;
    std::optional<Drag> drag_;
    bool dragMoved_ = false;
    Point mouseDownPoint_;
};
