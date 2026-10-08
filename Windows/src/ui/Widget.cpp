#include "Widget.h"
#include <d3d11.h>
#include <dxgi1_3.h>
#include "../platform/Dispatch.h"
#include "../util/Text.h"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>

#ifndef WM_APP_END_EDIT
#define WM_APP_END_EDIT (WM_APP + 2)
#endif

// MARK: - Widget

void Widget::addChild(Widget* w) {
    w->parent = this;
    children.push_back(w);
}

void Widget::setFrame(const Rect& r) {
    bool resized = r.w != frame.w || r.h != frame.h;
    if (r == frame) return;
    frame = r;
    if (resized) layout();
    if (parent) parent->setNeedsDisplay();
    setNeedsDisplay();
}

void Widget::setHidden(bool h) {
    if (h == hidden_) return;
    hidden_ = h;
    if (h) Host::shared().forget(this);
    if (parent) parent->setNeedsDisplay();
    else setNeedsDisplay();
}

void Widget::setEnabled(bool e) {
    if (e == enabled_) return;
    enabled_ = e;
    setNeedsDisplay();
}

bool Widget::isEffectivelyVisible() const {
    for (const Widget* w = this; w; w = w->parent)
        if (w->hidden_) return false;
    return true;
}

void Widget::setNeedsDisplay() {
    for (Widget* w = this; w; w = w->parent) {
        if (w->liveOverlay) break;   // drawn every frame outside the layer
        if (w->isLayer) {
            w->layerDirty = true;
            break;
        }
    }
    Host::shared().requestRender();
}

Point Widget::toLocal(Point p) const {
    for (const Widget* w = this; w; w = w->parent) {
        p.x -= w->frame.x;
        p.y -= w->frame.y;
    }
    return p;
}

Point Widget::toWindow(Point p) const {
    for (const Widget* w = this; w; w = w->parent) {
        p.x += w->frame.x;
        p.y += w->frame.y;
    }
    return p;
}

Rect Widget::windowRect() const {
    Point o = toWindow({0, 0});
    return {o.x, o.y, frame.w, frame.h};
}

Widget* Widget::hitTest(Point p) {
    if (hidden_ || !frame.contains(p)) return nullptr;
    Point local{p.x - frame.x, p.y - frame.y};
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (Widget* w = (*it)->hitTest(local)) return w;
    }
    return acceptsMouse() ? this : nullptr;
}

float snap(float v) {
    float s = Host::shared().scale;
    return std::round(v * s) / s;
}

Rect snapRect(Rect r) {
    float x0 = snap(r.x), y0 = snap(r.y), x1 = snap(r.x + r.w), y1 = snap(r.y + r.h);
    return {x0, y0, x1 - x0, y1 - y0};
}

// MARK: - Host

Host& Host::shared() {
    static Host* h = new Host();
    return *h;
}

void Host::attach(HWND w) {
    hwnd = w;
    setDpi(GetDpiForWindow(w));
}

void Host::setDpi(UINT dpi) {
    scale = float(dpi) / 96.0f;
    if (dc_) dc_->SetDpi(float(dpi), float(dpi));
    if (dc_ && swap_) {
        dc_->SetTarget(nullptr);
        backBuffer_.reset();
        createBackBuffer();
    }
    invalidateAll();
}

static IDXGISwapChain1* swapChain(const Com<IUnknown>& s) { return static_cast<IDXGISwapChain1*>(s.get()); }

