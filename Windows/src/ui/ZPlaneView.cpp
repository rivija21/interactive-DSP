#include "ZPlaneView.h"
#include "../platform/Dispatch.h"
#include "../util/Format.h"
#include "../util/Text.h"
#include "Controls.h"
#include <algorithm>
#include <cmath>

namespace {

/// Renders log|H(z)| over the visible square as a soft colour wash: blue valleys at the
/// zeros, warm mountains at the poles. Pixels are BGRX.
std::shared_ptr<std::vector<uint32_t>> makeHeatMap(const ZPK& zpk, int n, double range, Complex center, Color zeroColor,
                                                   Color poleColor, Color background) {
    if (zpk.zeros.empty() && zpk.poles.empty()) return nullptr;
    auto pixels = std::make_shared<std::vector<uint32_t>>(size_t(n * n), 0u);
    const double zc[3] = {zeroColor.r, zeroColor.g, zeroColor.b};
    const double pc[3] = {poleColor.r, poleColor.g, poleColor.b};
    const double bg[3] = {background.r, background.g, background.b};
    double logGain = std::log10(std::max(std::fabs(zpk.gain), 1e-300));
    for (int row = 0; row < n; row++) {
        for (int col = 0; col < n; col++) {
            Complex z(center.re + (double(col) + 0.5) / double(n) * 2 * range - range,
                      center.im + range - (double(row) + 0.5) / double(n) * 2 * range);
            double l = logGain;
            for (const auto& q : zpk.zeros) l += 0.5 * std::log10(std::max((z - q).norm2(), 1e-300));
            for (const auto& p : zpk.poles) l -= 0.5 * std::log10(std::max((z - p).norm2(), 1e-300));
            // Valleys fade in over 100 dB, peaks over 40 dB, so the zeros don't flood the plane.
            double v = l < 0 ? std::max(-1.0, l / 5) : std::min(1.0, l / 2);
            if (!std::isfinite(v)) v = 0;
            const double* target = v < 0 ? zc : pc;
            double a = std::pow(std::fabs(v), 1.4) * (v < 0 ? 0.24 : 0.45);
            auto ch = [&](int k) {
                return uint32_t(std::max(0.0, std::min(255.0, (bg[k] + (target[k] - bg[k]) * a) * 255)));
            };
            (*pixels)[size_t(row * n + col)] = ch(2) | (ch(1) << 8) | (ch(0) << 16) | 0xFF000000u;
        }
    }
    return pixels;
}

} // namespace

ZPlaneView::ZPlaneView(LabModel& m) : PlotView(m) { insets = {8, 8, 8, 8}; }

void ZPlaneView::invalidate() {
    updateAutoRange();
    heatDirty_ = true;
    setNeedsDisplay();
}

void ZPlaneView::resetView() {
    zoom_ = 1;
    center_ = Complex();
    heatDirty_ = true;
    setNeedsDisplay();
}

// MARK: Geometry

Rect ZPlaneView::square() const {
    Rect r = plotRect();
    float side = std::min(r.w, r.h);
    return {r.midX() - side / 2, r.midY() - side / 2, side, side};
}

Point ZPlaneView::point(Complex z) const {
    Rect s = square();
    float scale = s.w / float(2 * range());
    return {s.midX() + float(z.re - center_.re) * scale, s.midY() - float(z.im - center_.im) * scale};
}

Complex ZPlaneView::complex(Point p) const {
    Rect s = square();
    double scale = double(s.w) / (2 * range());
    return Complex(double(p.x - s.midX()) / scale + center_.re, -double(p.y - s.midY()) / scale + center_.im);
}

void ZPlaneView::updateAutoRange() {
    double farthest = 0;
    for (const auto& r : model.filter().zpk.zeros)
        if (std::isfinite(r.magnitude())) farthest = std::max(farthest, r.magnitude());
    for (const auto& r : model.filter().zpk.poles)
        if (std::isfinite(r.magnitude())) farthest = std::max(farthest, r.magnitude());
    autoRange_ = std::min(4.0, std::max(1.3, farthest * 1.12));
}

// MARK: Roots to draw

