#pragma once
// Stand-ins for the AppKit controls the app uses, drawn to look like macOS (light).
#include "Icons.h"
#include "Theme.h"
#include "Widget.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class ControlSize { smallSize, regular };

// MARK: - Labels

class Label : public Widget {
public:
    Label(std::string text = "", Font font = Fonts::label(), Color color = Theme::app().secondaryText);
    void setText(const std::string& t);
    const std::string& text() const { return text_; }
    void setColor(Color c);
    void setFont(Font f);
    Size intrinsicSize() override;
    void draw(Canvas& c) override;
    bool acceptsMouse() const override { return !tooltip.empty(); }
    DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
    /// Upper bound on the intrinsic width (0 = none).
    float maxWidth = 0;

protected:
    std::string text_;
    Font font_;
    Color color_;
};

class WrappingLabel : public Widget {
public:
    WrappingLabel(std::string text = "", Font font = Fonts::smallText(), Color color = Theme::app().secondaryText);
    void setText(const std::string& t);
    const std::string& text() const { return text_; }
    void setColor(Color c);
    float heightForWidth(float w) override;
    void draw(Canvas& c) override;
    bool acceptsMouse() const override { return false; }

private:
    std::string text_;
    Font font_;
    Color color_;
};

/// Multi-line attributed text (the "Filter facts" box).
class RichLabel : public Widget {
public:
    void setParagraphs(std::vector<Paragraph> p);
    float heightForWidth(float w) override;
    void draw(Canvas& c) override;
    bool acceptsMouse() const override { return false; }

private:
    void rebuild(float w);
    std::vector<Paragraph> paragraphs_;
    std::vector<std::unique_ptr<RichParagraph>> laid_;
    float laidWidth_ = -1;
};

// MARK: - Buttons

class SegmentedControl : public Widget {
public:
    SegmentedControl(std::vector<std::string> titles, ControlSize size = ControlSize::smallSize);
    std::function<void(int)> onChange;
    int selected() const { return selected_; }
    void setSelected(int i);
    void setSegmentWidth(int i, float w);
    bool fillEqually = false;
    Size intrinsicSize() override;
    void draw(Canvas& c) override;
    void mouseDown(const MouseEvent& e) override;
    void mouseDragged(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;

private:
    std::vector<Rect> segmentRects();
    int segmentAt(Point p);
    std::vector<std::string> titles_;
    std::vector<float> widths_;
    ControlSize size_;
    Font font_;
    int selected_ = 0;
    int pressed_ = -1;
    bool pressedInside_ = false;
};

class PopupButton : public Widget {
public:
    struct Item {
        std::string title;
        Icon icon = Icon::none;
        bool separatorAfter = false;
    };
    PopupButton(std::vector<Item> items, ControlSize size = ControlSize::smallSize, float fontSize = 0);
    static std::vector<Item> titles(const std::vector<std::string>& t);
    std::function<void(int)> onSelect;
    int selected() const { return selected_; }
    void selectItem(int i);
    /// A pull-down shows a fixed title and no check marks.
    bool pullsDown = false;
    std::string pullDownTitle;
    Size intrinsicSize() override;
    void draw(Canvas& c) override;
    void mouseDown(const MouseEvent& e) override;

private:
    std::vector<Item> items_;
    ControlSize size_;
    Font font_;
    int selected_ = 0;
    bool open_ = false;
};

class Checkbox : public Widget {
public:
    Checkbox(std::string title);
    std::function<void(bool)> onToggle;
    bool checked() const { return checked_; }
    void setChecked(bool v);
    Size intrinsicSize() override;
    void draw(Canvas& c) override;
    void mouseDown(const MouseEvent& e) override;
    void mouseDragged(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;

private:
    std::string title_;
    bool checked_ = false;
    bool pressed_ = false;
};

class PushButton : public Widget {
public:
    PushButton(std::string title, ControlSize size = ControlSize::smallSize);
    std::function<void()> onClick;
    void setTitle(const std::string& t);
    void setIcon(Icon i);
    /// pushOnPushOff buttons draw an "on" state.
    bool toggles = false;
    bool on = false;
    std::optional<Color> tint;
    Size intrinsicSize() override;
    void draw(Canvas& c) override;
    void mouseDown(const MouseEvent& e) override;
    void mouseDragged(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;

private:
    std::string title_;
    Icon icon_ = Icon::none;
    ControlSize size_;
    Font font_;
    bool pressed_ = false;
    bool inside_ = false;
};

/// A borderless symbol button (accessory bar style).
class IconButton : public Widget {
public:
    IconButton(Icon icon, std::string tip, Color tint = Theme::app().secondaryText);
    std::function<void()> onClick;
    void setIcon(Icon i);
    Color tint;
    float iconSize = 15;
    Size intrinsicSize() override;
    void draw(Canvas& c) override;
    void mouseDown(const MouseEvent& e) override;
    void mouseDragged(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;

private:
    Icon icon_;
    bool pressed_ = false;
    bool inside_ = false;
};

// MARK: - Slider

class Slider : public Widget {
public:
    Slider();
    std::function<void(double)> onChange;
    double value() const { return value_; }
    void setValue(double v);
    Size intrinsicSize() override { return {100, 17}; }
    void draw(Canvas& c) override;
    void mouseDown(const MouseEvent& e) override;
    void mouseDragged(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;

private:
    double valueAt(float x) const;
    float knobX() const;
    double value_ = 0;
    bool dragging_ = false;
    float grabOffset_ = 0;
};

/// The editable value in a SliderRow: looks like a label, click to type.
class ValueField : public Widget {
public:
    ValueField();
    std::function<void(const std::string&)> onCommit;
    std::function<void()> onCancel;
    void setText(const std::string& t);
    const std::string& text() const { return text_; }
    bool editing() const { return editing_; }
    Font font = Fonts::readout();
    Color color = Theme::app().response;
    Size intrinsicSize() override;
    void draw(Canvas& c) override;
    void mouseDown(const MouseEvent& e) override;
    Cursor cursorAt(Point) override { return Cursor::ibeam; }

private:
    std::string text_;
    bool editing_ = false;
};

/// Label, editable value and slider. The slider position can map linearly, on a log scale
/// (frequencies) or to integers.
class SliderRow : public Widget {
public:
    enum class Scale { linear, log, integer };
    SliderRow(std::string title, double min, double max, Scale scale, std::function<std::string(double)> format,
              std::function<std::optional<double>(const std::string&)> parse = nullptr);

