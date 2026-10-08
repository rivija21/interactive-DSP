#include "Controls.h"
#include "../platform/Dispatch.h"
#include "../util/Format.h"
#include "../util/Text.h"
#include <algorithm>
#include <cmath>

namespace {

float controlHeight(ControlSize s) { return s == ControlSize::smallSize ? 20.0f : 22.0f; }

/// The white, softly shadowed bezel of push buttons and pop-ups.
void drawBezel(Canvas& c, Rect r, float radius, Color fill) {
    c.fillRoundedRect(r.offsetBy(0, 0.6f), radius, ControlColors::bezelShadow());
    c.fillRoundedRect(r, radius, fill);
    c.strokeRoundedRect(r.insetBy(0.25f, 0.25f), radius, ControlColors::bezelBorder(), 0.5f);
}

void drawChevron(Canvas& c, Point center, float w, float h, bool up, Color color, float width) {
    Path p;
    float dy = up ? h / 2 : -h / 2;
    p.moveTo({center.x - w / 2, center.y + dy});
    p.lineTo({center.x, center.y - dy});
    p.lineTo({center.x + w / 2, center.y + dy});
    c.strokePath(p, color, width, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
}

} // namespace

// MARK: - Label

Label::Label(std::string text, Font font, Color color) : text_(std::move(text)), font_(font), color_(color) {}

void Label::setText(const std::string& t) {
    if (t == text_) return;
    text_ = t;
    setNeedsDisplay();
}

void Label::setColor(Color c) {
    color_ = c;
    setNeedsDisplay();
}

void Label::setFont(Font f) {
    font_ = f;
    setNeedsDisplay();
}

Size Label::intrinsicSize() {
    Size s = measureText(text_, font_);
    s.w += 4;
    s.h = std::max(s.h, Graphics::shared().lineHeight(font_));
    if (maxWidth > 0) s.w = std::min(s.w, maxWidth);
    return s;
}

void Label::draw(Canvas& c) {
    if (text_.empty()) return;
    const TextLayout& l = textLayout(text_, font_, TextMode::truncate, std::max(1.0f, frame.w - 4), alignment);
    float y = std::round((frame.h - l.size.h) / 2);
    c.drawLayout(l, {2, y}, color_);
}

// MARK: - Wrapping label

WrappingLabel::WrappingLabel(std::string text, Font font, Color color) : text_(std::move(text)), font_(font), color_(color) {}

void WrappingLabel::setText(const std::string& t) {
    if (t == text_) return;
    text_ = t;
    if (parent) parent->setNeedsLayout();
    setNeedsDisplay();
}

void WrappingLabel::setColor(Color c) {
    color_ = c;
    setNeedsDisplay();
}

float WrappingLabel::heightForWidth(float w) {
    if (text_.empty()) return 0;
    return std::ceil(wrappedHeight(text_, font_, std::max(1.0f, w - 4)));
}

void WrappingLabel::draw(Canvas& c) {
    if (text_.empty()) return;
    c.drawLayout(textLayout(text_, font_, TextMode::wrap, std::max(1.0f, frame.w - 4)), {2, 0}, color_);
}

// MARK: - Rich label

void RichLabel::setParagraphs(std::vector<Paragraph> p) {
    paragraphs_ = std::move(p);
    laidWidth_ = -1;
    setNeedsDisplay();
}

void RichLabel::rebuild(float w) {
    if (w == laidWidth_) return;
    laid_.clear();
    for (const auto& p : paragraphs_) laid_.push_back(std::make_unique<RichParagraph>(p, w - 4));
    laidWidth_ = w;
}

float RichLabel::heightForWidth(float w) {
    rebuild(w);
    float h = 0;
    for (size_t i = 0; i < laid_.size(); i++) {
        h += laid_[i]->source().spacingBefore + laid_[i]->height();
        if (i + 1 < laid_.size()) h += laid_[i]->source().spacingAfter;
    }
    return std::ceil(h);
}

void RichLabel::draw(Canvas& c) {
    rebuild(frame.w);
    float y = 0;
    for (const auto& p : laid_) {
        y += p->source().spacingBefore;
        c.drawParagraph(*p, {2, y});
        y += p->height() + p->source().spacingAfter;
    }
}

// MARK: - Segmented control

SegmentedControl::SegmentedControl(std::vector<std::string> titles, ControlSize size)
    : titles_(std::move(titles)), widths_(titles_.size(), 0.0f), size_(size),
      font_(Fonts::system(size == ControlSize::smallSize ? 11.0f : 12.0f)) {
    fixedHeight = controlHeight(size);
}

void SegmentedControl::setSelected(int i) {
    if (i == selected_) return;
    selected_ = i;
    setNeedsDisplay();
}

void SegmentedControl::setSegmentWidth(int i, float w) { widths_[size_t(i)] = w; }

Size SegmentedControl::intrinsicSize() {
    float pad = size_ == ControlSize::smallSize ? 9.0f : 11.0f;
    float w = 0;
    for (size_t i = 0; i < titles_.size(); i++)
        w += widths_[i] > 0 ? widths_[i] : std::ceil(measureText(titles_[i], font_).w) + 2 * pad;
    return {w, controlHeight(size_)};
}

std::vector<Rect> SegmentedControl::segmentRects() {
    std::vector<Rect> rects;
    size_t n = titles_.size();
    if (n == 0) return rects;
    float pad = size_ == ControlSize::smallSize ? 9.0f : 11.0f;
    std::vector<float> natural(n);
    float total = 0;
    for (size_t i = 0; i < n; i++) {
        natural[i] = fillEqually ? 1.0f : (widths_[i] > 0 ? widths_[i] : std::ceil(measureText(titles_[i], font_).w) + 2 * pad);
        total += natural[i];
    }
    float x = 0;
    for (size_t i = 0; i < n; i++) {
        float w = natural[i] / total * frame.w;
        rects.push_back({x, 0, w, frame.h});
        x += w;
    }
    return rects;
}

int SegmentedControl::segmentAt(Point p) {
    auto rects = segmentRects();
    for (size_t i = 0; i < rects.size(); i++)
        if (p.x >= rects[i].minX() && p.x < rects[i].maxX() && p.y >= 0 && p.y < frame.h) return int(i);
    return -1;
}

void SegmentedControl::draw(Canvas& c) {
    Rect b = bounds();
    float radius = size_ == ControlSize::smallSize ? 5.0f : 6.0f;
    c.fillRoundedRect(b, radius, Color{0, 0, 0, 0.075f});
    auto rects = segmentRects();
    // Separators between unselected neighbours.
    for (size_t i = 1; i < rects.size(); i++) {
        if (int(i) == selected_ || int(i) - 1 == selected_) continue;
        float x = std::round(rects[i].minX()) + 0.5f;
        c.line({x, b.h * 0.27f}, {x, b.h * 0.73f}, Color{0, 0, 0, 0.14f}, 1);
    }
    for (size_t i = 0; i < rects.size(); i++) {
        Rect r = rects[i];
        if (int(i) == selected_) {
            Rect pill = r.insetBy(1.5f, 1.5f);
            c.fillRoundedRect(pill.offsetBy(0, 0.6f), radius - 1.5f, Color{0, 0, 0, 0.14f});
            c.fillRoundedRect(pill, radius - 1.5f, Color::white());
            c.strokeRoundedRect(pill.insetBy(0.25f, 0.25f), radius - 1.5f, Color{0, 0, 0, 0.06f}, 0.5f);
        } else if (int(i) == pressed_ && pressedInside_) {
            c.fillRoundedRect(r.insetBy(1.5f, 1.5f), radius - 1.5f, Color{0, 0, 0, 0.07f});
        }
        const TextLayout& l = textLayout(titles_[i], font_, TextMode::truncate, std::max(1.0f, r.w - 8), DWRITE_TEXT_ALIGNMENT_CENTER);
        float y = std::round((b.h - l.size.h) / 2);
        c.drawLayout(l, {r.minX() + 4, y}, ControlColors::label().withAlpha(enabled() ? 0.85f : 0.4f));
    }
}

void SegmentedControl::mouseDown(const MouseEvent& e) {
    if (!enabled()) return;
    pressed_ = segmentAt(e.location);
    pressedInside_ = pressed_ >= 0;
    setNeedsDisplay();
}

void SegmentedControl::mouseDragged(const MouseEvent& e) {
    bool inside = segmentAt(e.location) == pressed_;
    if (inside != pressedInside_) {
        pressedInside_ = inside;
        setNeedsDisplay();
    }
}

void SegmentedControl::mouseUp(const MouseEvent& e) {
    int seg = segmentAt(e.location);
    int p = pressed_;
    pressed_ = -1;
    setNeedsDisplay();
    if (p >= 0 && seg == p) {
        selected_ = seg;
        if (onChange) onChange(seg);
    }
}

// MARK: - Popup button

PopupButton::PopupButton(std::vector<Item> items, ControlSize size, float fontSize)
    : items_(std::move(items)), size_(size),
      font_(Fonts::system(fontSize > 0 ? fontSize : (size == ControlSize::smallSize ? 11.0f : 12.0f))) {
    fixedHeight = controlHeight(size);
}

std::vector<PopupButton::Item> PopupButton::titles(const std::vector<std::string>& t) {
    std::vector<Item> items;
    for (const auto& s : t) items.push_back({s});
    return items;
}

void PopupButton::selectItem(int i) {
    if (i == selected_) return;
    selected_ = i;
    setNeedsDisplay();
}

Size PopupButton::intrinsicSize() {
    float w = 0;
    bool icons = false;
    for (const auto& it : items_) {
        w = std::max(w, measureText(it.title, font_).w);
        icons = icons || it.icon != Icon::none;
    }
    if (pullsDown) w = std::max(w, measureText(pullDownTitle, font_).w);
    float h = controlHeight(size_);
    return {std::ceil(w) + 10 + (icons ? 20 : 0) + h + 2, h};
}

void PopupButton::draw(Canvas& c) {
    Rect b = bounds();
    float radius = 5;
    drawBezel(c, b.insetBy(0.5f, 0.5f), radius, open_ ? Color::hex(0xEDEDED) : Color::white());
    float alpha = enabled() ? 1.0f : 0.45f;
    float x = 8;
    const Item* item = (!pullsDown && selected_ >= 0 && selected_ < int(items_.size())) ? &items_[size_t(selected_)] : nullptr;
    if (item && item->icon != Icon::none) {
        drawIcon(c, item->icon, {x, (b.h - 14) / 2, 14, 14}, ControlColors::label().withAlpha(0.8f * alpha));
        x += 20;
    }
    std::string title = pullsDown ? pullDownTitle : (item ? item->title : "");
    float ind = b.h - 6;
    const TextLayout& l = textLayout(title, font_, TextMode::truncate, std::max(1.0f, b.w - x - ind - 8));
    c.drawLayout(l, {x, std::round((b.h - l.size.h) / 2)}, ControlColors::label().withAlpha(0.85f * alpha));
    // The blue indicator with chevrons.
    Rect box{b.w - ind - 3, 3, ind, ind};
    c.fillRoundedRect(box, 4, enabled() ? ControlColors::accent() : Color{0, 0, 0, 0.18f});
    Point mid{box.midX(), box.midY()};
    float cw = ind * 0.36f;
    if (pullsDown) {
        drawChevron(c, mid, cw, ind * 0.2f, false, Color::white(), 1.4f);
    } else {
        drawChevron(c, {mid.x, mid.y - ind * 0.17f}, cw, ind * 0.15f, true, Color::white(), 1.3f);
        drawChevron(c, {mid.x, mid.y + ind * 0.17f}, cw, ind * 0.15f, false, Color::white(), 1.3f);
    }
}

void PopupButton::mouseDown(const MouseEvent&) {
    if (!enabled()) return;
    std::vector<MenuEntry> entries;
    std::vector<int> map;
    for (size_t i = 0; i < items_.size(); i++) {
        entries.push_back({items_[i].title, !pullsDown && int(i) == selected_});
        map.push_back(int(i));
        if (items_[i].separatorAfter) {
            entries.push_back({"", false, true});
            map.push_back(-1);
        }
    }
    open_ = true;
    setNeedsDisplay();
    Host::shared().render();
    int chosen = showPopupMenu(entries, toWindow({0, frame.h + 1}));
    open_ = false;
    setNeedsDisplay();
    if (chosen >= 0 && map[size_t(chosen)] >= 0) {
        int index = map[size_t(chosen)];
        if (!pullsDown) selected_ = index;
        if (onSelect) onSelect(index);
    }
}

int showPopupMenu(const std::vector<MenuEntry>& entries, Point windowPoint) {
    Host& host = Host::shared();
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < entries.size(); i++) {
        const auto& e = entries[i];
        if (e.separator) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        } else {
            UINT flags = MF_STRING | (e.checked ? MF_CHECKED : 0) | (e.enabled ? 0 : MF_GRAYED);
            AppendMenuW(menu, flags, UINT_PTR(i + 1), widen(e.title).c_str());
        }
    }
    POINT pt = {LONG(std::lround(windowPoint.x * host.scale)), LONG(std::lround(windowPoint.y * host.scale))};
    ClientToScreen(host.hwnd, &pt);
    if (GetCapture() == host.hwnd) ReleaseCapture();
    host.captureLost();
    int cmd = int(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x,
                                   pt.y, host.hwnd, nullptr));
    DestroyMenu(menu);
    return cmd > 0 ? cmd - 1 : -1;
}

