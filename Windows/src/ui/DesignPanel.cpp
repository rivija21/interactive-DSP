#include "DesignPanel.h"
#include "../util/Format.h"
#include "../util/Text.h"
#include <algorithm>
#include <cmath>

namespace {

std::vector<std::string> bandTitles() {
    std::vector<std::string> t;
    for (auto b : kAllBandTypes) t.push_back(bandTitle(b));
    return t;
}

std::vector<std::string> familyTitles() {
    std::vector<std::string> t;
    for (auto f : kAllFamilies) t.push_back(familyTitle(f));
    return t;
}

std::vector<std::string> windowTitles() {
    std::vector<std::string> t;
    for (auto w : kAllWindows) t.push_back(windowTitle(w));
    return t;
}

std::vector<std::string> methodTitles() {
    std::vector<std::string> t;
    for (auto m : kAllMethods) t.push_back(methodTitle(m));
    return t;
}

std::vector<PopupButton::Item> presetItems() {
    std::vector<PopupButton::Item> items;
    for (auto p : kAllPresets) items.push_back({presetTitle(p)});
    return items;
}

std::optional<double> parseDB(const std::string& s) { return parseDouble(trim(replaceAll(s, "dB", ""))); }

std::string fmt0(double v) { return strf("%.0f", v); }

} // namespace

