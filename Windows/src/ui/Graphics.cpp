#include "Graphics.h"
#include "../util/Text.h"
#include <algorithm>
#include <map>
#include <unordered_map>

#ifndef D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
#define D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT D2D1_DRAW_TEXT_OPTIONS(4)
#endif

namespace {

/// Baseline of the first line. GetLineMetrics fails unless the buffer fits every line.
float firstBaseline(IDWriteTextLayout* layout) {
    UINT32 count = 0;
    layout->GetLineMetrics(nullptr, 0, &count);
    if (count == 0) return 0;
    std::vector<DWRITE_LINE_METRICS> lines(count);
    if (FAILED(layout->GetLineMetrics(lines.data(), count, &count))) return 0;
    return lines[0].baseline;
}

const wchar_t* familyName(FontFamily f) {
    switch (f) {
    case FontFamily::system: return L"Segoe UI";
    case FontFamily::serif: return L"Georgia";
    case FontFamily::math: return L"Cambria";
    case FontFamily::mono: return L"Consolas";
    }
    return L"Segoe UI";
}

std::string fontKey(const Font& f) {
    return strf("%d|%.2f|%d|%d|%.2f", int(f.family), f.size, int(f.weight), f.tabular ? 1 : 0, f.kern);
}

/// Applies tabular figures and letter spacing to a whole layout.
void applyFontExtras(IDWriteTextLayout* layout, const Font& font, DWRITE_TEXT_RANGE range) {
    auto& g = Graphics::shared();
    if (font.tabular) {
        Com<IDWriteTypography> typo;
        if (SUCCEEDED(g.dwrite->CreateTypography(typo.put()))) {
            DWRITE_FONT_FEATURE feature = {DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1};
            typo->AddFontFeature(feature);
            layout->SetTypography(typo.get(), range);
        }
    }
    if (font.kern != 0) {
        Com<IDWriteTextLayout1> l1;
        if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1), reinterpret_cast<void**>(l1.put()))))
            l1->SetCharacterSpacing(0, font.kern, 0, range);
    }
}

void applyFont(IDWriteTextLayout* layout, const Font& font, DWRITE_TEXT_RANGE range) {
    layout->SetFontFamilyName(familyName(font.family), range);
    layout->SetFontSize(font.size, range);
    layout->SetFontWeight(DWRITE_FONT_WEIGHT(int(font.weight)), range);
    applyFontExtras(layout, font, range);
}

// MARK: Colour effect for rich text

const GUID kIidColorEffect = {0x6f1d2b8a, 0x3c55, 0x4e1d, {0x9a, 0x61, 0x2e, 0x55, 0x18, 0x0b, 0x7c, 0x42}};

class ColorEffect : public IUnknown {
public:
    explicit ColorEffect(Color c) : color(c) {}
    virtual ~ColorEffect() = default;
    Color color;
    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(++refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        long r = --refs_;
        if (r == 0) delete this;
        return ULONG(r);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, kIidColorEffect)) {
            *out = this;
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

private:
    long refs_ = 1;
};

struct DrawContext {
    ID2D1RenderTarget* rt;
    ID2D1SolidColorBrush* brush;
};

Color effectColor(IUnknown* effect) {
    if (!effect) return Color{};
    ColorEffect* ce = nullptr;
    if (SUCCEEDED(effect->QueryInterface(kIidColorEffect, reinterpret_cast<void**>(&ce)))) {
        Color c = ce->color;
        ce->Release();
        return c;
    }
    return Color{};
}

/// Draws rich text glyph runs with per-range colours (the drawing effects).
class RichRenderer : public IDWriteTextRenderer {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, __uuidof(IDWriteTextRenderer)) ||
            IsEqualGUID(riid, __uuidof(IDWritePixelSnapping))) {
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override {
        *disabled = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void* ctx, DWRITE_MATRIX* m) override {
        auto* c = static_cast<DrawContext*>(ctx);
        c->rt->GetTransform(reinterpret_cast<D2D1_MATRIX_3X2_F*>(m));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void* ctx, FLOAT* ppd) override {
        auto* c = static_cast<DrawContext*>(ctx);
        float dx, dy;
        c->rt->GetDpi(&dx, &dy);
        *ppd = dx / 96.0f;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void* ctx, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE mode,
                                           const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION*,
                                           IUnknown* effect) override {
        auto* c = static_cast<DrawContext*>(ctx);
        c->brush->SetColor(effectColor(effect).d2d());
        c->rt->DrawGlyphRun(D2D1::Point2F(x, y), run, c->brush, mode);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void* ctx, FLOAT x, FLOAT y, const DWRITE_UNDERLINE* u, IUnknown* effect) override {
        auto* c = static_cast<DrawContext*>(ctx);
        c->brush->SetColor(effectColor(effect).d2d());
        c->rt->FillRectangle(D2D1::RectF(x, y + u->offset, x + u->width, y + u->offset + u->thickness), c->brush);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void* ctx, FLOAT x, FLOAT y, IDWriteInlineObject* obj, BOOL sideways, BOOL rtl,
                                               IUnknown* effect) override {
        return obj->Draw(ctx, this, x, y, sideways, rtl, effect);
    }
};