std::vector<ZPlaneView::Mark> ZPlaneView::marks() const {
    std::vector<Mark> result;
    const auto& spec = model.spec();
    const auto& zpk = model.filter().zpk;
    if (spec.method == DesignMethod::poleZero) {
        for (const auto& item : spec.items) {
            result.push_back({item.position, item.kind, 1, item.id, false});
            if (item.paired) result.push_back({item.position.conj(), item.kind, 1, item.id, true});
        }
        // Origin roots added to keep the filter causal.
        auto originCount = [&](const std::vector<Complex>& roots, PZItem::Kind kind) {
            int n = 0;
            for (const auto& r : roots)
                if (r.magnitude() < 1e-12) n++;
            for (const auto& it : spec.items)
                if (it.kind == kind && it.position.magnitude() < 1e-12) n -= it.paired ? 2 : 1;
            return n;
        };
        int originZeros = originCount(zpk.zeros, PZItem::Kind::zero);
        int originPoles = originCount(zpk.poles, PZItem::Kind::pole);
        if (originZeros > 0) result.push_back({Complex(), PZItem::Kind::zero, originZeros, std::nullopt, false});
        if (originPoles > 0) result.push_back({Complex(), PZItem::Kind::pole, originPoles, std::nullopt, false});
        return result;
    }
    // Designed filters: group coincident roots and show their multiplicity.
    std::pair<PZItem::Kind, const std::vector<Complex>*> sets[] = {{PZItem::Kind::zero, &zpk.zeros},
                                                                  {PZItem::Kind::pole, &zpk.poles}};
    for (const auto& [kind, roots] : sets) {
        std::vector<std::pair<Complex, int>> groups;
        for (const auto& r : *roots) {
            bool found = false;
            for (auto& g : groups) {
                if ((g.first - r).magnitude() < 1e-6 * std::max(1.0, r.magnitude())) {
                    g.second += 1;
                    found = true;
                    break;
                }
            }
            if (!found) groups.push_back({r, 1});
        }
        for (const auto& [z, n] : groups) result.push_back({z, kind, n, std::nullopt, false});
    }
    return result;
}

// MARK: Drawing

void ZPlaneView::draw(Canvas& c) {
    c.fillRect(bounds(), theme().panel);
    Rect s = square();
    c.save();
    c.clip(s);
    c.fillRect(s, theme().plotBackground);

    if (model.showHMap() && heatMapAvailable()) {
        requestHeatMap();
        if (heat_) {
            if (!heatBitmap_ || heatBitmapDevice_ != c.deviceId() || heatBitmapVersion_ != heatVersion_) {
                heatBitmap_ = makeBitmap(c.rt(), heat_->data(), heatN_, heatN_);
                heatBitmapDevice_ = c.deviceId();
                heatBitmapVersion_ = heatVersion_;
            }
            c.drawBitmap(heatBitmap_.get(), s, nullptr, true);
        }
    }
    drawGrid(c, s);
    drawCursorLink(c);
    drawSourceDot(c);
    auto all = marks();
    for (const auto& m : all) drawMark(c, m);
    c.maskCorners(s, 6, theme().panel);
    c.restore();

    c.strokeRoundedRect(s.insetBy(0.5f, 0.5f), 6, theme().gridMajor, 1);

    if (model.showHMap() && !heatMapAvailable() && !exporting) {
        drawText(c, "|H| map: not shown for FIR", {s.minX() + 8, s.maxY() - 6}, HAlign::left, VAlign::bottom, Fonts::smallText(),
                 theme().secondaryText);
    }
    drawHoverInfo(c, all);
    if ((zoom_ != 1 || center_ != Complex()) && !exporting) {
        drawText(c, strf("zoom \xC3\x97%.1f", zoom_), {s.minX() + 8, s.maxY() - 6}, HAlign::left, VAlign::bottom, Fonts::axis(),
                 theme().secondaryText);
    }
}

