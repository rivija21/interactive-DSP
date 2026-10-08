#include "PlotView.h"
#include "../platform/Dialogs.h"
#include "../util/Format.h"
#include "../util/Text.h"
#include <wincodec.h>
#include <algorithm>
#include <cmath>

Rect PlotView::plotRect() const {
    return {insets.left, insets.top, std::max(10.0f, frame.w - insets.left - insets.right),
            std::max(10.0f, frame.h - insets.top - insets.bottom)};
}

void PlotView::fillBackground(Canvas& c) { c.fillRect(bounds(), theme().plotBackground); }

Rect PlotView::drawText(Canvas& c, const std::string& s, Point p, HAlign h, VAlign v, const Font& font, std::optional<Color> color) {
    return c.drawText(s, p, h, v, font, color ? *color : theme().axisText);
}

void PlotView::drawReadout(Canvas& c, const std::vector<std::string>& lines, Point p, Rect rect) {
    Font font = Fonts::readout();
    float w = 0;
    for (const auto& l : lines) w = std::max(w, measureText(l, font).w);
    w += 14;
    const float lineH = 15;
    float h = float(lines.size()) * lineH + 8;
    Point o{p.x + 12, p.y - h - 8};
    if (o.x + w > rect.maxX()) o.x = p.x - w - 12;
    if (o.y < rect.minY()) o.y = p.y + 12;
    Rect box{o.x, o.y, w, h};
    c.fillRoundedRect(box, 6, Color::white(0.96f));
    c.strokeRoundedRect(box, 6, theme().panelBorder, 1);
    for (size_t i = 0; i < lines.size(); i++) {
        const TextLayout& l = textLayout(lines[i], font);
        // Centre each line in its 15-point slot (the system font is a little taller than SF).
        c.drawLayout(l, {box.minX() + 7, box.minY() + 4 + float(i) * lineH + (lineH - l.size.h) / 2}, theme().text);
    }
}

float PlotView::xForFrequency(double f, Rect r) const {
    if (model.logAxis()) {
        double t = std::log(std::max(f, fMin()) / fMin()) / std::log(fMax() / fMin());
        return r.minX() + float(t) * r.w;
    }
    return r.minX() + float((f - fMin()) / (fMax() - fMin())) * r.w;
}

double PlotView::frequencyForX(float x, Rect r) const {
    double t = double((x - r.minX()) / r.w);
    if (model.logAxis()) return fMin() * std::pow(fMax() / fMin(), t);
    return fMin() + t * (fMax() - fMin());
}

std::vector<double> PlotView::frequencyGrid(int count) const {
    std::vector<double> out(size_t(std::max(count, 2)));
    for (size_t i = 0; i < out.size(); i++) {
        double t = double(i) / double(out.size() - 1);
        out[i] = model.logAxis() ? fMin() * std::pow(fMax() / fMin(), t) : std::max(0.5, fMin() + t * (fMax() - fMin()));
    }
    return out;
}

void PlotView::drawFrequencyGrid(Canvas& c, Rect r, bool labels) {
    std::vector<double> major, minor;
    if (model.logAxis()) {
        double decade = 1.0;
        while (decade < fMax() * 10) {
            for (int m = 1; m <= 9; m++) {
                double f = decade * double(m);
                if (!(f >= fMin() && f <= fMax())) continue;
                if (m == 1 || m == 2 || m == 5) major.push_back(f);
                else minor.push_back(f);
            }
            decade *= 10;
        }
    } else {
        double step = niceStep((fMax() - fMin()) / 7);
        double f = 0.0;
        while (f <= fMax() + 1e-9) {
            major.push_back(f);
            minor.push_back(f + step / 2);
            f += step;
        }
    }
    for (double f : minor) {
        if (f > fMax()) continue;
        float x = std::floor(xForFrequency(f, r)) + 0.5f;
        c.line({x, r.minY()}, {x, r.maxY()}, theme().gridMinor, 1);
    }
    for (double f : major) {
        float x = std::floor(xForFrequency(f, r)) + 0.5f;
        c.line({x, r.minY()}, {x, r.maxY()}, theme().gridMajor, 1);
    }
    if (!labels) return;
    float lastRight = -1e9f;
    for (double f : major) {
        float x = xForFrequency(f, r);
        std::string label = formatHzTick(f);
        float w = measureText(label, Fonts::axis()).w;
        if (!(x - w / 2 > lastRight + 4 && x + w / 2 < frame.w - 2)) continue;
        drawText(c, label, {x, r.maxY() + 4}, HAlign::center, VAlign::top);
        lastRight = x + w / 2;
    }
    drawText(c, "Hz", {r.minX() - 12, r.maxY() + 4}, HAlign::right, VAlign::top);
}

