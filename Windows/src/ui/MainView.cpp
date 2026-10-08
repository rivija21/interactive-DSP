#include "MainView.h"
#include "../app/Export.h"
#include "../platform/Dispatch.h"
#include "../util/Format.h"
#include "../util/Text.h"
#include <algorithm>
#include <cmath>

namespace {

std::vector<std::string> responseTitles() {
    std::vector<std::string> t;
    for (auto m : kAllResponseModes) t.push_back(responseModeTitle(m));
    return t;
}

std::vector<std::string> liveTitles() {
    std::vector<std::string> t;
    for (auto v : kAllLiveViews) t.push_back(liveViewTitle(v));
    return t;
}

std::vector<std::string> scopeTitles() {
    std::vector<std::string> t;
    for (double ms : MainView::scopeWindows) t.push_back(std::to_string(int(ms)) + " ms");
    return t;
}

std::string fileSlug(const std::string& title) { return replaceAll(lowercased(title), " ", "-"); }

} // namespace

const std::vector<double> MainView::scopeWindows = {2, 5, 10, 20, 50, 100, 500};

// MARK: - Toast

void Toast::show(const std::string& message) {
    text_ = "   " + message + "   ";
    shownAt_ = mediaTime();
    Size s = intrinsicSize();
    frame.w = s.w;
    frame.h = s.h;
    if (parent) parent->layout();
    setNeedsDisplay();
}

Size Toast::intrinsicSize() {
    Size s = measureText(text_, Fonts::system(12.5f, FontWeight::medium));
    return {std::ceil(s.w) + 4, std::ceil(s.h) + 2};
}

void Toast::tick() {
    // Fade in over 0.15 s, hold, then fade out over 0.4 s from 1.8 s.
    double t = mediaTime() - shownAt_;
    float a;
    if (t < 0.15) a = float(t / 0.15);
    else if (t < 1.8) a = 1;
    else if (t < 2.2) a = float(1 - (t - 1.8) / 0.4);
    else a = 0;
    a = std::max(0.0f, std::min(1.0f, a));
    if (a != alpha_) {
        alpha_ = a;
        setNeedsDisplay();
    }
}

void Toast::draw(Canvas& c) {
    if (alpha_ <= 0 || text_.empty()) return;
    c.fillRoundedRect(bounds(), 8, Color::hex(0x2B2A33, alpha_));
    const TextLayout& l = textLayout(text_, Fonts::system(12.5f, FontWeight::medium));
    c.drawLayout(l, {std::round((frame.w - l.size.w) / 2), std::round((frame.h - l.size.h) / 2)}, Color::white(alpha_));
}

// MARK: - MainView

MainView::MainView(LabModel& m)
    : model(m), topBar(m), banner(m), design(m), zPlane(m), response(m), spectrum(m), spectrogram(m), scope(m), theory(m),
      responseControl_(responseTitles()), liveControl_(liveTitles()),
      scopeWindow_(PopupButton::titles(scopeTitles()), ControlSize::smallSize, 11) {
    for (Panel* p : {&designPanel_, &zPanel_, &responsePanel_, &livePanel_, &theoryPanel_}) p->isLayer = true;
    buildHeaders();
    for (Widget* w : std::initializer_list<Widget*>{&topBar, &banner, &designPanel_, &zPanel_, &responsePanel_, &livePanel_,
                                                    &theoryPanel_, &toast_})
        addChild(w);
    model.observe([this](ModelChange change) { modelChanged(change); });
    modelChanged(Change::all);
    theory.onCopied = [this](const std::string& message) { showToast(message); };
    bannerHeight_ = banner.targetHeight;
}