void ZPlaneView::drawGrid(Canvas& c, Rect s) {
    Point o = point(Complex());
    float ppu = pixelsPerUnit();
    // Axes.
    c.line({s.minX(), o.y}, {s.maxX(), o.y}, theme().gridMajor, 1);
    c.line({o.x, s.minY()}, {o.x, s.maxY()}, theme().gridMajor, 1);
    // Faint rings and spokes.
    for (double r : {0.25, 0.5, 0.75}) {
        float rr = float(r) * ppu;
        c.strokeEllipse({o.x - rr, o.y - rr, 2 * rr, 2 * rr}, theme().gridMinor, 1);
    }
    Path spokes;
    for (int k = 1; k < 8; k++) {
        if (k == 4) continue;
        double a = double(k) * kPi / 8;
        spokes.moveTo(o);
        spokes.lineTo(point(Complex::polar(1, a)));
        spokes.moveTo(o);
        spokes.lineTo(point(Complex::polar(1, -a)));
    }
    c.strokePath(spokes, theme().gridMinor, 1);
    // Unit circle.
    c.strokeEllipse({o.x - ppu, o.y - ppu, 2 * ppu, 2 * ppu}, theme().unitCircle.withAlpha(0.9f), 1.6f);

    // Frequency labels around the upper half of the circle. 0 Hz and fs/2 sit just
    // inside the circle under the real axis so they never fall off the plot.
    double fs = model.fs();
    for (int k = 0; k <= 4; k++) {
        double a = double(k) * kPi / 4;
        double f = fs * double(k) / 8;
        Point p = point(Complex::polar(1, a));
        c.line(p, point(Complex::polar(1 + 5 / double(ppu), a)), theme().unitCircle.withAlpha(0.7f), 1);
        switch (k) {
        case 0: drawText(c, "0 Hz", {p.x + 6, p.y + 5}, HAlign::left, VAlign::top, Fonts::axis(), theme().secondaryText); break;
        case 4:
            drawText(c, "fs/2 = " + formatHz(f), {p.x + 6, p.y + 6}, HAlign::left, VAlign::top, Fonts::axis(), theme().secondaryText);
            break;
        default: {
            Point q = point(Complex::polar(1 + 20 / double(ppu), a));
            drawText(c, strf("%g kHz", std::round(f / 100) / 10), {q.x + (k == 2 ? 26.0f : 0.0f), q.y}, HAlign::center,
                     VAlign::middle, Fonts::axis(), theme().secondaryText);
        }
        }
    }
    drawText(c, "Re", {s.maxX() - 4, o.y - 3}, HAlign::right, VAlign::bottom);
    drawText(c, "Im", {o.x - 4, s.minY() + 3}, HAlign::right, VAlign::top);
}

void ZPlaneView::drawMark(Canvas& c, const Mark& m) {
    Point p = point(m.z);
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) return;
    bool selected = m.itemID && m.itemID == model.selectedItem();
    bool outside = m.kind == PZItem::Kind::pole && m.z.magnitude() >= 1 - 1e-9;
    Color color = m.kind == PZItem::Kind::pole ? (outside ? theme().danger : theme().pole) : theme().zero;
    const float size = 5.5f;
    if (selected) c.fillEllipse({p.x - 12, p.y - 12, 24, 24}, color.withAlpha(0.22f));
    if (m.kind == PZItem::Kind::zero) {
        c.strokeEllipse({p.x - size, p.y - size, size * 2, size * 2}, color, 2.2f);
    } else {
        Path x;
        x.moveTo({p.x - size, p.y - size});
        x.lineTo({p.x + size, p.y + size});
        x.moveTo({p.x - size, p.y + size});
        x.lineTo({p.x + size, p.y - size});
        c.strokePath(x, color, 2.2f, D2D1_CAP_STYLE_ROUND);
    }
    if (m.count > 1) drawText(c, std::to_string(m.count), {p.x + 7, p.y - 7}, HAlign::left, VAlign::bottom, Fonts::smallBold(), color);
}

void ZPlaneView::drawCursorLink(Canvas& c) {
    auto f = model.cursorFrequency();
    if (exporting || !f) return;
    double w = 2 * kPi * *f / model.fs();
    Point tp = point(Complex::polar(1, w));
    std::vector<std::pair<Complex, PZItem::Kind>> roots;
    for (const auto& z : model.filter().zpk.zeros)
        if (z.magnitude() > 1e-12) roots.push_back({z, PZItem::Kind::zero});
    for (const auto& p : model.filter().zpk.poles)
        if (p.magnitude() > 1e-12) roots.push_back({p, PZItem::Kind::pole});
    if (roots.size() <= 40) {
        for (const auto& [z, kind] : roots) {
            Color color = kind == PZItem::Kind::zero ? theme().zero : theme().pole;
            c.line(point(z), tp, color.withAlpha(0.45f), 1);
        }
    }
    c.fillEllipse({tp.x - 4.5f, tp.y - 4.5f, 9, 9}, theme().cursor);
    std::string label = strf("e^j\xCF\x89, \xCF\x89 = %.3f rad  (", w) + formatHz(*f) + ")";
    bool below = tp.y < square().midY();
    drawText(c, label, {tp.x, tp.y + (below ? -10.0f : 10.0f)}, tp.x > square().midX() ? HAlign::right : HAlign::left,
             below ? VAlign::bottom : VAlign::top, Fonts::smallText(), theme().text);
}