// MARK: - Checkbox

Checkbox::Checkbox(std::string title) : title_(std::move(title)) { fixedHeight = 16; }

void Checkbox::setChecked(bool v) {
    if (v == checked_) return;
    checked_ = v;
    setNeedsDisplay();
}

Size Checkbox::intrinsicSize() { return {16 + std::ceil(measureText(title_, Fonts::smallText()).w) + 2, 16}; }

void Checkbox::draw(Canvas& c) {
    Rect box{0.5f, std::round((frame.h - 12) / 2) + 0.5f, 12, 12};
    if (checked_) {
        c.fillRoundedRect(box, 3, pressed_ ? Color::hex(0x0062CC) : ControlColors::accent());
        Path check;
        check.moveTo({box.x + 3.0f, box.y + 6.3f});
        check.lineTo({box.x + 5.2f, box.y + 8.6f});
        check.lineTo({box.x + 9.2f, box.y + 3.6f});
        c.strokePath(check, Color::white(), 1.6f, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    } else {
        c.fillRoundedRect(box.offsetBy(0, 0.5f), 3, Color{0, 0, 0, 0.08f});
        c.fillRoundedRect(box, 3, pressed_ ? Color::hex(0xE4E4E4) : Color::white());
        c.strokeRoundedRect(box, 3, Color{0, 0, 0, 0.28f}, 0.6f);
    }
    const TextLayout& l = textLayout(title_, Fonts::smallText());
    c.drawLayout(l, {16, std::round((frame.h - l.size.h) / 2)}, ControlColors::label());
}

void Checkbox::mouseDown(const MouseEvent&) {
    pressed_ = true;
    setNeedsDisplay();
}

void Checkbox::mouseDragged(const MouseEvent& e) {
    bool inside = bounds().contains(e.location);
    if (inside != pressed_) {
        pressed_ = inside;
        setNeedsDisplay();
    }
}

void Checkbox::mouseUp(const MouseEvent& e) {
    pressed_ = false;
    setNeedsDisplay();
    if (bounds().contains(e.location)) {
        checked_ = !checked_;
        if (onToggle) onToggle(checked_);
    }
}

// MARK: - Push button

PushButton::PushButton(std::string title, ControlSize size)
    : title_(std::move(title)), size_(size), font_(Fonts::system(size == ControlSize::smallSize ? 11.0f : 13.0f)) {
    fixedHeight = controlHeight(size);
}

void PushButton::setTitle(const std::string& t) {
    title_ = t;
    setNeedsDisplay();
}

void PushButton::setIcon(Icon i) {
    icon_ = i;
    setNeedsDisplay();
}

Size PushButton::intrinsicSize() {
    float pad = size_ == ControlSize::smallSize ? 10.0f : 14.0f;
    float w = std::ceil(measureText(title_, font_).w) + 2 * pad + (icon_ != Icon::none ? 18.0f : 0.0f);
    return {std::max(w, size_ == ControlSize::smallSize ? 40.0f : 60.0f), controlHeight(size_)};
}

void PushButton::draw(Canvas& c) {
    Rect b = bounds().insetBy(0.5f, 0.5f);
    Color fill = Color::white();
    if (pressed_ && inside_) fill = Color::hex(0xE3E3E3);
    else if (toggles && on) fill = Color::hex(0xE9E9E9);
    drawBezel(c, b, 5, fill);
    float alpha = enabled() ? 1.0f : 0.4f;
    Color content = tint ? *tint : ControlColors::label();
    content = content.withAlpha(content.a * alpha);
    const TextLayout& l = textLayout(title_, font_);
    float iconW = icon_ != Icon::none ? 18.0f : 0.0f;
    float total = l.size.w + iconW;
    float x = std::round((frame.w - total) / 2);
    if (icon_ != Icon::none) {
        drawIcon(c, icon_, {x, (frame.h - 14) / 2, 14, 14}, content);
        x += iconW;
    }
    c.drawLayout(l, {x, std::round((frame.h - l.size.h) / 2)}, content);
}

void PushButton::mouseDown(const MouseEvent&) {
    if (!enabled()) return;
    pressed_ = true;
    inside_ = true;
    setNeedsDisplay();
}

void PushButton::mouseDragged(const MouseEvent& e) {
    if (!pressed_) return;
    bool inside = bounds().contains(e.location);
    if (inside != inside_) {
        inside_ = inside;
        setNeedsDisplay();
    }
}

void PushButton::mouseUp(const MouseEvent& e) {
    if (!pressed_) return;
    pressed_ = false;
    setNeedsDisplay();
    if (bounds().contains(e.location) && onClick) onClick();
}

// MARK: - Icon button

IconButton::IconButton(Icon icon, std::string tip, Color t) : tint(t), icon_(icon) {
    tooltip = std::move(tip);
    fixedWidth = 22;
    fixedHeight = 20;
}

void IconButton::setIcon(Icon i) {
    if (i == icon_) return;
    icon_ = i;
    setNeedsDisplay();
}

Size IconButton::intrinsicSize() { return {fixedWidth, fixedHeight}; }

void IconButton::draw(Canvas& c) {
    Color col = tint;
    if (pressed_ && inside_) col = col.withAlpha(col.a * 0.45f);
    if (!enabled()) col = col.withAlpha(col.a * 0.4f);
    float s = iconSize;
    drawIcon(c, icon_, {(frame.w - s) / 2, (frame.h - s) / 2, s, s}, col);
}

void IconButton::mouseDown(const MouseEvent&) {
    if (!enabled()) return;
    pressed_ = true;
    inside_ = true;
    setNeedsDisplay();
}

void IconButton::mouseDragged(const MouseEvent& e) {
    if (!pressed_) return;
    bool inside = bounds().contains(e.location);
    if (inside != inside_) {
        inside_ = inside;
        setNeedsDisplay();
    }
}

void IconButton::mouseUp(const MouseEvent& e) {
    if (!pressed_) return;
    pressed_ = false;
    setNeedsDisplay();
    if (bounds().contains(e.location) && onClick) onClick();
}

// MARK: - Slider

static constexpr float kKnob = 14;

Slider::Slider() { fixedHeight = 17; }

void Slider::setValue(double v) {
    v = std::max(0.0, std::min(1.0, v));
    if (v == value_) return;
    value_ = v;
    setNeedsDisplay();
}

float Slider::knobX() const { return kKnob / 2 + float(value_) * (frame.w - kKnob); }

double Slider::valueAt(float x) const {
    double t = double(x - kKnob / 2) / double(std::max(1.0f, frame.w - kKnob));
    return std::max(0.0, std::min(1.0, t));
}

void Slider::draw(Canvas& c) {
    float alpha = enabled() ? 1.0f : 0.45f;
    float mid = std::round(frame.h / 2);
    Rect track{kKnob / 2 - 2, mid - 2, frame.w - kKnob + 4, 4};
    c.fillRoundedRect(track, 2, Color{0, 0, 0, 0.12f * alpha});
    float kx = knobX();
    Rect filled{track.x, track.y, kx - track.x, 4};
    if (filled.w > 0) c.fillRoundedRect(filled, 2, enabled() ? ControlColors::accent() : Color{0, 0, 0, 0.18f});
    Rect knob{kx - kKnob / 2, mid - kKnob / 2, kKnob, kKnob};
    c.fillEllipse(knob.offsetBy(0, 0.7f), Color{0, 0, 0, 0.16f * alpha});
    c.fillEllipse(knob, dragging_ ? Color::hex(0xF2F2F2) : Color::white());
    c.strokeEllipse(knob.insetBy(0.25f, 0.25f), Color{0, 0, 0, 0.18f * alpha}, 0.5f);
}

void Slider::mouseDown(const MouseEvent& e) {
    if (!enabled()) return;
    dragging_ = true;
    float kx = knobX();
    if (std::fabs(e.location.x - kx) <= kKnob / 2 + 1) {
        grabOffset_ = e.location.x - kx;
    } else {
        grabOffset_ = 0;
        value_ = valueAt(e.location.x);
        if (onChange) onChange(value_);
    }
    setNeedsDisplay();
}

void Slider::mouseDragged(const MouseEvent& e) {
    if (!dragging_) return;
    double v = valueAt(e.location.x - grabOffset_);
    if (v != value_) {
        value_ = v;
        setNeedsDisplay();
        if (onChange) onChange(value_);
    }
}

void Slider::mouseUp(const MouseEvent&) {
    dragging_ = false;
    setNeedsDisplay();
}

// MARK: - Value field

ValueField::ValueField() { tooltip = "Click to type a value"; }

void ValueField::setText(const std::string& t) {
    if (t == text_) return;
    text_ = t;
    setNeedsDisplay();
}

Size ValueField::intrinsicSize() { return {96, Graphics::shared().lineHeight(font)}; }

void ValueField::draw(Canvas& c) {
    if (editing_) return;
    const TextLayout& l = textLayout(text_, font, TextMode::truncate, std::max(1.0f, frame.w - 4), DWRITE_TEXT_ALIGNMENT_TRAILING);
    c.drawLayout(l, {2, std::round((frame.h - l.size.h) / 2)}, color);
}

void ValueField::mouseDown(const MouseEvent&) {
    Rect r = windowRect();
    Host::EditSession s;
    s.commit = [this](const std::string& t) {
        editing_ = false;
        if (onCommit) onCommit(t);
    };
    s.cancel = [this] {
        editing_ = false;
        if (onCancel) onCancel();
    };
    editing_ = true;
    Host::shared().beginEditing(this, r.insetBy(0, -1), text_, font, color, true, std::move(s));
}

// MARK: - Slider row

SliderRow::SliderRow(std::string title, double min, double max, Scale scale, std::function<std::string(double)> format,
                     std::function<std::optional<double>(const std::string&)> parse)
    : title_(std::move(title), Fonts::label(), Theme::app().text), scale_(scale), minValue_(min), maxLimit_(max),
      format_(std::move(format)), parse_(std::move(parse)) {
    if (!parse_) parse_ = [](const std::string& s) { return parseDouble(trim(s)); };
    addChild(&title_);
    addChild(&field_);
    addChild(&slider_);
    slider_.onChange = [this](double t) { sliderMoved(t); };
    field_.onCommit = [this](const std::string& t) { fieldEdited(t); };
    field_.onCancel = [this] { field_.setText(format_(value_)); };
}

void SliderRow::setTitle(const std::string& t) { title_.setText(t); }

double SliderRow::toSlider(double v) const {
    if (scale_ == Scale::log) return std::log(v / minValue_) / std::log(maxLimit_ / minValue_);
    return (v - minValue_) / (maxLimit_ - minValue_);
}

double SliderRow::fromSlider(double t) const {
    switch (scale_) {
    case Scale::log: return minValue_ * std::pow(maxLimit_ / minValue_, t);
    case Scale::linear: return minValue_ + t * (maxLimit_ - minValue_);
    case Scale::integer: return std::round(minValue_ + t * (maxLimit_ - minValue_));
    }
    return t;
}

void SliderRow::setValue(double v) {
    value_ = v;
    slider_.setValue(std::max(0.0, std::min(1.0, toSlider(v))));
    if (!field_.editing()) field_.setText(format_(v));
}

void SliderRow::setRange(double newMax) {
    if (std::fabs(maxLimit_ - newMax) > 1e-9) maxLimit_ = newMax;
}

void SliderRow::sliderMoved(double t) {
    double v = fromSlider(t);
    if (v == value_) return;
    value_ = v;
    field_.setText(format_(v));
    if (onChange) onChange(v);
}

void SliderRow::fieldEdited(const std::string& text) {
    if (auto v = parse_(text)) {
        double c = std::max(minValue_, std::min(maxLimit_, scale_ == Scale::integer ? std::round(*v) : *v));
        value_ = c;
        slider_.setValue(toSlider(c));
        if (onChange) onChange(c);
    }
    field_.setText(format_(value_));
}

float SliderRow::heightForWidth(float) {
    float th = std::ceil(Graphics::shared().lineHeight(Fonts::label()));
    return th + 3 + slider_.fixedHeight;
}

void SliderRow::layout() {
    auto& g = Graphics::shared();
    float th = std::ceil(g.lineHeight(Fonts::label()));
    float fieldW = 96;
    title_.setFrame({0, 0, std::max(10.0f, frame.w - fieldW - 4), th});
    // Value baseline lines up with the title's baseline.
    float fh = std::ceil(g.lineHeight(field_.font));
    float fy = std::round(g.ascent(Fonts::label()) - g.ascent(field_.font) + (th - g.lineHeight(Fonts::label())) / 2);
    field_.setFrame({frame.w - fieldW, fy, fieldW, fh});
    slider_.setFrame({-1, th + 3, frame.w + 2, slider_.fixedHeight});
}

// MARK: - Form row

FormRow::FormRow(std::string title, Widget* control, float labelWidth)
    : label_(std::move(title), Fonts::label(), Theme::app().text), control_(control), labelWidth_(labelWidth) {
    addChild(&label_);
    addChild(control_);
}

float FormRow::heightForWidth(float) { return control_->fixedHeight > 0 ? control_->fixedHeight : control_->intrinsicSize().h; }

void FormRow::layout() {
    float h = frame.h;
    float lh = std::ceil(Graphics::shared().lineHeight(Fonts::label()));
    label_.setFrame({0, std::round((h - lh) / 2), labelWidth_, lh});
    control_->setFrame({labelWidth_ + 4, 0, std::max(10.0f, frame.w - labelWidth_ - 4), h});
}

// MARK: - Stack view

float StackView::heightForWidth(float w) {
    if (orientation == Orientation::horizontal) return intrinsicSize().h;
    float contentW = w - insetLeft - insetRight;
    float y = insetTop;
    bool any = false;
    for (auto* c : children) {
        if (c->hidden()) continue;
        float cw = c->fixedWidth > 0 ? std::min(c->fixedWidth, contentW) : contentW;
        float h = c->fixedHeight > 0 ? c->fixedHeight : c->heightForWidth(cw);
        y += h + spacing;
        any = true;
    }
    if (any) y -= spacing;
    return std::ceil(y + insetBottom);
}

Size StackView::intrinsicSize() {
    float w = insetLeft + insetRight, h = 0;
    bool any = false;
    for (auto* c : children) {
        if (c->hidden()) continue;
        Size s = c->intrinsicSize();
        float cw = c->fixedWidth > 0 ? c->fixedWidth : s.w;
        float ch = c->fixedHeight > 0 ? c->fixedHeight : s.h;
        w += cw + spacing;
        h = std::max(h, ch);
        any = true;
    }
    if (any) w -= spacing;
    return {w, h + insetTop + insetBottom};
}

void StackView::layout() {
    if (orientation == Orientation::vertical) {
        float contentW = frame.w - insetLeft - insetRight;
        float y = insetTop;
        for (auto* c : children) {
            if (c->hidden()) continue;
            float cw = c->fixedWidth > 0 ? std::min(c->fixedWidth, contentW) : contentW;
            float h = c->fixedHeight > 0 ? c->fixedHeight : c->heightForWidth(cw);
            c->setFrame({insetLeft, y, cw, h});
            c->layout();
            y += h + spacing;
        }
        return;
    }
    float overflow = availableWidth > 0 ? std::max(0.0f, intrinsicSize().w - availableWidth) : 0;
    float x = insetLeft;
    for (auto* c : children) {
        if (c->hidden()) continue;
        Size s = c->intrinsicSize();
        float cw = c->fixedWidth > 0 ? c->fixedWidth : s.w;
        float ch = c->fixedHeight > 0 ? c->fixedHeight : s.h;
        if (c == compressible && overflow > 0) cw = std::max(40.0f, cw - overflow);
        c->setFrame({x, std::round((frame.h - ch) / 2), cw, ch});
        c->layout();
        x += cw + spacing;
    }
}

// MARK: - Scroll view

ScrollView::ScrollView(Widget* doc) : document(doc) { addChild(doc); }

void ScrollView::documentChanged() {
    layout();
    setNeedsDisplay();
}

void ScrollView::layout() {
    contentHeight_ = document->heightForWidth(frame.w);
    float maxOffset = std::max(0.0f, contentHeight_ - frame.h);
    offset_ = std::max(0.0f, std::min(offset_, maxOffset));
    document->setFrame({0, -offset_, frame.w, std::max(contentHeight_, frame.h)});
    document->layout();
}

void ScrollView::setOffset(float y) {
    float maxOffset = std::max(0.0f, contentHeight_ - frame.h);
    float o = std::max(0.0f, std::min(y, maxOffset));
    if (o == offset_) return;
    offset_ = o;
    document->setFrame({0, -offset_, frame.w, std::max(contentHeight_, frame.h)});
    lastActivity_ = mediaTime();
    setNeedsDisplay();
}

bool ScrollView::scrollWheel(const ScrollEvent& e) {
    if (contentHeight_ <= frame.h) return false;
    setOffset(offset_ - e.deltaY * 48);
    lastActivity_ = mediaTime();
    return true;
}

Rect ScrollView::knobRect() const {
    float h = frame.h;
    float knobH = std::max(24.0f, h * h / std::max(contentHeight_, 1.0f));
    float maxOffset = std::max(1.0f, contentHeight_ - h);
    float y = 2 + (offset_ / maxOffset) * (h - 4 - knobH);
    float w = (nearScroller_ || dragging_) ? 9.0f : 6.0f;
    return {frame.w - w - 3, y, w, knobH};
}

void ScrollView::drawOverChildren(Canvas& c) {
    if (contentHeight_ <= frame.h + 0.5f) return;
    double age = mediaTime() - lastActivity_;
    float alpha = (dragging_ || nearScroller_) ? 1.0f : float(std::max(0.0, std::min(1.0, (1.4 - age) / 0.4)));
    if (alpha <= 0) return;
    Rect k = knobRect();
    c.fillRoundedRect(k, k.w / 2, Color{0, 0, 0, 0.38f * alpha});
}

Widget* ScrollView::hitTest(Point p) {
    if (hidden() || !frame.contains(p)) return nullptr;
    Point local{p.x - frame.x, p.y - frame.y};
    if (contentHeight_ > frame.h && local.x >= frame.w - 12) return this;
    return Widget::hitTest(p);
}

void ScrollView::mouseDown(const MouseEvent& e) {
    Rect k = knobRect();
    if (e.location.y >= k.minY() && e.location.y <= k.maxY()) {
        dragging_ = true;
        dragStartY_ = e.location.y;
        dragStartOffset_ = offset_;
    } else {
        setOffset(offset_ + (e.location.y < k.minY() ? -frame.h : frame.h) * 0.9f);
    }
    setNeedsDisplay();
}

void ScrollView::mouseDragged(const MouseEvent& e) {
    if (!dragging_) return;
    Rect k = knobRect();
    float track = std::max(1.0f, frame.h - 4 - k.h);
    float maxOffset = std::max(0.0f, contentHeight_ - frame.h);
    setOffset(dragStartOffset_ + (e.location.y - dragStartY_) / track * maxOffset);
}

void ScrollView::mouseUp(const MouseEvent&) {
    dragging_ = false;
    lastActivity_ = mediaTime();
    setNeedsDisplay();
}

void ScrollView::mouseMoved(const MouseEvent& e) {
    bool isNear = e.location.x >= frame.w - 12;
    if (isNear != nearScroller_) {
        nearScroller_ = isNear;
        setNeedsDisplay();
    }
}

void ScrollView::mouseExited() {
    if (nearScroller_) {
        nearScroller_ = false;
        lastActivity_ = mediaTime();
        setNeedsDisplay();
    }
}

void ScrollView::tick() {
    double age = mediaTime() - lastActivity_;
    if (age < 1.5 && contentHeight_ > frame.h) setNeedsDisplay();
}

// MARK: - Panel

Panel::Panel(std::string title) : title_(uppercased(title), Fonts::header(), Theme::app().secondaryText) {
    addChild(&title_);
    addChild(&controls);
    addChild(&body);
    body.frame = {0, 33, 10, 10};
}

void Panel::setBody(Widget* w) {
    content_ = w;
    body.children.clear();
    body.addChild(w);
    layout();
}

float Panel::headerWidth() {
    float saved = controls.availableWidth;
    controls.availableWidth = 0;
    float w = 10 + title_.intrinsicSize().w + 8 + controls.intrinsicSize().w + 8;
    controls.availableWidth = saved;
    return std::ceil(w);
}

void Panel::layout() {
    Size ts = title_.intrinsicSize();
    title_.setFrame({10, std::round((32 - ts.h) / 2), ts.w, ts.h});
    float available = frame.w - 8 - (title_.frame.maxX() + 8);
    controls.availableWidth = std::max(0.0f, available);
    Size cs = controls.intrinsicSize();
    float cw = std::min(cs.w, std::max(0.0f, available));
    controls.setFrame({frame.w - 8 - cw, std::round((32 - cs.h) / 2), cw, cs.h});
    controls.layout();
    body.setFrame({0, 33, frame.w, std::max(0.0f, frame.h - 33)});
    if (content_) {
        content_->setFrame(body.bounds());
        content_->layout();
    }
}

void Panel::draw(Canvas& c) {
    Rect b = bounds();
    c.fillRoundedRect(b, 12, Theme::app().panel);
}

void Panel::drawOverChildren(Canvas& c) {
    Rect b = bounds();
    // Content is clipped to the rounded shape by painting the window background over the corners.
    c.maskCorners(b, 12, Host::shared().background);
    c.fillRect({0, 32, b.w, 1}, Theme::app().panelBorder);
    c.strokeRoundedRect(b.insetBy(0.5f, 0.5f), 11.5f, Theme::app().panelBorder, 1);
}

void Panel::drawAfterOverlays(Canvas& c) {
    // Live content drawn over the cached panel needs the same rounded clip. Only the bottom
    // corners can be touched (the header covers the top ones).
    bool any = false;
    std::function<void(Widget*)> find = [&](Widget* w) {
        for (auto* ch : w->children) {
            if (ch->hidden()) continue;
            if (ch->liveOverlay) any = true;
            else find(ch);
        }
    };
    find(&body);
    if (!any) return;
    Rect b = bounds();
    c.save();
    c.clip({0, b.h - 14, b.w, 14});
    c.maskCorners(b, 12, Host::shared().background);
    c.strokeRoundedRect(b.insetBy(0.5f, 0.5f), 11.5f, Theme::app().panelBorder, 1);
    c.restore();
}

// MARK: - Level meter

void LevelMeter::update(double input, double output) {
    double v[2] = {input, output};
    for (int i = 0; i < 2; i++) {
        double db = 20 * std::log10(std::max(v[i], 1e-6));
        double t = std::max(0.0, std::min(1.0, (db + 60) / 60));
        levels_[i] = std::max(t, levels_[i] * 0.86);
        holds_[i] = std::max(levels_[i], holds_[i] - 0.006);
    }
    setNeedsDisplay();
}

void LevelMeter::draw(Canvas& c) {
    const Theme& theme = Theme::app();
    const char* names[] = {"IN", "OUT"};
    Font f = Fonts::system(8.5f, FontWeight::bold);
    for (int i = 0; i < 2; i++) {
        float y = float(i) * 13 + 1;
        const TextLayout& l = textLayout(names[i], f);
        c.drawLayout(l, {0, y + 11 - l.size.h}, theme.secondaryText);
        Rect bar{26, y + 2, frame.w - 26, 7};
        c.fillRoundedRect(bar, 2, Color::hex(0xEDE6D8));
        float w = bar.w * float(levels_[i]);
        if (w > 0.5f) c.fillRoundedRect({bar.x, bar.y, w, bar.h}, 2, i == 0 ? theme.input : theme.output);
        float hx = bar.x + bar.w * float(holds_[i]);
        c.fillRect({hx - 1, bar.y, 2, bar.h}, (holds_[i] > 0.97 || (i == 1 && clipped)) ? theme.danger : theme.text);
    }
}