void MainView::buildHeaders() {
    toolControl_.tooltip = "Move: drag poles and zeros. + Zero / + Pole: click in the plane to add one (near the real axis adds a real one).";
    toolControl_.onChange = [this](int i) { model.setZTool(ZTool(i)); };
    mapButton_.tooltip = "Shade the plane by |H(z)|: blue valleys at zeros, red peaks at poles (roots at the origin, which only "
                         "add delay, are left out)";
    mapButton_.onToggle = [this](bool on) { model.setShowHMap(on); };
    resetButton_.onClick = [this] { zPlane.resetView(); };
    zExport_.onClick = [this] { exportZ(); };
    for (Widget* w : std::initializer_list<Widget*>{&toolControl_, &mapButton_, &resetButton_, &zExport_}) zPanel_.controls.addArranged(w);
    zPanel_.controls.compressible = &toolControl_;
    zPanel_.setBody(&zPlane);

    responseControl_.onChange = [this](int i) { model.setResponseMode(ResponseMode(i)); };
    rExport_.onClick = [this] { exportResponse(); };
    responsePanel_.controls.addArranged(&responseControl_);
    responsePanel_.controls.addArranged(&rExport_);
    responsePanel_.controls.compressible = &responseControl_;
    responsePanel_.setBody(&response);

    liveControl_.onChange = [this](int i) { model.setLiveView(LiveView(i)); };
    axisControl_.tooltip = "Frequency axis scale (all plots)";
    axisControl_.onChange = [this](int i) { model.setLogAxis(i == 0); };
    predictionBox_.tooltip = "Overlay input spectrum + |H| (dB): what theory says the output should be";
    predictionBox_.onToggle = [this](bool on) { model.setShowPrediction(on); };
    spectrogramSource_.onChange = [this](int i) { model.setSpectrogramShowsInput(i == 0); };
    scopeWindow_.tooltip = "Time shown across the screen";
    scopeWindow_.onSelect = [this](int i) { model.setScopeWindowMs(scopeWindows[size_t(i)]); };
    holdButton_.tooltip = "Freeze the live display";
    holdButton_.onToggle = [this](bool on) { model.setHold(on); };
    lExport_.onClick = [this] { exportLive(); };
    for (Widget* w : std::initializer_list<Widget*>{&liveControl_, &predictionBox_, &spectrogramSource_, &scopeWindow_, &holdButton_,
                                                    &axisControl_, &lExport_})
        livePanel_.controls.addArranged(w);
    livePanel_.controls.compressible = &liveControl_;
    for (Widget* v : std::initializer_list<Widget*>{&spectrum, &spectrogram, &scope}) v->liveOverlay = true;
    liveBody_.addChild(&spectrum);
    liveBody_.addChild(&spectrogram);
    liveBody_.addChild(&scope);
    livePanel_.setBody(&liveBody_);

    theoryPanel_.controls.addArranged(&theory.pageControl);
    theoryPanel_.setBody(&theory);
    designPanel_.setBody(&design);
}

float MainView::minimumWidth() const {
    float theoryW = theoryVisible() ? 340.0f + 10.0f : 0.0f;
    // Design panel, z-plane (>= 240) and response (>= 380) with their gaps.
    return 10 + 272 + 10 + 240 + 10 + 380 + 10 + theoryW;
}

void MainView::layout() {
    float W = frame.w, H = frame.h;
    const float gap = 10;
    topBar.setFrame(snapRect({0, 0, W, 54}));
    banner.setFrame(snapRect({0, 54, W, bannerHeight_}));
    banner.setHidden(bannerHeight_ < 0.5f);
    float top = 54 + bannerHeight_ + gap;
    float panelH = std::max(100.0f, H - gap - top);
    designPanel_.setFrame(snapRect({gap, top, 272, panelH}));
    float theoryW = theoryVisible() ? 340.0f : 0.0f;
    theoryPanel_.setFrame(snapRect({W - gap - theoryW, top, theoryW, panelH}));
    float zLeft = gap + 272 + gap;
    float middleRight = W - gap - theoryW - gap;
    float avail = panelH - gap;
    float liveH = std::max(220.0f, avail / 2);
    float zH = std::max(60.0f, avail - liveH);
    // Roughly square plot area (width = body height), given up first when space is short.
    // As on macOS, the header controls' compression resistance outranks that, so the
    // panel grows until its controls fit.
    float maxZW = middleRight - zLeft - gap - 380;
    float zW = std::max(240.0f, std::min(std::max(zH - 26, zPanel_.headerWidth()), maxZW));
    zPanel_.setFrame(snapRect({zLeft, top, zW, zH}));
    float respLeft = zLeft + zW + gap;
    responsePanel_.setFrame(snapRect({respLeft, top, std::max(10.0f, middleRight - respLeft), zH}));
    livePanel_.setFrame(snapRect({zLeft, top + zH + gap, std::max(10.0f, middleRight - zLeft), liveH}));
    Size ts = toast_.intrinsicSize();
    toast_.setFrame(snapRect({livePanel_.frame.midX() - ts.w / 2, H - 28 - ts.h, ts.w, ts.h}));
}

void MainView::draw(Canvas& c) { c.fillRect(bounds(), Theme::app().background); }

// MARK: Model changes