bool Host::createDevice() {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,
                                        D3D_FEATURE_LEVEL_9_1};
    // Use the GPU that drives the window's monitor. On laptops with two GPUs the default
    // adapter can be the discrete one, and every frame would then be copied across adapters.
    Com<IDXGIAdapter1> displayAdapter;
    {
        Com<IDXGIFactory1> f;
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(f.put())))) {
            for (UINT i = 0; !displayAdapter; i++) {
                Com<IDXGIAdapter1> adapter;
                if (f->EnumAdapters1(i, adapter.put()) == DXGI_ERROR_NOT_FOUND) break;
                for (UINT j = 0;; j++) {
                    Com<IDXGIOutput> output;
                    if (adapter->EnumOutputs(j, output.put()) == DXGI_ERROR_NOT_FOUND) break;
                    DXGI_OUTPUT_DESC od;
                    if (SUCCEEDED(output->GetDesc(&od)) && od.Monitor == monitor) {
                        displayAdapter = adapter;
                        break;
                    }
                }
            }
        }
    }
    ID3D11Device* device = nullptr;
    HRESULT hr = D3D11CreateDevice(displayAdapter.get(), displayAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                                   nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, UINT(std::size(levels)),
                                   D3D11_SDK_VERSION, &device, nullptr, nullptr);
    if (FAILED(hr)) {
        // No usable GPU (remote desktop, basic display driver): software rasteriser.
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                               UINT(std::size(levels)), D3D11_SDK_VERSION, &device, nullptr, nullptr);
    }
    if (FAILED(hr) || !device) return false;
    *d3d_.put() = device;
    Com<IDXGIDevice1> dxgiDevice;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(dxgiDevice.put())))) return false;
    dxgiDevice->SetMaximumFrameLatency(1);
    if (FAILED(Graphics::shared().d2d1->CreateDevice(dxgiDevice.get(), d2dDevice_.put()))) return false;
    if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, dc_.put()))) return false;
    dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    dc_->SetDpi(scale * 96.0f, scale * 96.0f);

    Com<IDXGIAdapter> adapter;
    Com<IDXGIFactory2> factory;
    dxgiDevice->GetAdapter(adapter.put());
    if (!adapter || FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.put())))) return false;
    RECT rc;
    GetClientRect(hwnd, &rc);
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = UINT(std::max<LONG>(1, rc.right));
    desc.Height = UINT(std::max<LONG>(1, rc.bottom));
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    // Frames are paced by waiting on the swap chain's latency object, so Present never has to
    // block (some drivers spin the CPU while Present waits for the display).
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    IDXGISwapChain1* chain = nullptr;
    if (FAILED(factory->CreateSwapChainForHwnd(device, hwnd, &desc, nullptr, nullptr, &chain)) || !chain) return false;
    *swap_.put() = chain;
    Com<IDXGISwapChain2> chain2;
    if (SUCCEEDED(chain->QueryInterface(__uuidof(IDXGISwapChain2), reinterpret_cast<void**>(chain2.put())))) {
        chain2->SetMaximumFrameLatency(1);
        frameWait_ = chain2->GetFrameLatencyWaitableObject();
        frameReady_ = false;
    }
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    createBackBuffer();
    deviceId++;
    return backBuffer_ ? true : false;
}

void Host::createBackBuffer() {
    Com<IDXGISurface> surface;
    if (FAILED(swapChain(swap_)->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.put())))) return;
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), scale * 96.0f, scale * 96.0f);
    dc_->CreateBitmapFromDxgiSurface(surface.get(), &props, backBuffer_.put());
}

void Host::discardDevice() {
    if (dc_) dc_->SetTarget(nullptr);
    std::function<void(Widget*)> drop = [&](Widget* w) {
        w->layerBitmap.reset();
        for (auto* c : w->children) drop(c);
    };
    if (root) drop(root);
    backBuffer_.reset();
    if (frameWait_) CloseHandle(frameWait_);
    frameWait_ = nullptr;
    frameReady_ = false;
    swap_.reset();
    dc_.reset();
    d2dDevice_.reset();
    d3d_.reset();
    deviceId++;
    invalidateAll();
}

void Host::invalidateAll() {
    std::function<void(Widget*)> mark = [&](Widget* w) {
        w->layerDirty = true;
        for (auto* c : w->children) mark(c);
    };
    if (root) mark(root);
    requestRender();
}