void ZPlaneView::drawSourceDot(Canvas& c) {
    if (exporting || !sourceIsTone(model.source())) return;
    double f = model.audio.params.currentFrequency.value();
    if (!(f > 0)) return;
    Point p = point(Complex::polar(1, 2 * kPi * f / model.fs()));
    c.fillEllipse({p.x - 5, p.y - 5, 10, 10}, theme().output);
    c.strokeEllipse({p.x - 8, p.y - 8, 16, 16}, theme().output.withAlpha(0.35f), 5);
}

void ZPlaneView::drawHoverInfo(Canvas& c, const std::vector<Mark>& all) {
    if (!hover_ || !square().contains(*hover_) || exporting || (drag_ && !dragMoved_)) return;
    auto m = hitMark(*hover_, all);
    if (!m && dragMoved_) m = draggedMark(all);
    if (m) {
        double r = m->z.magnitude(), a = std::fabs(m->z.phase());
        double f = a / (2 * kPi) * model.fs();
        std::vector<std::string> lines{m->kind == PZItem::Kind::pole ? "pole" : "zero"};
        if (m->count > 1) lines[0] += " \xC3\x97" + std::to_string(m->count);
        if (m->z.isReal(1e-9)) {
            lines.push_back("z = " + formatSigned(m->z.re));
        } else {
            lines.push_back(strf("r = %.4f, \xCE\xB8 = \xC2\xB1%.2f\xC2\xB0", r, a * 180 / kPi));
            lines.push_back("f = " + formatHz(f));
        }
        if (m->kind == PZItem::Kind::pole && r >= 1) lines.push_back("outside |z| = 1: unstable");
        drawReadout(c, lines, point(m->z), square());
    } else {
        Complex z = complex(*hover_);
        double h = model.filter().zpk.response(z).magnitude();
        double db = 20 * std::log10(std::max(h, 1e-30));
        std::vector<std::string> lines{strf("z = %.3f %s %.3fj", z.re, z.im < 0 ? kMinus : "+", std::fabs(z.im)),
                                       "|z| = " + strf("%.3f", z.magnitude()), "|H(z)| = " + formatDB(db)};
        drawReadout(c, lines, *hover_, square());
    }
}

std::optional<ZPlaneView::Mark> ZPlaneView::draggedMark(const std::vector<Mark>& all) const {
    if (drag_ && drag_->kind == Drag::Kind::item) {
        for (const auto& m : all)
            if (m.itemID == drag_->id && m.conjugate == drag_->conjugate) return m;
    }
    return std::nullopt;
}

std::optional<ZPlaneView::Mark> ZPlaneView::hitMark(Point p, const std::vector<Mark>& all) const {
    std::optional<Mark> best;
    float bestD = 10;
    for (const auto& m : all) {
        Point q = point(m.z);
        float d = std::hypot(q.x - p.x, q.y - p.y);
        if (d < bestD) {
            bestD = d;
            best = m;
        }
    }
    return best;
}

// MARK: |H(z)| heat map

bool ZPlaneView::heatMapAvailable() const {
    // A long FIR filter is a polynomial of degree N-1: its |H(z)| swings by 20(N-1)log10|z| dB
    // across the plane, which would drown the picture, so the map is only drawn for IIR
    // and hand-made filters.
    return model.spec().method != DesignMethod::fir &&
           model.filter().zpk.zeros.size() + model.filter().zpk.poles.size() <= 80;
}