DesignPanel::DesignPanel(LabModel& model)
    : model_(model), methodControl_(methodTitles(), ControlSize::regular),
      iirBand_(PopupButton::titles(bandTitles()), ControlSize::regular, 12),
      family_(PopupButton::titles(familyTitles()), ControlSize::regular, 12),
      order_("Order", 1, 12, SliderRow::Scale::integer, fmt0),
      ripple_("Passband ripple", 0.05, 6, SliderRow::Scale::log, [](double v) { return strf("%.2f dB", v); }, parseDB),
      stopband_("Stopband attenuation", 10, 120, SliderRow::Scale::integer, [](double v) { return strf("%.0f dB", v); }, parseDB),
      firBand_(PopupButton::titles(bandTitles()), ControlSize::regular, 12),
      windowPopup_(PopupButton::titles(windowTitles()), ControlSize::regular, 12),
      taps_("Taps (length N)", 3, 255, SliderRow::Scale::integer, fmt0),
      beta_("Kaiser \xCE\xB2", 0, 16, SliderRow::Scale::linear, [](double v) { return strf("%.1f", v); }),
      cutoff_("Cutoff", 10, 24000, SliderRow::Scale::log, formatHz, parseHz),
      edge1_("Low edge f\xE2\x82\x81", 10, 24000, SliderRow::Scale::log, formatHz, parseHz),
      edge2_("High edge f\xE2\x82\x82", 10, 24000, SliderRow::Scale::log, formatHz, parseHz),
      presetPopup_(presetItems(), ControlSize::regular, 13), selectedTitle_("", Fonts::smallBold(), Theme::app().text),
      deleteButton_("Delete"), clearButton_("Clear All"),
      radius_("Radius r", 0, 1.5, SliderRow::Scale::linear, [](double v) { return strf("%.4f", v); }),
      angle_("Angle (frequency)", 0, 24000, SliderRow::Scale::linear, formatHz, parseHz) {
    stack_.insetTop = 12;
    stack_.insetLeft = 14;
    stack_.insetBottom = 14;
    stack_.insetRight = 14;
    methodControl_.fillEqually = true;

    // Actions.
    methodControl_.onChange = [this](int i) {
        if (refreshing_) return;
        DesignMethod method = kAllMethods[i];
        if (method == DesignMethod::poleZero && model_.spec().items.empty()) {
            model_.convertToPoleZero();
        } else {
            model_.updateSpec([&](DesignSpec& s) { s.method = method; });
        }
    };
    iirBand_.onSelect = [this](int i) { bandChanged(i); };
    firBand_.onSelect = [this](int i) { bandChanged(i); };
    family_.onSelect = [this](int i) {
        if (!refreshing_) model_.updateSpec([&](DesignSpec& s) { s.family = kAllFamilies[i]; });
    };
    windowPopup_.onSelect = [this](int i) {
        if (!refreshing_) model_.updateSpec([&](DesignSpec& s) { s.window = kAllWindows[i]; });
    };
    order_.onChange = [this](double v) { model_.updateSpec([&](DesignSpec& s) { s.order = int(v); }); };
    ripple_.onChange = [this](double v) { model_.updateSpec([&](DesignSpec& s) { s.rippleDB = v; }); };
    stopband_.onChange = [this](double v) { model_.updateSpec([&](DesignSpec& s) { s.stopDB = v; }); };
    taps_.onChange = [this](double v) {
        model_.updateSpec([&](DesignSpec& s) {
            int n = int(v);
            if (firNeedsOddTaps(s.band) && n % 2 == 0) n += n > s.taps ? 1 : -1;
            s.taps = std::max(3, n);
        });
    };
    beta_.onChange = [this](double v) { model_.updateSpec([&](DesignSpec& s) { s.kaiserBeta = v; }); };
    cutoff_.onChange = [this](double v) { model_.updateSpec([&](DesignSpec& s) { s.f1 = v; }); };
    edge1_.onChange = [this](double v) { model_.updateSpec([&](DesignSpec& s) { s.f1 = std::min(v, s.f2 / 1.02); }); };
    edge2_.onChange = [this](double v) { model_.updateSpec([&](DesignSpec& s) { s.f2 = std::max(v, s.f1 * 1.02); }); };
    radius_.onChange = [this](double v) { editSelected(v, std::nullopt); };
    angle_.onChange = [this](double v) { editSelected(std::nullopt, v); };
    presetPopup_.pullsDown = true;
    presetPopup_.pullDownTitle = "Load a preset\xE2\x80\xA6";
    presetPopup_.onSelect = [this](int i) { model_.applyPreset(kAllPresets[i]); };
    deleteButton_.onClick = [this] { model_.deleteSelectedItem(); };
    clearButton_.onClick = [this] {
        model_.setItems({});
        model_.setSelectedItem(std::nullopt);
    };
    pzHelp_.setText("Drag poles (\xC3\x97) and zeros (\xE2\x97\x8B) in the z-plane. Use + Zero / + Pole above the plot to add; "
                    "right-click one to delete. Zeros snap onto the unit circle (hold Alt to stop snapping).");

    // Build the stack.
    cutoffGroup_.addArranged(&cutoff_);
    cutoffGroup_.addArranged(&edge1_);
    cutoffGroup_.addArranged(&edge2_);

    iirBandRow_ = std::make_unique<FormRow>("Response", &iirBand_);
    familyRow_ = std::make_unique<FormRow>("Family", &family_);
    for (Widget* w : std::initializer_list<Widget*>{iirBandRow_.get(), familyRow_.get(), &order_, &ripple_, &stopband_, &iirHint_})
        iirGroup_.addArranged(w);

    firBandRow_ = std::make_unique<FormRow>("Response", &firBand_);
    windowRow_ = std::make_unique<FormRow>("Window", &windowPopup_);
    for (Widget* w : std::initializer_list<Widget*>{firBandRow_.get(), &taps_, windowRow_.get(), &beta_, &firHint_})
        firGroup_.addArranged(w);

    selHeader_ = std::make_unique<SelectionHeader>(&selectedTitle_, &deleteButton_);
    selectedBox_.addArranged(selHeader_.get());
    selectedBox_.addArranged(&radius_);
    selectedBox_.addArranged(&angle_);
    clearButton_.fixedWidth = 90;
    for (Widget* w : std::initializer_list<Widget*>{&presetPopup_, &presetInfo_, &pzHelp_, &selectedBox_, &clearButton_})
        pzGroup_.addArranged(w);

    methodSection_ = section("Design method");
    factsSection_ = section("Filter facts");
    factsBox_.addArranged(factsSection_.get());
    factsBox_.addArranged(&facts_);

    for (Widget* w : std::initializer_list<Widget*>{methodSection_.get(), &methodControl_, &iirGroup_, &firGroup_, &pzGroup_,
                                                    &cutoffGroup_, &separator_, &factsBox_})
        stack_.addArranged(w);
    addChild(&scroll_);

    model_.observe([this](ModelChange change) {
        if (intersects(change, Change::filter | Change::zeros | Change::selection | Change::sampleRate)) refresh();
    });
    refresh();
}