    std::function<void(double)> onChange;
    double value() const { return value_; }
    void setValue(double v);
    /// Changes the upper end of a frequency slider (it depends on the sample rate).
    void setRange(double newMax);
    void setTitle(const std::string& t);

    float heightForWidth(float w) override;
    Size intrinsicSize() override { return {fixedWidth > 0 ? fixedWidth : 200, heightForWidth(200)}; }
    void layout() override;

private:
    double toSlider(double v) const;
    double fromSlider(double t) const;
    void sliderMoved(double t);
    void fieldEdited(const std::string& text);

    Label title_;
    ValueField field_;
    Slider slider_;
    Scale scale_;
    double minValue_;
    double maxLimit_;
    std::function<std::string(double)> format_;
    std::function<std::optional<double>(const std::string&)> parse_;
    double value_ = 0;
};

/// A label on the left, a control on the right.
class FormRow : public Widget {
public:
    FormRow(std::string title, Widget* control, float labelWidth = 78);
    float heightForWidth(float) override;
    void layout() override;

private:
    Label label_;
    Widget* control_;
    float labelWidth_;
};

class Separator : public Widget {
public:
    Separator() { fixedHeight = 1; }
    float heightForWidth(float) override { return 1; }
    void draw(Canvas& c) override { c.fillRect(bounds(), Theme::app().panelBorder); }
    bool acceptsMouse() const override { return false; }
};

// MARK: - Containers

class StackView : public Widget {
public:
    enum class Orientation { horizontal, vertical };
    explicit StackView(Orientation o, float spacing = 8) : orientation(o), spacing(spacing) {}
    Orientation orientation;
    float spacing;
    float insetTop = 0, insetLeft = 0, insetBottom = 0, insetRight = 0;
    void addArranged(Widget* w) { addChild(w); }
    /// In a horizontal stack, the child that gives up width first when space is short.
    Widget* compressible = nullptr;
    float availableWidth = 0;   // 0 = unlimited
    float heightForWidth(float w) override;
    Size intrinsicSize() override;
    void layout() override;
    bool acceptsMouse() const override { return false; }
};

class ScrollView : public Widget {
public:
    explicit ScrollView(Widget* document);
    Widget* document;
    float offset() const { return offset_; }
    void setOffset(float y);
    void scrollToTop() { setOffset(0); }
    /// Re-measures the document (call after its content changes).
    void documentChanged();
    void layout() override;
    bool scrollWheel(const ScrollEvent& e) override;
    void drawOverChildren(Canvas& c) override;
    void mouseDown(const MouseEvent& e) override;
    void mouseDragged(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;
    void mouseMoved(const MouseEvent& e) override;
    void mouseExited() override;
    void tick();
    Widget* hitTest(Point inParent) override;

private:
    Rect knobRect() const;
    float contentHeight_ = 0;
    float offset_ = 0;
    double lastActivity_ = -10;
    bool dragging_ = false;
    float dragStartY_ = 0, dragStartOffset_ = 0;
    bool nearScroller_ = false;
};

/// A rounded instrument panel with a title bar for controls.
class Panel : public Widget {
public:
    explicit Panel(std::string title);
    StackView controls{StackView::Orientation::horizontal, 8};
    Widget body;
    void setBody(Widget* w);
    /// Width the header needs to show the title and every control uncompressed.
    float headerWidth();
    void layout() override;
    void draw(Canvas& c) override;
    void drawOverChildren(Canvas& c) override;
    void drawAfterOverlays(Canvas& c) override;

private:
    Label title_;
    Widget* content_ = nullptr;
};

/// Peak meters for input and output with a short hold, like a mixing desk.
class LevelMeter : public Widget {
public:
    LevelMeter() {
        fixedWidth = 120;
        fixedHeight = 26;
    }
    bool clipped = false;
    void update(double input, double output);
    Size intrinsicSize() override { return {120, 26}; }
    void draw(Canvas& c) override;
    bool acceptsMouse() const override { return false; }

private:
    double levels_[2] = {0, 0};
    double holds_[2] = {0, 0};
};

/// A native popup menu at a window point (DIPs); returns the chosen index or -1.
struct MenuEntry {
    std::string title;
    bool checked = false;
    bool separator = false;
    bool enabled = true;
};
int showPopupMenu(const std::vector<MenuEntry>& entries, Point windowPoint);
