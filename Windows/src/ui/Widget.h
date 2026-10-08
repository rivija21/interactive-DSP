#pragma once
// A small retained view tree (the stand-in for NSView): frames in parent coordinates,
// y down, mouse events in local coordinates, optional cached layers for big panels.
#include "Graphics.h"
#include <functional>
#include <string>
#include <vector>

struct MouseEvent {
    Point location;      // local coordinates
    int clickCount = 1;
    bool shift = false, ctrl = false, alt = false;
};

struct ScrollEvent {
    Point location;
    float deltaY = 0;    // wheel notches (positive = away from the user)
    bool precise = false;
    bool ctrl = false;
};

struct KeyEvent {
    unsigned vk = 0;
    bool shift = false, ctrl = false, alt = false;
};

enum class Cursor { arrow, hand, ibeam, resizeLeftRight };

class Widget {
public:
    Widget() = default;
    virtual ~Widget() = default;
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    Rect frame;
    std::string tooltip;
    Widget* parent = nullptr;
    std::vector<Widget*> children;
    /// Draws into its own cached bitmap, redrawn only when something inside changes.
    bool isLayer = false;
    /// Redrawn every frame straight onto the window, on top of its enclosing layer's cached
    /// bitmap, so animating content does not force the whole layer to re-render.
    bool liveOverlay = false;
    /// Width/height constraints used by stack views (0 = use the intrinsic size).
    float fixedWidth = 0;
    float fixedHeight = 0;

    void addChild(Widget* w);
    void setFrame(const Rect& r);
    void setHidden(bool h);
    bool hidden() const { return hidden_; }
    void setEnabled(bool e);
    bool enabled() const { return enabled_; }
    Rect bounds() const { return {0, 0, frame.w, frame.h}; }
    bool isEffectivelyVisible() const;

    virtual void layout() {}
    virtual Size intrinsicSize() { return {frame.w, frame.h}; }
    virtual float heightForWidth(float) { return intrinsicSize().h; }
    virtual void draw(Canvas&) {}
    virtual void drawOverChildren(Canvas&) {}
    /// For layers: drawn after the live overlays inside them (e.g. rounded-corner masks).
    virtual void drawAfterOverlays(Canvas&) {}
    virtual bool clipsToBounds() const { return true; }

    virtual bool acceptsMouse() const { return true; }
    virtual void mouseDown(const MouseEvent&) {}
    virtual void mouseDragged(const MouseEvent&) {}
    virtual void mouseUp(const MouseEvent&) {}
    virtual void mouseMoved(const MouseEvent&) {}
    virtual void mouseEntered() {}
    virtual void mouseExited() {}
    /// Return true if handled (otherwise the parent gets it).
    virtual bool rightMouseDown(const MouseEvent&) { return false; }
    virtual bool scrollWheel(const ScrollEvent&) { return false; }
    virtual Cursor cursorAt(Point) { return Cursor::arrow; }
    virtual std::string tooltipAt(Point) { return tooltip; }

    void setNeedsDisplay();
    void setNeedsLayout() {
        layout();
        setNeedsDisplay();
    }
    Point toLocal(Point windowPoint) const;
    Point toWindow(Point local) const;
    Rect windowRect() const;

    virtual Widget* hitTest(Point inParent);

    // Layer cache (managed by the host).
    Com<ID2D1Bitmap1> layerBitmap;
    int layerDevice = 0;
    bool layerDirty = true;

protected:
    bool hidden_ = false;
    bool enabled_ = true;
};

/// The window: owns the render target, routes events and draws the widget tree.
class Host {
public:
    static Host& shared();

    HWND hwnd = nullptr;
    Widget* root = nullptr;
    float scale = 1;          // pixels per DIP
    int deviceId = 1;         // bumped when the Direct2D device is recreated
    Color background;
    std::function<bool(const KeyEvent&)> onKeyDown;   // window-level keys

    void attach(HWND w);
    void resize();
    void setDpi(UINT dpi);
    void requestRender();
    bool renderPending() const { return pending_; }
    /// Signalled when the swap chain can take another frame (null before the device exists).
    HANDLE frameWaitHandle() const { return frameWait_; }
    /// The message loop saw frameWaitHandle() signalled (its wait consumed the signal).
    void frameReady() { frameReady_ = true; }
    /// The last frame couldn't be seen (window covered or on a locked screen).
    bool occluded() const { return occluded_; }
    void render();
    void invalidateAll();

    // Events from the window procedure (client coordinates in pixels).
    void mouseMove(int x, int y, WPARAM keys);
    void mouseLeave();
    void mouseDown(int x, int y, WPARAM keys);
    void mouseUp(int x, int y, WPARAM keys);
    void rightMouseDown(int x, int y, WPARAM keys);
    void mouseWheel(int screenX, int screenY, int delta, WPARAM keys);
    bool setCursor();
    bool keyDown(WPARAM vk);
    void tick();

    /// Called when a widget is hidden or destroyed so stale pointers are dropped.
    void forget(Widget* w);
    /// The mouse capture went away (a menu or dialog opened): stop routing drags.
    void captureLost() { pressed_ = nullptr; }
    Widget* pressed() const { return pressed_; }
    Widget* hovered() const { return hover_; }

    /// Draws a widget subtree into any render target (used for figure export).
    void drawTree(Widget* w, Canvas& c, bool asRoot);

    // In-place text editing (a native EDIT control placed over a widget).
    struct EditSession {
        std::function<void(const std::string&)> commit;
        std::function<void()> cancel;
    };
    void beginEditing(Widget* owner, Rect windowRect, const std::string& text, const Font& font, Color color,
                      bool rightAligned, EditSession session);
    void endEditing(bool commit);
    bool isEditing() const { return edit_ != nullptr; }
    HWND editControl() const { return edit_; }
    LRESULT editColors(HDC dc);

    // Tooltip overlay.
    std::string tooltipText;
    Point tooltipPoint;

private:
    bool createDevice();
    void createBackBuffer();
    void discardDevice();
    void renderLayers(Widget* w);
    void renderLayer(Widget* w);
    void drawWidget(Widget* w, Canvas& c);
    void drawOverlays(Widget* w, Canvas& c);
    bool renderingLayer_ = false;
    void drawTooltip(Canvas& c);
    Point toDip(int x, int y) const { return {float(x) / scale, float(y) / scale}; }
    MouseEvent eventFor(Widget* w, Point p, WPARAM keys, int clicks);

    // Direct3D 11 + flip-model swap chain + one Direct2D device context.
    Com<IUnknown> d3d_;
    Com<ID2D1Device> d2dDevice_;
    Com<ID2D1DeviceContext> dc_;
    Com<IUnknown> swap_;
    Com<ID2D1Bitmap1> backBuffer_;
    HANDLE frameWait_ = nullptr;
    bool frameReady_ = false;
    bool occluded_ = false;
    double lastPresent_ = 0;
    bool pending_ = false;
    Widget* hover_ = nullptr;
    Widget* pressed_ = nullptr;
    bool tracking_ = false;
    double lastClickTime_ = 0;
    Point lastClickPoint_;
    int clickCount_ = 0;
    // tooltip timing
    Widget* tipWidget_ = nullptr;
    double hoverSince_ = 0;
    Point lastMouse_;
    bool tipShown_ = false;
    // editing
    HWND edit_ = nullptr;
    HFONT editFont_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    Color editColor_;
    EditSession editSession_;
    Widget* editOwner_ = nullptr;
};

/// Pixel-snaps a DIP value at the current scale.
float snap(float v);
Rect snapRect(Rect r);
