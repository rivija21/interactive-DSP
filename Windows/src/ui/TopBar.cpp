#include "TopBar.h"
#include "../util/Format.h"
#include "../util/Text.h"
#include <algorithm>
#include <cmath>

namespace {

Icon sourceIcon(SourceKind s) {
    switch (s) {
    case SourceKind::music:
    case SourceKind::musicHum: return Icon::musicNote;
    case SourceKind::sine: return Icon::waveform;
    case SourceKind::square: return Icon::squareOnSquare;
    case SourceKind::sweep: return Icon::arrowUpRight;
    case SourceKind::whiteNoise:
    case SourceKind::pinkNoise: return Icon::noise;
    case SourceKind::clicks: return Icon::metronome;
    case SourceKind::microphone: return Icon::mic;
    case SourceKind::file: return Icon::doc;
    }
    return Icon::none;
}

std::vector<PopupButton::Item> sourceItems() {
    std::vector<PopupButton::Item> items;
    for (auto s : kAllSources) {
        items.push_back({sourceTitle(s), sourceIcon(s), s == SourceKind::microphone || s == SourceKind::clicks});
    }
    return items;
}

std::vector<std::string> rateTitles() {
    std::vector<std::string> t;
    for (double r : LabModel::sampleRates) t.push_back("fs = " + formatHzTick(r) + "Hz");
    return t;
}

std::optional<double> parseGain(const std::string& s) {
    auto v = parseDouble(trim(replaceAll(replaceAll(s, "dB", ""), kMinus, "-")));
    if (!v) return std::nullopt;
    return std::pow(10.0, *v / 20);
}

} // namespace

TopBar::TopBar(LabModel& model)
    : model_(model), playButton_(Icon::pause, "Pause or resume the input", Theme::app().text),
      sourcePopup_(sourceItems(), ControlSize::regular, 13),
      frequencyRow_("Frequency", 20, 20000, SliderRow::Scale::log, formatHz, parseHz),
      micGainRow_("Mic gain", 1, 32, SliderRow::Scale::log,
                  [](double v) { return formatSigned(20 * std::log10(v), "%.0f") + " dB"; }, parseGain),
      sweepLabel_("", Fonts::readout(), Theme::app().output), fileLabel_("", Fonts::label(), Theme::app().text),
      openButton_("Open\xE2\x80\xA6"), ratePopup_(PopupButton::titles(rateTitles()), ControlSize::regular, 12),
      abControl_({"Original", "Filtered"}, ControlSize::regular), listenButton_("Listen", ControlSize::regular) {
    isLayer = true;
    meter.liveOverlay = true;
    playButton_.fixedWidth = 24;
    playButton_.iconSize = 14;
    playButton_.onClick = [this] { model_.setPaused(!model_.paused()); };

    sourcePopup_.fixedWidth = 190;
    sourcePopup_.onSelect = [this](int i) {
        if (!refreshing_) model_.setSource(kAllSources[i]);
    };

    frequencyRow_.onChange = [this](double v) { model_.setFrequency(v); };
    frequencyRow_.fixedWidth = 210;
    micGainRow_.onChange = [this](double v) { model_.setMicGain(v); };
    micGainRow_.fixedWidth = 170;
    openButton_.onClick = [this] { model_.openAudioFile(); };
    fileLabel_.maxWidth = 200;
    for (Widget* w : std::initializer_list<Widget*>{&frequencyRow_, &micGainRow_, &sweepLabel_, &fileLabel_, &openButton_})
        sourceExtras_.addArranged(w);

    ratePopup_.tooltip = "Sampling rate. Nyquist (fs/2) is the top of every frequency axis.";
    ratePopup_.onSelect = [this](int i) {
        if (!refreshing_) model_.setSampleRate(LabModel::sampleRates[size_t(i)]);
    };

    abControl_.tooltip = "Compare the original and filtered sound (Space)";
    abControl_.setSegmentWidth(0, 76);
    abControl_.setSegmentWidth(1, 76);
    abControl_.onChange = [this](int i) {
        if (!refreshing_) model_.setFilterOn(i == 1);
    };

    listenButton_.toggles = true;
    listenButton_.setIcon(Icon::speakerSlash);
    listenButton_.tooltip = "Play the result through your speakers or headphones (L)";
    listenButton_.fixedWidth = 92;
    listenButton_.onClick = [this] { model_.setListen(!model_.listen()); };

    volumeSlider_.tooltip = "Volume";
    volumeSlider_.fixedWidth = 80;
    volumeSlider_.onChange = [this](double v) { model_.setVolume(v); };

    for (Widget* w : std::initializer_list<Widget*>{&playButton_, &sourcePopup_, &sourceExtras_}) left_.addArranged(w);
    for (Widget* w : std::initializer_list<Widget*>{&ratePopup_, &abControl_, &listenButton_, &volumeSlider_, &meter})
        right_.addArranged(w);
    addChild(&left_);
    addChild(&right_);

    model_.observe([this](ModelChange change) {
        if (intersects(change, Change::source | Change::output | Change::sampleRate)) refresh();
    });
    refresh();
}

void TopBar::layout() {
    Size rs = right_.intrinsicSize();
    right_.setFrame({frame.w - 14 - rs.w, std::round((frame.h - rs.h) / 2), rs.w, rs.h});
    right_.layout();
    Size ls = left_.intrinsicSize();
    float maxW = std::max(0.0f, right_.frame.minX() - 12 - 14);
    left_.setFrame({14, std::round((frame.h - ls.h) / 2), std::min(ls.w, maxW), ls.h});
    left_.layout();
}