void Host::resize() {
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (swap_ && dc_) {
        dc_->SetTarget(nullptr);
        backBuffer_.reset();
        HRESULT hr = swapChain(swap_)->ResizeBuffers(0, UINT(std::max<LONG>(1, rc.right)), UINT(std::max<LONG>(1, rc.bottom)),
                                                     DXGI_FORMAT_UNKNOWN,
                                                     frameWait_ ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0);
        if (FAILED(hr)) discardDevice();
        else createBackBuffer();
    }
    if (root) root->setFrame({0, 0, float(rc.right) / scale, float(rc.bottom) / scale});
    requestRender();
}

void Host::requestRender() { pending_ = true; }

void Host::render() {
    pending_ = false;
    if (!hwnd || !root || IsIconic(hwnd)) return;
    if (!dc_ && !createDevice()) {
        discardDevice();
        return;
    }
    if (!backBuffer_) return;
    if (frameWait_ && !frameReady_ && WaitForSingleObject(frameWait_, 0) != WAIT_OBJECT_0 && mediaTime() - lastPresent_ < 0.25) {
        pending_ = true;   // the swap chain is still busy: draw when it signals
        return;
    }
    frameReady_ = false;
    textCacheMaintenance();
    // Cached panels first (each into its own bitmap), then the frame that shows them.
    renderLayers(root);
    dc_->SetTarget(backBuffer_.get());
    dc_->BeginDraw();
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    dc_->Clear(background.d2d());
    {
        Canvas c(dc_.get(), deviceId);
        drawWidget(root, c);
        drawTooltip(c);
    }
    HRESULT hr = dc_->EndDraw();
    if (SUCCEEDED(hr)) hr = swapChain(swap_)->Present(1, 0);
    occluded_ = hr == DXGI_STATUS_OCCLUDED;
    lastPresent_ = mediaTime();
    if (hr == HRESULT(D2DERR_RECREATE_TARGET) || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        discardDevice();
        requestRender();
    }
    ValidateRect(hwnd, nullptr);
}

void Host::renderLayers(Widget* w) {
    if (w->hidden() || w->frame.w <= 0 || w->frame.h <= 0) return;
    if (w->isLayer) {
        renderLayer(w);
        return;   // layers don't nest
    }
    for (auto* c : w->children) renderLayers(c);
}

void Host::renderLayer(Widget* w) {
    D2D1_SIZE_U pixels = D2D1::SizeU(UINT32(std::ceil(w->frame.w * scale)), UINT32(std::ceil(w->frame.h * scale)));
    if (pixels.width < 1 || pixels.height < 1) return;
    bool recreate = !w->layerBitmap || w->layerDevice != deviceId;
    if (!recreate) {
        D2D1_SIZE_U have = w->layerBitmap->GetPixelSize();
        float dx, dy;
        w->layerBitmap->GetDpi(&dx, &dy);
        recreate = have.width != pixels.width || have.height != pixels.height || std::fabs(dx - scale * 96.0f) > 0.01f;
    }
    if (recreate) {
        w->layerBitmap.reset();
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            scale * 96.0f, scale * 96.0f);
        if (FAILED(dc_->CreateBitmap(pixels, nullptr, 0, &props, w->layerBitmap.put()))) return;
        w->layerDevice = deviceId;
        w->layerDirty = true;
    }
    if (!w->layerDirty) return;
    renderingLayer_ = true;
    dc_->SetTarget(w->layerBitmap.get());
    dc_->BeginDraw();
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    dc_->Clear(background.d2d());
    {
        Canvas c(dc_.get(), deviceId);
        drawTree(w, c, true);
    }
    HRESULT hr = dc_->EndDraw();
    renderingLayer_ = false;
    w->layerDirty = FAILED(hr);
}

void Host::drawTree(Widget* w, Canvas& c, bool asRoot) {
    c.save();
    if (w->clipsToBounds()) c.clip(w->bounds());
    w->draw(c);
    for (auto* child : w->children) drawWidget(child, c);
    w->drawOverChildren(c);
    c.restore();
    (void)asRoot;
}