RichRenderer gRenderer;

/// A superscript or subscript run, drawn smaller on a raised or lowered baseline.
class ScriptInline : public IDWriteInlineObject {
public:
    ScriptInline(const std::wstring& text, const Font& base, Color color, int script) {
        auto& g = Graphics::shared();
        Font scriptFont = base.withSize(base.size * 0.72f);
        g.dwrite->CreateTextLayout(text.c_str(), UINT32(text.size()), g.format(scriptFont), 10000, 1000, inner_.put());
        inner_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TEXT_RANGE all = {0, UINT32(text.size())};
        applyFontExtras(inner_.get(), scriptFont, all);
        effect_ = new ColorEffect(color);
        inner_->SetDrawingEffect(effect_, all);
        DWRITE_TEXT_METRICS m;
        inner_->GetMetrics(&m);
        width_ = m.widthIncludingTrailingWhitespace;
        height_ = m.height;
        innerBaseline_ = firstBaseline(inner_.get());
        shift_ = script > 0 ? base.size * 0.38f : -base.size * 0.18f;
    }
    virtual ~ScriptInline() { effect_->Release(); }

    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(++refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        long r = --refs_;
        if (r == 0) delete this;
        return ULONG(r);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, __uuidof(IDWriteInlineObject))) {
            *out = this;
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE Draw(void* ctx, IDWriteTextRenderer* renderer, FLOAT x, FLOAT y, BOOL, BOOL, IUnknown*) override {
        return inner_->Draw(ctx, renderer, x, y);
    }
    HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_INLINE_OBJECT_METRICS* m) override {
        m->width = width_;
        m->height = height_;
        m->baseline = innerBaseline_ + shift_;
        m->supportsSideways = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetOverhangMetrics(DWRITE_OVERHANG_METRICS* o) override {
        *o = {0, 0, 0, 0};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetBreakConditions(DWRITE_BREAK_CONDITION* before, DWRITE_BREAK_CONDITION* after) override {
        *before = DWRITE_BREAK_CONDITION_NEUTRAL;
        *after = DWRITE_BREAK_CONDITION_NEUTRAL;
        return S_OK;
    }

private:
    long refs_ = 1;
    Com<IDWriteTextLayout> inner_;
    ColorEffect* effect_ = nullptr;
    float width_ = 0, height_ = 0, innerBaseline_ = 0, shift_ = 0;
};

struct FontMetricsEntry {
    float ascent, lineHeight;   // per unit size
};

} // namespace

// MARK: - Graphics

Graphics& Graphics::shared() {
    static Graphics* g = new Graphics();
    return *g;
}

Graphics::Graphics() {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr,
                      reinterpret_cast<void**>(d2d1.put()));
    if (d2d1) {
        d2d1->AddRef();
        *d2d.put() = d2d1.get();
    }
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite.put()));
}

IDWriteTextFormat* Graphics::format(const Font& font) {
    static std::map<std::string, Com<IDWriteTextFormat>> cache;
    std::string key = fontKey(font);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second.get();
    Com<IDWriteTextFormat> f;
    dwrite->CreateTextFormat(familyName(font.family), nullptr, DWRITE_FONT_WEIGHT(int(font.weight)), DWRITE_FONT_STYLE_NORMAL,
                             DWRITE_FONT_STRETCH_NORMAL, font.size, L"en-us", f.put());
    IDWriteTextFormat* raw = f.get();
    cache[key] = std::move(f);
    return raw;
}