void MainView::modelChanged(ModelChange change) {
    if (intersects(change, Change::filter | Change::zeros | Change::sampleRate)) {
        zPlane.invalidate();
        response.invalidate();
    }
    if (intersects(change, Change::filter | Change::source | Change::sampleRate)) spectrum.resetSmoothing();
    if (intersects(change, Change::display | Change::sampleRate)) {
        response.invalidate();
        zPlane.invalidate();
        spectrum.setNeedsDisplay();
        spectrogram.reset();
    }
    if (intersects(change, Change::cursor | Change::selection)) {
        zPlane.setNeedsDisplay();
        response.setNeedsDisplay();
    }
    if (intersects(change, Change::display | Change::selection | Change::source)) refreshHeaders();
    if (change & Change::filter) {
        if (Host::shared().hwnd) SetWindowTextW(Host::shared().hwnd, widen("Filter Lab: " + filterTitle(model.spec())).c_str());
    }
}

void MainView::refreshHeaders() {
    toolControl_.setSelected(int(model.zTool()));
    mapButton_.setChecked(model.showHMap());
    responseControl_.setSelected(int(model.responseMode()));
    liveControl_.setSelected(int(model.liveView()));
    axisControl_.setSelected(model.logAxis() ? 0 : 1);
    predictionBox_.setChecked(model.showPrediction());
    spectrogramSource_.setSelected(model.spectrogramShowsInput() ? 0 : 1);
    for (size_t i = 0; i < scopeWindows.size(); i++)
        if (scopeWindows[i] == model.scopeWindowMs()) scopeWindow_.selectItem(int(i));
    holdButton_.setChecked(model.hold());
    LiveView live = model.liveView();
    spectrum.setHidden(live != LiveView::spectrum);
    spectrogram.setHidden(live != LiveView::spectrogram);
    scope.setHidden(live != LiveView::scope);
    predictionBox_.setHidden(live != LiveView::spectrum);
    spectrogramSource_.setHidden(live != LiveView::spectrogram);
    scopeWindow_.setHidden(live != LiveView::scope);
    axisControl_.setHidden(live == LiveView::scope);
    livePanel_.layout();
    livePanel_.setNeedsDisplay();
}

// MARK: Animation

void MainView::tick() {
    model.audio.tick();
    topBar.tick();
    design.tick();
    theory.tick();
    toast_.tick();
    // Banner height animates towards its target (like NSAnimationContext's default 0.25 s).
    float target = banner.targetHeight;
    if (std::fabs(bannerHeight_ - target) > 0.01f) {
        float step = 34.0f / 15.0f;
        bannerHeight_ = target > bannerHeight_ ? std::min(target, bannerHeight_ + step) : std::max(target, bannerHeight_ - step);
        layout();
        setNeedsDisplay();
    }
    HWND hwnd = Host::shared().hwnd;
    if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return;
    if (Host::shared().occluded()) {
        // Like occlusionState on macOS: no live updates while nothing can be seen. A cheap
        // frame now and then notices when the window is uncovered.
        static int skipped = 0;
        if (++skipped < 15) return;
        skipped = 0;
        Host::shared().requestRender();
        return;
    }
    switch (model.liveView()) {
    case LiveView::spectrum: spectrum.tick(); break;
    case LiveView::spectrogram: spectrogram.tick(); break;
    case LiveView::scope: scope.tick(); break;
    }
    if (sourceIsTone(model.source())) {
        double f = model.audio.params.currentFrequency.value();
        if (std::fabs(f - lastMarkerFrequency_) > 0.05) {
            lastMarkerFrequency_ = f;
            zPlane.setNeedsDisplay();
            response.setNeedsDisplay();
        }
    }
}

void MainView::showToast(const std::string& message) {
    toast_.show(message);
    layout();
}

void MainView::setTheoryVisible(bool visible) {
    theoryPanel_.setHidden(!visible);
    layout();
    setNeedsDisplay();
}

// MARK: Actions

void MainView::exportZ() { zPlane.exportPNG("z-plane.png"); }

void MainView::exportResponse() { response.exportPNG(fileSlug(responseModeTitle(model.responseMode())) + "-response.png"); }

void MainView::exportLive() {
    PlotView* view = nullptr;
    switch (model.liveView()) {
    case LiveView::spectrum: view = &spectrum; break;
    case LiveView::spectrogram: view = &spectrogram; break;
    case LiveView::scope: view = &scope; break;
    }
    view->exportPNG(lowercased(liveViewTitle(model.liveView())) + ".png", model.liveView() != LiveView::spectrogram);
}