void Host::drawWidget(Widget* w, Canvas& c) {
    if (w->hidden()) return;
    if (w->frame.w <= 0 || w->frame.h <= 0) return;
    if (w->liveOverlay && renderingLayer_) return;   // drawn later, straight onto the window
    c.save();
    c.translate(w->frame.x, w->frame.y);
    if (w->isLayer && !renderingLayer_ && dc_ && c.rt() == static_cast<ID2D1RenderTarget*>(dc_.get()) && w->layerBitmap) {
        D2D1_SIZE_F size = w->layerBitmap->GetSize();
        c.save();
        c.clip(w->bounds());
        c.drawBitmap(w->layerBitmap.get(), {0, 0, size.width, size.height}, nullptr, false);
        c.restore();
        c.save();
        c.clip(w->bounds());
        drawOverlays(w, c);
        w->drawAfterOverlays(c);
        c.restore();
    } else {
        drawTree(w, c, false);
    }
    c.restore();
}

void Host::drawOverlays(Widget* w, Canvas& c) {
    for (auto* child : w->children) {
        if (child->hidden() || child->frame.w <= 0 || child->frame.h <= 0) continue;
        c.save();
        c.translate(child->frame.x, child->frame.y);
        if (child->liveOverlay) drawTree(child, c, false);
        else drawOverlays(child, c);
        c.restore();
    }
}

void Host::drawTooltip(Canvas& c) {
    if (!tipShown_ || tooltipText.empty()) return;
    Font font{FontFamily::system, 11.5f};
    const TextLayout& l = textLayout(tooltipText, font, TextMode::wrap, 300);
    float w = l.size.w + 12, h = l.size.h + 8;
    float W = root->frame.w, H = root->frame.h;
    float x = tooltipPoint.x + 2, y = tooltipPoint.y + 20;
    if (x + w > W - 4) x = W - 4 - w;
    if (y + h > H - 4) y = tooltipPoint.y - h - 6;
    x = std::max(4.0f, x);
    Rect box{x, y, w, h};
    c.fillRoundedRect(box.offsetBy(0, 1), 4, Color{0, 0, 0, 0.10f});
    c.fillRoundedRect(box, 4, Color::hex(0xF7F7F7));
    c.strokeRoundedRect(box.insetBy(0.5f, 0.5f), 4, Color{0, 0, 0, 0.22f}, 1);
    c.drawLayout(l, {x + 6, y + 4}, Color::hex(0x262626));
}

static bool isDescendant(const Widget* w, const Widget* ancestor) {
    for (const Widget* p = w; p; p = p->parent)
        if (p == ancestor) return true;
    return false;
}

void Host::forget(Widget* w) {
    if (hover_ && isDescendant(hover_, w)) {
        hover_ = nullptr;
    }
    if (pressed_ && isDescendant(pressed_, w)) pressed_ = nullptr;
    if (tipWidget_ && isDescendant(tipWidget_, w)) {
        tipWidget_ = nullptr;
        tipShown_ = false;
    }
}

MouseEvent Host::eventFor(Widget* w, Point p, WPARAM keys, int clicks) {
    MouseEvent e;
    e.location = w->toLocal(p);
    e.clickCount = clicks;
    e.shift = (keys & MK_SHIFT) != 0;
    e.ctrl = (keys & MK_CONTROL) != 0;
    e.alt = GetKeyState(VK_MENU) < 0;
    return e;
}