static FontMetricsEntry metricsFor(const Font& font) {
    static std::map<std::string, FontMetricsEntry> cache;
    std::string key = strf("%d|%d", int(font.family), int(font.weight));
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    FontMetricsEntry e{0.93f, 1.33f};
    auto& g = Graphics::shared();
    Com<IDWriteFontCollection> fonts;
    if (SUCCEEDED(g.dwrite->GetSystemFontCollection(fonts.put(), FALSE))) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        fonts->FindFamilyName(familyName(font.family), &index, &exists);
        Com<IDWriteFontFamily> family;
        Com<IDWriteFont> f;
        if (exists && SUCCEEDED(fonts->GetFontFamily(index, family.put())) &&
            SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT(int(font.weight)), DWRITE_FONT_STRETCH_NORMAL,
                                                   DWRITE_FONT_STYLE_NORMAL, f.put()))) {
            DWRITE_FONT_METRICS m;
            f->GetMetrics(&m);
            float u = float(m.designUnitsPerEm);
            e.ascent = float(m.ascent) / u;
            e.lineHeight = float(m.ascent + m.descent + m.lineGap) / u;
        }
    }
    cache[key] = e;
    return e;
}

float Graphics::lineHeight(const Font& font) { return metricsFor(font).lineHeight * font.size; }
float Graphics::ascent(const Font& font) { return metricsFor(font).ascent * font.size; }

ID2D1StrokeStyle* Graphics::strokeStyle(D2D1_CAP_STYLE cap, D2D1_LINE_JOIN join, const std::vector<float>& dashes) {
    static std::map<std::string, Com<ID2D1StrokeStyle>> cache;
    std::string key = strf("%d|%d", int(cap), int(join));
    for (float d : dashes) key += strf("|%.3f", d);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second.get();
    Com<ID2D1StrokeStyle> s;
    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(cap, cap, cap, join, 10.0f,
                                                                     dashes.empty() ? D2D1_DASH_STYLE_SOLID : D2D1_DASH_STYLE_CUSTOM);
    d2d->CreateStrokeStyle(props, dashes.empty() ? nullptr : dashes.data(), UINT32(dashes.size()), s.put());
    ID2D1StrokeStyle* raw = s.get();
    cache[key] = std::move(s);
    return raw;
}

// MARK: - Text layouts

static std::unordered_map<std::string, TextLayout>& layoutCache() {
    static auto* cache = new std::unordered_map<std::string, TextLayout>();
    return *cache;
}

void textCacheMaintenance() {
    // Cleared only between frames, so references handed out during a frame stay valid.
    if (layoutCache().size() > 6000) layoutCache().clear();
}