std::unique_ptr<Label> DesignPanel::section(const std::string& title) {
    auto l = std::make_unique<Label>(uppercased(title), Fonts::header(), Theme::app().secondaryText);
    return l;
}

void DesignPanel::layout() {
    scroll_.setFrame(bounds());
    scroll_.layout();
}

// MARK: Refresh from the model

void DesignPanel::refresh() {
    refreshing_ = true;
    const DesignSpec& spec = model_.spec();
    double fs = model_.fs();
    methodControl_.setSelected(int(spec.method));
    iirGroup_.setHidden(spec.method != DesignMethod::iir);
    firGroup_.setHidden(spec.method != DesignMethod::fir);
    pzGroup_.setHidden(spec.method != DesignMethod::poleZero);
    cutoffGroup_.setHidden(spec.method == DesignMethod::poleZero);

    int bandIndex = int(spec.band);
    iirBand_.selectItem(bandIndex);
    firBand_.selectItem(bandIndex);
    family_.selectItem(int(spec.family));
    windowPopup_.selectItem(int(spec.window));

    order_.setValue(double(spec.order));
    order_.setTitle(bandIsBand(spec.band) ? "Order (\xC3\x97" "2 for band filters)" : "Order");
    ripple_.setHidden(!familyUsesPassbandRipple(spec.family));
    stopband_.setHidden(!familyUsesStopbandAttenuation(spec.family));
    ripple_.setValue(spec.rippleDB);
    stopband_.setValue(spec.stopDB);
    iirHint_.setText(iirHintText(spec));

    taps_.setValue(double(spec.taps));
    beta_.setHidden(spec.window != WindowType::kaiser);
    beta_.setValue(spec.kaiserBeta);
    firHint_.setText(firHintText(spec));

    bool single = !bandIsBand(spec.band);
    cutoff_.setHidden(!single);
    edge1_.setHidden(single);
    edge2_.setHidden(single);
    std::string meaning = spec.method == DesignMethod::iir
                              ? std::string(" (") + familyCutoffMeaning(spec.family) + ")"
                              : (spec.method == DesignMethod::fir ? std::string(" (") + kMinus + "6 dB point)" : "");
    cutoff_.setTitle("Cutoff" + meaning);
    for (SliderRow* row : {&cutoff_, &edge1_, &edge2_}) row->setRange(fs / 2 * 0.98);
    cutoff_.setValue(spec.f1);
    edge1_.setValue(spec.f1);
    edge2_.setValue(spec.f2);

    refreshPoleZero(spec, fs);
    facts_.setParagraphs(factsText());
    refreshing_ = false;
    scroll_.documentChanged();
    setNeedsDisplay();
}

std::string DesignPanel::iirHintText(const DesignSpec& s) const {
    int n = bandIsBand(s.band) ? 2 * s.order : s.order;
    int slope = 20 * s.order;
    switch (s.family) {
    case IIRFamily::butterworth:
        return "Maximally flat passband. " + std::to_string(n) + " poles; the response falls by " + std::to_string(slope) +
               " dB per decade past the cutoff.";
    case IIRFamily::chebyshev1: return "Ripple in the passband buys a steeper roll-off than Butterworth at the same order.";
    case IIRFamily::chebyshev2:
        return "Flat passband, ripple in the stopband. Zeros sit on the unit circle and put notches in the stopband.";
    case IIRFamily::elliptic: return "Ripple in both bands gives the sharpest possible transition for a given order.";
    case IIRFamily::bessel: return "Nearly constant group delay, so waveforms keep their shape. The roll-off is gentle.";
    }
    return "";
}

std::string DesignPanel::firHintText(const DesignSpec& s) const {
    double delay = double(s.taps - 1) / 2;
    std::string t = strf("Linear phase: every frequency is delayed by (N\xE2\x88\x92" "1)/2 = %g samples (", delay) +
                    formatMs(delay / model_.fs() * 1000) + ").";
    if (auto a = windowTypicalStopbandDB(s.window)) {
        t += strf(" %s window: about %.0f dB of stopband attenuation.", windowTitle(s.window), *a);
    } else {
        t += strf(" Kaiser \xCE\xB2 = %.1f gives about %.0f dB of stopband attenuation.", s.kaiserBeta, kaiserAttenuation(s.kaiserBeta));
    }
    if (firNeedsOddTaps(s.band)) t += " High-pass and band-stop need an odd N.";
    return t;
}