void Host::mouseMove(int x, int y, WPARAM keys) {
    Point p = toDip(x, y);
    lastMouse_ = p;
    if (!tracking_) {
        TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        tracking_ = true;
    }
    if (pressed_) {
        pressed_->mouseDragged(eventFor(pressed_, p, keys, clickCount_));
        return;
    }
    Widget* hit = root ? root->hitTest(p) : nullptr;
    if (hit != hover_) {
        if (hover_) hover_->mouseExited();
        hover_ = hit;
        if (hover_) hover_->mouseEntered();
    }
    if (hover_) hover_->mouseMoved(eventFor(hover_, p, keys, 0));
    std::string tip = hover_ ? hover_->tooltipAt(hover_->toLocal(p)) : std::string();
    if (hover_ != tipWidget_ || tip != tooltipText) {
        tipWidget_ = hover_;
        tooltipText = tip;
        hoverSince_ = mediaTime();
        if (tipShown_) {
            tipShown_ = false;
            requestRender();
        }
    }
    if (!tipShown_) {
        tooltipPoint = p;
        hoverSince_ = mediaTime();
    }
}

void Host::mouseLeave() {
    tracking_ = false;
    if (pressed_) return;
    if (hover_) hover_->mouseExited();
    hover_ = nullptr;
    tipWidget_ = nullptr;
    if (tipShown_) {
        tipShown_ = false;
        requestRender();
    }
}

void Host::mouseDown(int x, int y, WPARAM keys) {
    Point p = toDip(x, y);
    if (isEditing()) endEditing(true);
    SetFocus(hwnd);
    double now = mediaTime();
    double dbl = double(GetDoubleClickTime()) / 1000.0;
    if (now - lastClickTime_ < dbl && std::fabs(p.x - lastClickPoint_.x) < 4 && std::fabs(p.y - lastClickPoint_.y) < 4) {
        clickCount_++;
    } else {
        clickCount_ = 1;
    }
    lastClickTime_ = now;
    lastClickPoint_ = p;
    if (tipShown_) {
        tipShown_ = false;
        requestRender();
    }
    tipWidget_ = nullptr;
    hoverSince_ = now + 1e9;   // no tooltip until the mouse moves again
    Widget* hit = root ? root->hitTest(p) : nullptr;
    if (!hit) return;
    pressed_ = hit;
    SetCapture(hwnd);
    Widget* target = hit;
    target->mouseDown(eventFor(target, p, keys, clickCount_));
}

void Host::mouseUp(int x, int y, WPARAM keys) {
    Point p = toDip(x, y);
    Widget* w = pressed_;
    pressed_ = nullptr;
    if (GetCapture() == hwnd) ReleaseCapture();
    if (w) w->mouseUp(eventFor(w, p, keys, clickCount_));
    // The hovered widget may have changed while dragging.
    mouseMove(x, y, keys);
}

void Host::rightMouseDown(int x, int y, WPARAM keys) {
    Point p = toDip(x, y);
    if (isEditing()) endEditing(true);
    if (tipShown_) {
        tipShown_ = false;
        requestRender();
    }
    for (Widget* w = root ? root->hitTest(p) : nullptr; w; w = w->parent) {
        if (w->rightMouseDown(eventFor(w, p, keys, 1))) return;
    }
}

void Host::mouseWheel(int screenX, int screenY, int delta, WPARAM keys) {
    POINT pt = {screenX, screenY};
    ScreenToClient(hwnd, &pt);
    Point p = toDip(pt.x, pt.y);
    for (Widget* w = root ? root->hitTest(p) : nullptr; w; w = w->parent) {
        ScrollEvent e;
        e.location = w->toLocal(p);
        e.deltaY = float(delta) / float(WHEEL_DELTA);
        e.precise = (delta % WHEEL_DELTA) != 0;
        e.ctrl = (keys & MK_CONTROL) != 0;
        if (w->scrollWheel(e)) return;
    }
}

bool Host::setCursor() {
    Widget* w = pressed_ ? pressed_ : hover_;
    Cursor c = w ? w->cursorAt(w->toLocal(lastMouse_)) : Cursor::arrow;
    LPCWSTR id = IDC_ARROW;
    switch (c) {
    case Cursor::arrow: id = IDC_ARROW; break;
    case Cursor::hand: id = IDC_HAND; break;
    case Cursor::ibeam: id = IDC_IBEAM; break;
    case Cursor::resizeLeftRight: id = IDC_SIZEWE; break;
    }
    SetCursor(LoadCursorW(nullptr, id));
    return true;
}