const TextLayout& textLayout(const std::string& text, const Font& font, TextMode mode, float width, DWRITE_TEXT_ALIGNMENT align) {
    auto& cache = layoutCache();
    std::string key = fontKey(font) + strf("|%d|%.1f|%d|", int(mode), mode == TextMode::singleLine ? 0.0f : width, int(align)) + text;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    auto& g = Graphics::shared();
    std::wstring w = widen(text);
    TextLayout tl;
    for (size_t i = 0; i < w.size(); i++) {
        // Emoji live in the supplementary planes (surrogate pairs) and around U+2600-U+27BF.
        if ((w[i] >= 0xD800 && w[i] <= 0xDBFF) || (w[i] >= 0x2600 && w[i] <= 0x27BF)) tl.colorGlyphs = true;
    }
    float maxW = mode == TextMode::singleLine ? 100000.0f : std::max(1.0f, width);
    g.dwrite->CreateTextLayout(w.c_str(), UINT32(w.size()), g.format(font), maxW, 100000.0f, tl.layout.put());
    if (tl.layout) {
        DWRITE_TEXT_RANGE all = {0, UINT32(w.size())};
        applyFontExtras(tl.layout.get(), font, all);
        tl.layout->SetTextAlignment(align);
        if (mode == TextMode::wrap) {
            tl.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        } else {
            tl.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        if (mode == TextMode::truncate) {
            Com<IDWriteInlineObject> sign;
            g.dwrite->CreateEllipsisTrimmingSign(g.format(font), sign.put());
            DWRITE_TRIMMING trimming = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            tl.layout->SetTrimming(&trimming, sign.get());
        }
        DWRITE_TEXT_METRICS m;
        tl.layout->GetMetrics(&m);
        tl.size = {m.widthIncludingTrailingWhitespace, m.height};
        if (mode != TextMode::singleLine) tl.size.w = std::min(tl.size.w, maxW);
        tl.baseline = firstBaseline(tl.layout.get());
        if (w.empty()) tl.size.h = g.lineHeight(font);
    }
    return cache.emplace(key, std::move(tl)).first->second;
}

Size measureText(const std::string& text, const Font& font) { return textLayout(text, font).size; }

float wrappedHeight(const std::string& text, const Font& font, float width) {
    return textLayout(text, font, TextMode::wrap, width).size.h;
}

// MARK: - Rich paragraphs

RichParagraph::RichParagraph(const Paragraph& p, float width) : source_(p) {
    auto& g = Graphics::shared();
    textX_ = p.bullet ? 14 : p.indent;
    Font base = p.runs.empty() ? Font{} : p.runs.front().font;
    for (const auto& r : p.runs) text_ += widen(r.text);
    if (text_.empty()) text_ = L"";
    g.dwrite->CreateTextLayout(text_.c_str(), UINT32(text_.size()), g.format(base), std::max(1.0f, width - textX_), 100000.0f,
                               layout_.put());
    if (!layout_) return;
    layout_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    float lh = 0, asc = 0;
    UINT32 pos = 0;
    for (const auto& r : p.runs) {
        std::wstring w = widen(r.text);
        DWRITE_TEXT_RANGE range = {pos, UINT32(w.size())};
        if (range.length > 0) {
            if (r.script != 0) {
                auto* obj = new ScriptInline(w, r.font, r.color, r.script);
                layout_->SetInlineObject(obj, range);
                owned_.push_back(obj);
            } else {
                applyFont(layout_.get(), r.font, range);
                auto* effect = new ColorEffect(r.color);
                layout_->SetDrawingEffect(effect, range);
                owned_.push_back(effect);
                lh = std::max(lh, g.lineHeight(r.font));
                asc = std::max(asc, g.ascent(r.font));
            }
            if (!r.link.empty()) links_.push_back({pos, range.length, r.link});
        }
        pos += range.length;
    }
    if (lh == 0) {
        lh = g.lineHeight(base);
        asc = g.ascent(base);
    }
    // Uniform line height: tallest font plus the paragraph's line spacing (TextKit adds it below each line).
    layout_->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, lh + p.lineSpacing, asc);
    DWRITE_TEXT_METRICS m;
    layout_->GetMetrics(&m);
    height_ = text_.empty() ? lh + p.lineSpacing : m.height;
    if (p.bullet && !p.runs.empty()) {
        bulletFont_ = p.runs.front().font;
        bulletColor_ = p.runs.front().color;
    }
}

RichParagraph::~RichParagraph() {
    layout_.reset();
    for (auto* o : owned_) o->Release();
}

std::string RichParagraph::linkAt(Point p) const {
    if (!layout_ || links_.empty()) return {};
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS hm;
    layout_->HitTestPoint(p.x - textX_, p.y, &trailing, &inside, &hm);
    if (!inside) return {};
    for (const auto& l : links_)
        if (hm.textPosition >= l.start && hm.textPosition < l.start + l.length) return l.link;
    return {};
}

UINT32 RichParagraph::positionAt(Point p) const {
    if (!layout_) return 0;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS hm;
    layout_->HitTestPoint(p.x - textX_, p.y, &trailing, &inside, &hm);
    UINT32 pos = hm.textPosition + (trailing ? hm.length : 0);
    return std::min(pos, UINT32(text_.size()));
}

std::vector<Rect> RichParagraph::selectionRects(UINT32 from, UINT32 to) const {
    std::vector<Rect> out;
    if (!layout_ || to <= from) return out;
    UINT32 count = 0;
    layout_->HitTestTextRange(from, to - from, textX_, 0, nullptr, 0, &count);
    if (count == 0) return out;
    std::vector<DWRITE_HIT_TEST_METRICS> hm(count);
    layout_->HitTestTextRange(from, to - from, textX_, 0, hm.data(), count, &count);
    for (const auto& m : hm) out.push_back({m.left, m.top, m.width, m.height});
    return out;
}

// MARK: - Paths

void Path::moveTo(Point p) { figures_.push_back({{D2D1::Point2F(p.x, p.y)}, false}); }

void Path::lineTo(Point p) {
    if (figures_.empty()) moveTo(p);
    else figures_.back().points.push_back(D2D1::Point2F(p.x, p.y));
}

void Path::close() {
    if (!figures_.empty()) figures_.back().closed = true;
}

void Path::addRect(Rect r) {
    moveTo({r.minX(), r.minY()});
    lineTo({r.maxX(), r.minY()});
    lineTo({r.maxX(), r.maxY()});
    lineTo({r.minX(), r.maxY()});
    close();
}