void PlotView::drawValueGrid(Canvas& c, Rect r, double lo, double hi, double step, const std::function<std::string(double)>& format,
                             const std::function<std::string(double)>& rightLabels) {
    double v = std::ceil(lo / step) * step;
    while (v <= hi + step * 1e-6) {
        float y = std::floor(yForValue(v, lo, hi, r)) + 0.5f;
        Color col = std::fabs(v) < step * 1e-6 ? theme().gridMajor.blended(0.35f, theme().axisText) : theme().gridMajor;
        c.line({r.minX(), y}, {r.maxX(), y}, col, 1);
        drawText(c, format(v), {r.minX() - 5, y}, HAlign::right);
        if (rightLabels) drawText(c, rightLabels(v), {r.maxX() + 5, y}, HAlign::left);
        v += step;
    }
}

void PlotView::drawFrame(Canvas& c, Rect r) { c.strokeRect(r.insetBy(-0.5f, -0.5f), theme().gridMajor, 1); }

void PlotView::strokeCurve(Canvas& c, const std::vector<float>& xs, const std::vector<float>& ys, Color color, float width,
                           const std::vector<float>& dash) {
    if (xs.size() != ys.size() || xs.empty()) return;
    Path path;
    bool penDown = false;
    for (size_t i = 0; i < xs.size(); i++) {
        float y = ys[i];
        if (!std::isfinite(y)) {
            penDown = false;
            continue;
        }
        if (penDown) path.lineTo({xs[i], y});
        else path.moveTo({xs[i], y});
        penDown = true;
    }
    c.strokePath(path, color, width, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, dash);
}

double niceStep(double raw) {
    if (!(raw > 0) || !std::isfinite(raw)) return 1;
    double p = std::pow(10.0, std::floor(std::log10(raw)));
    double m = raw / p;
    if (m < 1.5) return p;
    if (m < 3.5) return 2 * p;
    if (m < 7.5) return 5 * p;
    return 10 * p;
}

// MARK: - PNG export

namespace {
int gExportDevice = -1000;
}

void PlotView::exportPNG(const std::string& suggestedName, bool light) {
    const float scale = 2;
    Size size{frame.w, frame.h};
    if (size.w <= 1 || size.h <= 1) return;
    UINT pw = UINT(std::ceil(size.w * scale)), ph = UINT(std::ceil(size.h * scale));

    Com<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                reinterpret_cast<void**>(wic.put()))))
        return;
    Com<IWICBitmap> bitmap;
    if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.put()))) return;
    Com<ID2D1RenderTarget> rt;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f * scale, 96.0f * scale);
    if (FAILED(Graphics::shared().d2d->CreateWicBitmapRenderTarget(bitmap.get(), props, rt.put()))) return;
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    exporting = light;
    prepareForExport();
    rt->BeginDraw();
    rt->Clear(theme().panel.d2d());
    {
        Canvas c(rt.get(), --gExportDevice);
        Host::shared().drawTree(this, c, true);
    }
    rt->EndDraw();
    exporting = false;
    prepareForExport();
    setNeedsDisplay();

    auto path = chooseSaveLocation(model.window, suggestedName, "PNG image", "png",
                                   "Figures are saved with a white background, ready for a lab report.");
    if (!path) return;
    Com<IWICStream> stream;
    Com<IWICBitmapEncoder> encoder;
    Com<IWICBitmapFrameEncode> frameEnc;
    bool ok = SUCCEEDED(wic->CreateStream(stream.put())) &&
              SUCCEEDED(stream->InitializeFromFilename(path->c_str(), GENERIC_WRITE)) &&
              SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())) &&
              SUCCEEDED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) &&
              SUCCEEDED(encoder->CreateNewFrame(frameEnc.put(), nullptr)) && SUCCEEDED(frameEnc->Initialize(nullptr)) &&
              SUCCEEDED(frameEnc->SetSize(pw, ph));
    if (ok) {
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        frameEnc->SetPixelFormat(&format);
        frameEnc->SetResolution(144, 144);
        ok = SUCCEEDED(frameEnc->WriteSource(bitmap.get(), nullptr)) && SUCCEEDED(frameEnc->Commit()) &&
             SUCCEEDED(encoder->Commit());
    }
    if (!ok) showAlert(model.window, "Filter Lab", "The figure couldn't be saved.");
}