void TopBar::draw(Canvas& c) { c.fillRect(bounds(), Theme::app().panel); }

void TopBar::drawOverChildren(Canvas& c) { c.fillRect({0, frame.h - 1, frame.w, 1}, Theme::app().panelBorder); }

void TopBar::refresh() {
    refreshing_ = true;
    SourceKind source = model_.source();
    sourcePopup_.selectItem(int(source));
    frequencyRow_.setHidden(!sourceUsesFrequency(source));
    frequencyRow_.setRange(model_.fs() * 0.45);
    frequencyRow_.setValue(model_.frequency());
    micGainRow_.setHidden(source != SourceKind::microphone);
    micGainRow_.setValue(model_.micGain());
    sweepLabel_.setHidden(source != SourceKind::sweep);
    fileLabel_.setHidden(source != SourceKind::file);
    openButton_.setHidden(source != SourceKind::file);
    auto name = model_.audio.fileName();
    fileLabel_.setText(name ? "\xE2\x80\x9C" + *name + "\xE2\x80\x9D" : "");
    for (size_t i = 0; i < LabModel::sampleRates.size(); i++)
        if (LabModel::sampleRates[i] == model_.fs()) ratePopup_.selectItem(int(i));
    abControl_.setSelected(model_.filterOn() ? 1 : 0);
    listenButton_.on = model_.listen();
    listenButton_.setIcon(model_.listen() ? Icon::speakerWave : Icon::speakerSlash);
    if (model_.listen()) listenButton_.tint = Theme::app().output;
    else listenButton_.tint.reset();
    volumeSlider_.setValue(model_.volume());
    volumeSlider_.setEnabled(model_.listen());
    playButton_.setIcon(model_.paused() ? Icon::play : Icon::pause);
    refreshing_ = false;
    layout();
    setNeedsDisplay();
}

void TopBar::tick() {
    auto& p = model_.audio.params;
    meter.clipped = p.clipped.value() != 0;
    p.clipped.set(0);
    meter.update(p.inputPeak.value(), p.outputPeak.value());
    if (model_.source() == SourceKind::sweep) {
        std::string text = "sweeping  " + formatHz(p.currentFrequency.value());
        if (text != sweepLabel_.text()) {
            sweepLabel_.setText(text);
            Size s = sweepLabel_.intrinsicSize();
            if (std::fabs(s.w - sweepLabel_.frame.w) > 0.5f) layout();
        }
    }
}

// MARK: - Banner

BannerView::BannerView(LabModel& model)
    : model_(model), label_("", Fonts::label(), Theme::app().text), actionButton_(""),
      closeButton_(Icon::xmark, "Dismiss", Theme::app().secondaryText) {
    isLayer = true;
    closeButton_.iconSize = 12;
    actionButton_.onClick = [this] { model_.performBannerAction(); };
    closeButton_.onClick = [this] { model_.dismissBanner(); };
    addChild(&label_);
    addChild(&actionButton_);
    addChild(&closeButton_);
    model_.observe([this](ModelChange change) {
        if (change & Change::banner) refresh();
    });
    refresh();
}

void BannerView::refresh() {
    const auto& b = model_.banner();
    if (!b) {
        targetHeight = 0;
        return;
    }
    targetHeight = 34;
    label_.setText(b->text);
    label_.tooltip = b->text;
    Color ink;
    switch (b->style) {
    case Banner::Style::info:
        color_ = Color::hex(0xE6EEF7);
        ink = Color::hex(0x1F3A5F);
        break;
    case Banner::Style::warning:
        color_ = Color::hex(0xFBF0D2);
        ink = Color::hex(0x6B4A12);
        break;
    case Banner::Style::danger:
        color_ = Color::hex(0xF8DEDA);
        ink = Color::hex(0x8A2318);
        break;
    }
    label_.setColor(ink);
    actionButton_.setHidden(!b->actionTitle);
    actionButton_.setTitle(b->actionTitle ? *b->actionTitle : "");
    closeButton_.setHidden(b->id == "unstable");
    layout();
    setNeedsDisplay();
}

void BannerView::layout() {
    // Content stays centred in the full 34-point strip while the height animates.
    float h = 34;
    float y0 = frame.h - h;
    float closeX = frame.w - 14 - closeButton_.fixedWidth;
    closeButton_.setFrame({closeX, y0 + (h - closeButton_.fixedHeight) / 2, closeButton_.fixedWidth, closeButton_.fixedHeight});
    Size as = actionButton_.intrinsicSize();
    float actionW = actionButton_.hidden() ? 0 : as.w;
    float limit = (closeButton_.hidden() ? frame.w - 14 : closeX) - 12 - (actionW > 0 ? actionW + 12 : 0) - 16;
    Size ls = label_.intrinsicSize();
    float lw = std::max(10.0f, std::min(ls.w, limit));
    label_.setFrame({16, y0 + std::round((h - ls.h) / 2), lw, ls.h});
    actionButton_.setFrame({label_.frame.maxX() + 12, y0 + (h - actionButton_.fixedHeight) / 2, as.w, actionButton_.fixedHeight});
}

void BannerView::draw(Canvas& c) { c.fillRect(bounds(), color_); }