Com<ID2D1PathGeometry> Path::build() const {
    Com<ID2D1PathGeometry> geo;
    Graphics::shared().d2d->CreatePathGeometry(geo.put());
    if (!geo) return geo;
    Com<ID2D1GeometrySink> sink;
    geo->Open(sink.put());
    for (const auto& f : figures_) {
        if (f.points.empty()) continue;
        sink->BeginFigure(f.points[0], D2D1_FIGURE_BEGIN_FILLED);
        if (f.points.size() > 1) sink->AddLines(f.points.data() + 1, UINT32(f.points.size() - 1));
        sink->EndFigure(f.closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    }
    sink->Close();
    return geo;
}

// MARK: - Canvas

Canvas::Canvas(ID2D1RenderTarget* rt, int deviceId) : rt_(rt), deviceId_(deviceId) {
    rt_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), brush_.put());
}

Canvas::~Canvas() {
    while (!clips_.empty()) {
        if (clips_.back().layer) rt_->PopLayer();
        else rt_->PopAxisAlignedClip();
        clips_.pop_back();
    }
}

ID2D1SolidColorBrush* Canvas::brush(Color c) {
    brush_->SetColor(c.d2d());
    return brush_.get();
}

void Canvas::save() {
    State s;
    rt_->GetTransform(&s.transform);
    s.clips = clips_.size();
    states_.push_back(s);
}

void Canvas::restore() {
    if (states_.empty()) return;
    State s = states_.back();
    states_.pop_back();
    while (clips_.size() > s.clips) {
        if (clips_.back().layer) rt_->PopLayer();
        else rt_->PopAxisAlignedClip();
        clips_.pop_back();
    }
    rt_->SetTransform(s.transform);
}

void Canvas::translate(float dx, float dy) {
    D2D1_MATRIX_3X2_F m;
    rt_->GetTransform(&m);
    m._31 += dx * m._11 + dy * m._21;
    m._32 += dx * m._12 + dy * m._22;
    rt_->SetTransform(m);
}

void Canvas::clip(Rect r) {
    rt_->PushAxisAlignedClip(r.d2d(), D2D1_ANTIALIAS_MODE_ALIASED);
    clips_.push_back({false});
}

void Canvas::clipRoundedRect(Rect r, float radius) {
    Com<ID2D1RoundedRectangleGeometry> geo;
    Graphics::shared().d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r.d2d(), radius, radius), geo.put());
    rt_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geo.get()), nullptr);
    clips_.push_back({true});
}

void Canvas::fillRect(Rect r, Color c) { rt_->FillRectangle(r.d2d(), brush(c)); }

void Canvas::strokeRect(Rect r, Color c, float width) { rt_->DrawRectangle(r.d2d(), brush(c), width); }

void Canvas::fillRoundedRect(Rect r, float radius, Color c) {
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r.d2d(), radius, radius), brush(c));
}

void Canvas::strokeRoundedRect(Rect r, float radius, Color c, float width) {
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(r.d2d(), radius, radius), brush(c), width);
}

void Canvas::fillEllipse(Rect r, Color c) {
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.midX(), r.midY()), r.w / 2, r.h / 2), brush(c));
}

void Canvas::strokeEllipse(Rect r, Color c, float width) {
    rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(r.midX(), r.midY()), r.w / 2, r.h / 2), brush(c), width);
}

void Canvas::line(Point a, Point b, Color c, float width, const std::vector<float>& dashes, D2D1_CAP_STYLE cap) {
    if (dashes.empty() && cap == D2D1_CAP_STYLE_FLAT && (a.x == b.x || a.y == b.y)) {
        // Axis-aligned lines are rectangles: by far the cheapest thing Direct2D can draw.
        float h = width / 2;
        if (a.x == b.x) fillRect({a.x - h, std::min(a.y, b.y), width, std::fabs(b.y - a.y)}, c);
        else fillRect({std::min(a.x, b.x), a.y - h, std::fabs(b.x - a.x), width}, c);
        return;
    }
    std::vector<float> d;
    for (float v : dashes) d.push_back(v / width);
    ID2D1StrokeStyle* style = Graphics::shared().strokeStyle(cap, D2D1_LINE_JOIN_MITER, d);
    rt_->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), brush(c), width, style);
}