void ZPlaneView::requestHeatMap() {
    Rect s = square();
    if (s.w != heatSize_) heatDirty_ = true;
    if (!heatDirty_) return;
    if (model.zerosPending()) {
        heat_.reset();
        return;
    }
    heatDirty_ = false;
    heatSize_ = s.w;
    int generation = ++(*heatGeneration_);
    // Roots at the origin only add delay (a factor of z^-n), but n of them would raise a
    // huge mountain in the middle, so the map leaves them out.
    ZPK zpk = model.filter().zpk;
    zpk.zeros.erase(std::remove_if(zpk.zeros.begin(), zpk.zeros.end(), [](const Complex& z) { return !(z.magnitude() > 1e-12); }),
                    zpk.zeros.end());
    zpk.poles.erase(std::remove_if(zpk.poles.begin(), zpk.poles.end(), [](const Complex& z) { return !(z.magnitude() > 1e-12); }),
                    zpk.poles.end());
    double rng = range();
    Complex ctr = center_;
    Color zc = theme().zero, pc = theme().pole, bg = theme().plotBackground;
    int n = heatN_;
    if (exporting) {
        heat_ = makeHeatMap(zpk, n, rng, ctr, zc, pc, bg);
        heatVersion_++;
        return;
    }
    auto token = heatGeneration_;
    backgroundQueue().async([this, token, generation, zpk, n, rng, ctr, zc, pc, bg] {
        if (token->load() != generation) return;   // a newer request supersedes this one
        auto image = makeHeatMap(zpk, n, rng, ctr, zc, pc, bg);
        dispatchMain([this, token, generation, image] {
            if (token->load() != generation) return;
            heat_ = image;
            heatVersion_++;
            setNeedsDisplay();
        });
    });
}

// MARK: Mouse

void ZPlaneView::mouseMoved(const MouseEvent& e) {
    hover_ = e.location;
    setNeedsDisplay();
}

void ZPlaneView::mouseExited() {
    hover_.reset();
    setNeedsDisplay();
}

bool ZPlaneView::scrollWheel(const ScrollEvent& e) {
    zoomBy(std::exp(double(e.deltaY) * 0.1), e.location);
    return true;
}

void ZPlaneView::zoomBy(double factor, Point p) {
    Complex before = complex(p);
    zoom_ = std::min(400.0, std::max(0.6, zoom_ * factor));
    Complex after = complex(p);
    center_ = center_ + (before - after);
    if (zoom_ <= 1.0001 && std::fabs(zoom_ - 1) < 0.05) center_ = Complex();
    heatDirty_ = true;
    setNeedsDisplay();
}

void ZPlaneView::mouseDown(const MouseEvent& e) {
    Point p = e.location;
    mouseDownPoint_ = p;
    dragMoved_ = false;
    drag_.reset();
    if (e.clickCount == 2 && !hitMark(p, marks())) {
        resetView();
        return;
    }
    switch (model.zTool()) {
    case ZTool::addZero:
    case ZTool::addPole:
        addItem(p, model.zTool() == ZTool::addZero ? PZItem::Kind::zero : PZItem::Kind::pole, !e.alt);
        return;
    case ZTool::move: break;
    }
    if (auto m = hitMark(p, marks())) {
        if (m->itemID) {
            model.setSelectedItem(*m->itemID);
            Drag d{Drag::Kind::item};
            d.id = *m->itemID;
            d.conjugate = m->conjugate;
            drag_ = d;
        } else if (m->z.magnitude() > 1e-12) {
            Drag d{Drag::Kind::root};
            d.z = m->z;
            d.rootKind = m->kind;
            drag_ = d;
        }
    } else {
        model.setSelectedItem(std::nullopt);
        Drag d{Drag::Kind::pan};
        d.start = p;
        d.center = center_;
        drag_ = d;
    }
    setNeedsDisplay();
}

void ZPlaneView::mouseDragged(const MouseEvent& e) {
    Point p = e.location;
    hover_ = p;
    if (std::hypot(p.x - mouseDownPoint_.x, p.y - mouseDownPoint_.y) > 2) dragMoved_ = true;
    if (!dragMoved_ || !drag_) return;
    Drag d = *drag_;
    switch (d.kind) {
    case Drag::Kind::pan: {
        double s = double(pixelsPerUnit());
        center_ = d.center - Complex(double(p.x - d.start.x) / s, -double(p.y - d.start.y) / s);
        heatDirty_ = true;
        setNeedsDisplay();
        break;
    }
    case Drag::Kind::root: {
        // Grabbing a designed filter's root turns it into a hand-edited one.
        model.convertToPoleZero();
        Complex target = d.z.im < 0 ? d.z.conj() : d.z;
        const PZItem* best = nullptr;
        for (const auto& it : model.spec().items) {
            if (it.kind != d.rootKind) continue;
            if (!best || (it.position - target).magnitude() < (best->position - target).magnitude()) best = &it;
        }
        if (best) {
            ItemID id = best->id;
            model.setSelectedItem(id);
            Drag nd{Drag::Kind::item};
            nd.id = id;
            nd.conjugate = d.z.im < 0;
            drag_ = nd;
            moveItem(id, d.z.im < 0, p, !e.alt);
        }
        break;
    }
    case Drag::Kind::item: moveItem(d.id, d.conjugate, p, !e.alt); break;
    }
}