void DesignPanel::refreshPoleZero(const DesignSpec& spec, double fs) {
    if (spec.presetName) {
        auto preset = presetNamed(*spec.presetName);
        presetInfo_.setText(preset ? *spec.presetName + ": " + presetSummary(*preset) : *spec.presetName);
        presetInfo_.setHidden(false);
    } else {
        presetInfo_.setHidden(true);
    }
    angle_.setRange(fs / 2);
    const PZItem* item = nullptr;
    if (auto id = model_.selectedItem()) {
        for (const auto& it : spec.items)
            if (it.id == *id) item = &it;
    }
    if (item) {
        selectedBox_.setHidden(false);
        const char* kind = item->kind == PZItem::Kind::pole ? "pole" : "zero";
        selectedTitle_.setText(item->paired ? std::string("Selected ") + kind + " pair" : std::string("Selected real ") + kind);
        double r = item->paired ? item->position.magnitude() : std::fabs(item->position.re);
        radius_.setValue(r);
        radius_.setTitle(item->paired ? "Radius r" : "Position on the real axis");
        angle_.setHidden(!item->paired);
        angle_.setValue(std::fabs(item->position.phase()) / (2 * kPi) * fs);
    } else {
        selectedBox_.setHidden(true);
    }
    clearButton_.setEnabled(!spec.items.empty());
}

std::vector<Paragraph> DesignPanel::factsText() {
    const DigitalFilter& f = model_.filter();
    std::vector<Paragraph> out;
    auto line = [&](const std::string& k, const std::string& v, std::optional<Color> color = std::nullopt) {
        Paragraph p;
        p.runs.push_back({padded(k, 13), Fonts::monoSmall(), Theme::app().secondaryText});
        p.runs.push_back({v, Fonts::monoSmall(), color ? *color : Theme::app().text});
        out.push_back(p);
    };
    line("Order", std::to_string(f.order()));
    if (f.isStable()) {
        double r = f.zpk.maxPoleRadius();
        line("Stability", r > 0 ? strf("stable, max |p| = %.4f", r) : "stable (FIR)", Theme::app().output);
    } else {
        line("Stability", strf("UNSTABLE, |p| = %.3f", f.zpk.maxPoleRadius()), Theme::app().danger);
    }
    const FilterMeasurements& m = model_.measurements();
    if (!m.minus3dB.empty()) {
        std::vector<std::string> parts;
        for (size_t i = 0; i < m.minus3dB.size() && i < 2; i++) parts.push_back(formatHz(m.minus3dB[i]));
        line(std::string(kMinus) + "3 dB at", join(parts, ", "));
    }
    if (f.isStable()) line("Delay", formatMs(m.groupDelayMs) + " in passband");
    line("Cost", std::to_string(f.multipliesPerSample()) + " multiplies/sample");
    line("Sample rate", formatHz(model_.fs()));
    return out;
}

// MARK: Actions

void DesignPanel::bandChanged(int index) {
    if (refreshing_) return;
    BandType band = kAllBandTypes[index];
    double fs = model_.fs();
    model_.updateSpec([&](DesignSpec& s) {
        if (bandIsBand(band) && !bandIsBand(s.band)) {
            // Make a sensible band around the old cutoff.
            s.f2 = std::min(fs * 0.45, s.f1 * 3);
        }
        s.band = band;
    });
}

void DesignPanel::editSelected(std::optional<double> r, std::optional<double> f) {
    auto id = model_.selectedItem();
    if (refreshing_ || !id) return;
    auto items = model_.spec().items;
    auto it = std::find_if(items.begin(), items.end(), [&](const PZItem& i) { return i.id == *id; });
    if (it == items.end()) return;
    if (it->paired) {
        double curR = r ? *r : it->position.magnitude();
        double curA = f ? 2 * kPi * *f / model_.fs() : std::fabs(it->position.phase());
        Complex z = Complex::polar(curR, curA);
        it->position = Complex(z.re, std::fabs(z.im));
    } else if (r) {
        it->position = Complex(it->position.re < 0 ? -*r : *r);
    }
    model_.setItems(items);
}