bool Host::keyDown(WPARAM vk) {
    KeyEvent e;
    e.vk = unsigned(vk);
    e.shift = GetKeyState(VK_SHIFT) < 0;
    e.ctrl = GetKeyState(VK_CONTROL) < 0;
    e.alt = GetKeyState(VK_MENU) < 0;
    return onKeyDown && onKeyDown(e);
}

void Host::tick() {
    if (!tipShown_ && !tooltipText.empty() && tipWidget_ && !pressed_ && mediaTime() - hoverSince_ > 0.9) {
        tipShown_ = true;
        requestRender();
    }
}

// MARK: - Editing

static LRESULT CALLBACK editProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    Host* host = reinterpret_cast<Host*>(ref);
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_RETURN || wp == VK_TAB) {
            host->endEditing(true);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            host->endEditing(false);
            return 0;
        }
        break;
    case WM_CHAR:
        if (wp == '\r' || wp == '\t' || wp == 27) return 0;
        break;
    case WM_KILLFOCUS: PostMessageW(GetParent(h), WM_APP_END_EDIT, 1, 0); break;
    default: break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

void Host::beginEditing(Widget* owner, Rect r, const std::string& text, const Font& font, Color color, bool rightAligned,
                        EditSession session) {
    endEditing(true);
    editOwner_ = owner;
    editSession_ = std::move(session);
    editColor_ = color;
    if (editFont_) DeleteObject(editFont_);
    std::wstring family = font.family == FontFamily::mono ? L"Consolas" : L"Segoe UI";
    editFont_ = CreateFontW(-int(std::lround(font.size * scale)), 0, 0, 0, int(font.weight), FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                            family.c_str());
    if (!editBrush_) editBrush_ = CreateSolidBrush(RGB(255, 255, 255));
    int x = int(std::lround(r.x * scale)), y = int(std::lround(r.y * scale));
    int w = int(std::lround(r.w * scale)), h = int(std::lround(r.h * scale));
    edit_ = CreateWindowExW(0, L"EDIT", widen(text).c_str(),
                            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | (rightAligned ? ES_RIGHT : ES_LEFT), x, y, w, h, hwnd,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(edit_, WM_SETFONT, WPARAM(editFont_), TRUE);
    SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
    SetWindowSubclass(edit_, editProc, 1, DWORD_PTR(this));
    SetFocus(edit_);
    SendMessageW(edit_, EM_SETSEL, 0, -1);
    if (owner) owner->setNeedsDisplay();
}

void Host::endEditing(bool commit) {
    if (!edit_) return;
    HWND e = edit_;
    edit_ = nullptr;
    int len = GetWindowTextLengthW(e);
    std::wstring w(size_t(len) + 1, L'\0');
    GetWindowTextW(e, w.data(), len + 1);
    w.resize(size_t(len));
    EditSession session = std::move(editSession_);
    editSession_ = {};
    RemoveWindowSubclass(e, editProc, 1);
    DestroyWindow(e);
    if (GetFocus() == nullptr || GetFocus() == e) SetFocus(hwnd);
    Widget* owner = editOwner_;
    editOwner_ = nullptr;
    if (commit) {
        if (session.commit) session.commit(narrow(w));
    } else if (session.cancel) {
        session.cancel();
    }
    if (owner) owner->setNeedsDisplay();
}

LRESULT Host::editColors(HDC dc) {
    SetTextColor(dc, RGB(int(editColor_.r * 255), int(editColor_.g * 255), int(editColor_.b * 255)));
    SetBkColor(dc, RGB(255, 255, 255));
    return LRESULT(editBrush_);
}