void ZPlaneView::mouseUp(const MouseEvent&) {
    drag_.reset();
    dragMoved_ = false;
    setNeedsDisplay();
}

Complex ZPlaneView::snapped(Complex z, PZItem::Kind kind, bool paired, bool snap) const {
    double tol = double(7 / pixelsPerUnit());
    if (!paired) z.im = 0;
    else if (std::fabs(z.im) < tol) z.im = 0;
    if (snap && kind == PZItem::Kind::zero && std::fabs(z.magnitude() - 1) < tol) z = Complex::polar(1, z.phase());
    if (snap && std::fabs(z.magnitude()) < tol) z = Complex();
    return z;
}

void ZPlaneView::moveItem(ItemID id, bool conjugate, Point p, bool snap) {
    auto items = model.spec().items;
    auto it = std::find_if(items.begin(), items.end(), [&](const PZItem& i) { return i.id == id; });
    if (it == items.end()) return;
    Complex z = complex(p);
    if (conjugate) z = z.conj();
    if (it->paired && z.im < 0) z = z.conj();
    z = snapped(z, it->kind, it->paired, snap);
    it->position = Complex(z.re, it->paired ? std::fabs(z.im) : 0);
    model.setItems(items);
}

void ZPlaneView::addItem(Point p, PZItem::Kind kind, bool snap) {
    if (model.spec().method != DesignMethod::poleZero) model.convertToPoleZero();
    Complex z = complex(p);
    double tol = double(8 / pixelsPerUnit());
    bool real = std::fabs(z.im) < tol;
    z = snapped(z, kind, !real, snap);
    PZItem item(kind, real ? Complex(z.re) : Complex(z.re, std::fabs(z.im)), !real);
    auto items = model.spec().items;
    items.push_back(item);
    model.setItems(items);
    model.setSelectedItem(item.id);
}

bool ZPlaneView::rightMouseDown(const MouseEvent& e) {
    auto m = hitMark(e.location, marks());
    if (!m) return true;
    Point at = toWindow(e.location);
    if (!m->itemID) {
        if (!(m->z.magnitude() > 1e-12)) return true;
        if (showPopupMenu({{"Edit Poles and Zeros by Hand"}}, at) == 0) model.convertToPoleZero();
        return true;
    }
    model.setSelectedItem(*m->itemID);
    std::vector<MenuEntry> entries{{"Delete"}};
    std::vector<int> actions{0};
    if (m->kind == PZItem::Kind::pole && m->z.magnitude() >= 1) {
        entries.push_back({"Reflect Inside Unit Circle (1/p*)"});
        actions.push_back(1);
    }
    if (m->kind == PZItem::Kind::zero && std::fabs(m->z.magnitude() - 1) > 1e-9 && m->z.magnitude() > 0) {
        entries.push_back({"Move onto Unit Circle"});
        actions.push_back(2);
    }
    int chosen = showPopupMenu(entries, at);
    if (chosen < 0) return true;
    switch (actions[size_t(chosen)]) {
    case 0: model.deleteSelectedItem(); break;
    case 1: reflectSelected(); break;
    case 2: moveSelectedOntoCircle(); break;
    }
    return true;
}

void ZPlaneView::reflectSelected() {
    auto id = model.selectedItem();
    if (!id) return;
    auto items = model.spec().items;
    for (auto& it : items) {
        if (it.id != *id) continue;
        Complex z = it.position;
        it.position = Complex::polar(1 / z.magnitude(), z.phase());
        model.setItems(items);
        return;
    }
}

void ZPlaneView::moveSelectedOntoCircle() {
    auto id = model.selectedItem();
    if (!id) return;
    auto items = model.spec().items;
    for (auto& it : items) {
        if (it.id != *id) continue;
        Complex z = it.position;
        it.position = Complex::polar(1, z.phase());
        if (!it.paired) it.position = Complex(z.re >= 0 ? 1 : -1);
        model.setItems(items);
        return;
    }
}