void Canvas::strokePath(const Path& p, Color c, float width, D2D1_CAP_STYLE cap, D2D1_LINE_JOIN join,
                        const std::vector<float>& dashes) {
    if (p.empty()) return;
    auto geo = p.build();
    if (!geo) return;
    // Core Graphics dash lengths are in points; Direct2D's are in multiples of the line width.
    std::vector<float> d;
    for (float v : dashes) d.push_back(v / width);
    rt_->DrawGeometry(geo.get(), brush(c), width, Graphics::shared().strokeStyle(cap, join, d));
}

void Canvas::fillPath(const Path& p, Color c, bool antialias) {
    if (p.empty()) return;
    auto geo = p.build();
    if (!geo) return;
    if (!antialias) rt_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    rt_->FillGeometry(geo.get(), brush(c));
    if (!antialias) rt_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Canvas::maskCorners(Rect r, float radius, Color outside) {
    Path mask;
    const int steps = 8;
    const float pi = 3.14159265f;
    struct Corner {
        float cx, cy, x, y, a0;
    } corners[] = {
        {r.minX() + radius, r.minY() + radius, r.minX(), r.minY(), pi},
        {r.maxX() - radius, r.minY() + radius, r.maxX(), r.minY(), 1.5f * pi},
        {r.maxX() - radius, r.maxY() - radius, r.maxX(), r.maxY(), 0},
        {r.minX() + radius, r.maxY() - radius, r.minX(), r.maxY(), 0.5f * pi},
    };
    for (const auto& k : corners) {
        // Slightly outside the corner so no seam shows at the edges.
        float ox = k.x < k.cx ? k.x - 1 : k.x + 1, oy = k.y < k.cy ? k.y - 1 : k.y + 1;
        mask.moveTo({ox, oy});
        for (int i = 0; i <= steps; i++) {
            float a = k.a0 + 0.5f * pi * float(i) / steps;
            mask.lineTo({k.cx + radius * std::cos(a), k.cy + radius * std::sin(a)});
        }
        mask.close();
    }
    fillPath(mask, outside);
}

void Canvas::drawBitmap(ID2D1Bitmap* bitmap, Rect dest, const Rect* src, bool smooth, float opacity) {
    if (!bitmap) return;
    D2D1_RECT_F s = src ? src->d2d() : D2D1_RECT_F{};
    rt_->DrawBitmap(bitmap, dest.d2d(), opacity,
                    smooth ? D2D1_BITMAP_INTERPOLATION_MODE_LINEAR : D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                    src ? &s : nullptr);
}

void Canvas::drawLayout(const TextLayout& layout, Point origin, Color color) {
    if (!layout.layout) return;
    rt_->DrawTextLayout(D2D1::Point2F(origin.x, origin.y), layout.layout.get(), brush(color),
                        layout.colorGlyphs ? D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT : D2D1_DRAW_TEXT_OPTIONS_NONE);
}

Rect Canvas::drawText(const std::string& s, Point origin, const Font& font, Color color) {
    const TextLayout& l = textLayout(s, font);
    drawLayout(l, origin, color);
    return {origin.x, origin.y, l.size.w, l.size.h};
}

Rect Canvas::drawText(const std::string& s, Point p, HAlign h, VAlign v, const Font& font, Color color) {
    const TextLayout& l = textLayout(s, font);
    Point o = p;
    if (h == HAlign::center) o.x -= l.size.w / 2;
    else if (h == HAlign::right) o.x -= l.size.w;
    if (v == VAlign::middle) o.y -= l.size.h / 2;
    else if (v == VAlign::bottom) o.y -= l.size.h;
    drawLayout(l, o, color);
    return {o.x, o.y, l.size.w, l.size.h};
}

void Canvas::drawParagraph(const RichParagraph& p, Point origin) {
    if (!p.layout_) return;
    DrawContext ctx{rt_, brush_.get()};
    if (p.source_.bullet) {
        const TextLayout& b = textLayout("\xE2\x80\xA2", p.bulletFont_);
        drawLayout(b, {origin.x + 2, origin.y + firstBaseline(p.layout_.get()) - b.baseline}, p.bulletColor_);
    }
    p.layout_->Draw(&ctx, &gRenderer, origin.x + p.textX_, origin.y);
}

Com<ID2D1Bitmap> makeBitmap(ID2D1RenderTarget* rt, const uint32_t* pixels, int width, int height) {
    Com<ID2D1Bitmap> bmp;
    float dx, dy;
    rt->GetDpi(&dx, &dy);
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
    rt->CreateBitmap(D2D1::SizeU(UINT32(width), UINT32(height)), pixels, UINT32(width * 4), props, bmp.put());
    return bmp;
}
