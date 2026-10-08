#pragma once
// Drawing primitives on Direct2D / DirectWrite: the stand-ins for CGContext, NSColor,
// NSFont and NSAttributedString. Coordinates are DIPs with y pointing down (like a
// flipped NSView).
#include <windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dwrite_1.h>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// MARK: - COM pointer

template <class T>
class Com {
public:
    Com() = default;
    Com(T* p) : p_(p) {}
    Com(const Com& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    Com(Com&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ~Com() { reset(); }
    Com& operator=(const Com& o) {
        if (this != &o) {
            reset();
            p_ = o.p_;
            if (p_) p_->AddRef();
        }
        return *this;
    }
    Com& operator=(Com&& o) noexcept {
        if (this != &o) {
            reset();
            p_ = o.p_;
            o.p_ = nullptr;
        }
        return *this;
    }
    void reset() {
        if (p_) p_->Release();
        p_ = nullptr;
    }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    T** put() {
        reset();
        return &p_;
    }
    explicit operator bool() const { return p_ != nullptr; }

private:
    T* p_ = nullptr;
};

// MARK: - Geometry

struct Point {
    float x = 0, y = 0;
};

struct Size {
    float w = 0, h = 0;
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float minX() const { return x; }
    float maxX() const { return x + w; }
    float midX() const { return x + w / 2; }
    float minY() const { return y; }
    float maxY() const { return y + h; }
    float midY() const { return y + h / 2; }
    bool contains(Point p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
    Rect insetBy(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    Rect offsetBy(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
    bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
    bool operator!=(const Rect& o) const { return !(*this == o); }
    D2D1_RECT_F d2d() const { return D2D1::RectF(x, y, x + w, y + h); }
};

// MARK: - Colour

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    static Color hex(uint32_t v, float alpha = 1) {
        return {float((v >> 16) & 0xFF) / 255, float((v >> 8) & 0xFF) / 255, float(v & 0xFF) / 255, alpha};
    }
    static Color white(float alpha = 1) { return {1, 1, 1, alpha}; }
    Color withAlpha(float alpha) const { return {r, g, b, alpha}; }
    /// NSColor.blended(withFraction:of:)
    Color blended(float f, const Color& o) const {
        return {r + (o.r - r) * f, g + (o.g - g) * f, b + (o.b - b) * f, a + (o.a - a) * f};
    }
    D2D1_COLOR_F d2d() const { return D2D1::ColorF(r, g, b, a); }
};

// MARK: - Fonts

enum class FontFamily { system, serif, math, mono };
enum class FontWeight { regular = 400, medium = 500, semibold = 600, bold = 700 };

struct Font {
    FontFamily family = FontFamily::system;
    float size = 12;
    FontWeight weight = FontWeight::regular;
    bool tabular = false;   // monospaced digits
    float kern = 0;         // extra spacing after each character
    bool operator==(const Font& o) const = default;
    Font withSize(float s) const {
        Font f = *this;
        f.size = s;
        return f;
    }
    Font withWeight(FontWeight w) const {
        Font f = *this;
        f.weight = w;
        return f;
    }
};

// MARK: - Shared factories

struct Graphics {
    static Graphics& shared();
    Com<ID2D1Factory> d2d;
    Com<ID2D1Factory1> d2d1;
    Com<IDWriteFactory> dwrite;
    IDWriteTextFormat* format(const Font& font);
    /// Line height of a font (ascent + descent + gap), in DIPs.
    float lineHeight(const Font& font);
    float ascent(const Font& font);
    ID2D1StrokeStyle* strokeStyle(D2D1_CAP_STYLE cap, D2D1_LINE_JOIN join, const std::vector<float>& dashes);

private:
    Graphics();
};

// MARK: - Text layouts

enum class TextMode { singleLine, truncate, wrap };

/// A cached single-run text layout.
struct TextLayout {
    Com<IDWriteTextLayout> layout;
    Size size;          // measured size (width includes trailing spaces)
    float baseline = 0; // first line baseline
    bool colorGlyphs = false;   // contains emoji (needs Direct2D's slower colour-font path)
};

/// Returns a cached layout. For truncate/wrap modes, `width` is the available width.
const TextLayout& textLayout(const std::string& text, const Font& font, TextMode mode = TextMode::singleLine,
                             float width = 100000, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);
Size measureText(const std::string& text, const Font& font);
/// Trims the layout cache; call between frames.
void textCacheMaintenance();
float wrappedHeight(const std::string& text, const Font& font, float width);

// MARK: - Rich text (attributed strings)

struct TextRun {
    std::string text;
    Font font;
    Color color;
    std::string link;   // "lab:..." for clickable runs
    int script = 0;     // +1 superscript, -1 subscript
};

struct Paragraph {
    std::vector<TextRun> runs;
    float spacingBefore = 0;
    float spacingAfter = 0;
    float indent = 0;        // left indent of every line
    float lineSpacing = 0;   // extra space between wrapped lines
    bool bullet = false;     // draws "•" at x = 2 and indents the text to 14
};

/// One laid-out paragraph of rich text.
class RichParagraph {
public:
    RichParagraph(const Paragraph& p, float width);
    ~RichParagraph();
    RichParagraph(const RichParagraph&) = delete;
    RichParagraph& operator=(const RichParagraph&) = delete;

    float height() const { return height_; }
    float textX() const { return textX_; }
    const std::wstring& text() const { return text_; }
    IDWriteTextLayout* layout() const { return layout_.get(); }
    /// Link under a point (relative to the paragraph's top-left), or "".
    std::string linkAt(Point p) const;
    /// Character position under a point (relative to the paragraph's top-left).
    UINT32 positionAt(Point p) const;
    /// Rectangles covering text positions [from, to).
    std::vector<Rect> selectionRects(UINT32 from, UINT32 to) const;

    const Paragraph& source() const { return source_; }

private:
    friend class Canvas;
    Paragraph source_;
    Com<IDWriteTextLayout> layout_;
    std::vector<IUnknown*> owned_;   // inline objects and colour effects
    std::wstring text_;
    struct LinkRange {
        UINT32 start, length;
        std::string link;
    };
    std::vector<LinkRange> links_;
    float height_ = 0;
    float textX_ = 0;
    Color bulletColor_;
    Font bulletFont_;
};

// MARK: - Paths

class Path {
public:
    void moveTo(Point p);
    void lineTo(Point p);
    void close();
    void addRect(Rect r);
    bool empty() const { return figures_.empty(); }
    Com<ID2D1PathGeometry> build() const;

private:
    struct Figure {
        std::vector<D2D1_POINT_2F> points;
        bool closed = false;
    };
    std::vector<Figure> figures_;
};

// MARK: - Canvas

enum class HAlign { left, center, right };
enum class VAlign { top, middle, bottom };

class Canvas {
public:
    /// deviceId identifies the resource domain (bitmaps must be recreated when it changes).
    Canvas(ID2D1RenderTarget* rt, int deviceId);
    ~Canvas();

    ID2D1RenderTarget* rt() const { return rt_; }
    int deviceId() const { return deviceId_; }

    void save();
    void restore();
    void translate(float dx, float dy);
    void clip(Rect r);
    void clipRoundedRect(Rect r, float radius);

    void fillRect(Rect r, Color c);
    void strokeRect(Rect r, Color c, float width = 1);
    void fillRoundedRect(Rect r, float radius, Color c);
    void strokeRoundedRect(Rect r, float radius, Color c, float width = 1);
    void fillEllipse(Rect r, Color c);
    void strokeEllipse(Rect r, Color c, float width = 1);
    void line(Point a, Point b, Color c, float width = 1, const std::vector<float>& dashes = {},
              D2D1_CAP_STYLE cap = D2D1_CAP_STYLE_FLAT);
    void strokePath(const Path& p, Color c, float width, D2D1_CAP_STYLE cap = D2D1_CAP_STYLE_FLAT,
                    D2D1_LINE_JOIN join = D2D1_LINE_JOIN_MITER, const std::vector<float>& dashes = {});
    /// Large fills whose top edge is covered by a stroke can skip anti-aliasing (much cheaper).
    void fillPath(const Path& p, Color c, bool antialias = true);
    /// Paints the four corners outside a rounded rectangle (a cheap stand-in for a rounded clip).
    void maskCorners(Rect r, float radius, Color outside);
    void drawBitmap(ID2D1Bitmap* bitmap, Rect dest, const Rect* src = nullptr, bool smooth = true, float opacity = 1);

    /// Draws text with its top-left at `origin`; returns its rect.
    Rect drawText(const std::string& s, Point origin, const Font& font, Color color);
    /// Positions text like PlotView.drawText (alignment relative to p).
    Rect drawText(const std::string& s, Point p, HAlign h, VAlign v, const Font& font, Color color);
    void drawLayout(const TextLayout& layout, Point origin, Color color);
    void drawParagraph(const RichParagraph& p, Point origin);

    ID2D1SolidColorBrush* brush(Color c);

private:
    ID2D1RenderTarget* rt_;
    int deviceId_;
    Com<ID2D1SolidColorBrush> brush_;
    struct State {
        D2D1_MATRIX_3X2_F transform;
        size_t clips;
    };
    std::vector<State> states_;
    struct ClipEntry {
        bool layer;
    };
    std::vector<ClipEntry> clips_;
};

/// Creates a bitmap from 32-bit BGRA pixels (alpha ignored).
Com<ID2D1Bitmap> makeBitmap(ID2D1RenderTarget* rt, const uint32_t* pixels, int width, int height);
