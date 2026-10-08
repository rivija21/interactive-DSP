#include "LabModel.h"
#include "../platform/Dialogs.h"
#include "../platform/Dispatch.h"
#include "../platform/Settings.h"
#include "../util/Format.h"
#include "../util/Text.h"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>

const char* liveViewTitle(LiveView v) {
    switch (v) {
    case LiveView::spectrum: return "Spectrum";
    case LiveView::spectrogram: return "Spectrogram";
    case LiveView::scope: return "Scope";
    }
    return "";
}

const char* responseModeTitle(ResponseMode m) {
    switch (m) {
    case ResponseMode::magnitude: return "Magnitude";
    case ResponseMode::phase: return "Phase";
    case ResponseMode::groupDelay: return "Group delay";
    case ResponseMode::impulse: return "Impulse";
    case ResponseMode::step: return "Step";
    }
    return "";
}

const std::vector<double> LabModel::sampleRates = {8000, 16000, 22050, 32000, 44100, 48000};

static SerialQueue& rootQueue() {
    static SerialQueue* q = new SerialQueue();
    return *q;
}

LabModel::LabModel() {
    filter_ = DigitalFilter::passthrough(48000);
    restore();
    audio.onStatus = [this](std::optional<std::string> message) {
        if (message) showBanner(Banner{*message, Banner::Style::warning, std::nullopt, "audio"});
    };
    filter_ = buildFilter(spec_, fs_);
}

void LabModel::start() {
    audio.params.frequency.set(frequency_);
    audio.params.micGain.set(micGain_);
    audio.params.volume.set(volume_);
    audio.params.listen.set(listen_ ? 1 : 0);
    audio.params.filterOn.set(filterOn_ ? 1 : 0);
    audio.setSource(source_);
    audio.start(fs_);
    redesign();
    if (!Settings::shared().getBool("seenListenTip")) {
        showBanner(Banner{"Click \xF0\x9F\x94\x88 Listen (top right) to hear the filter. Press Space to switch between the "
                          "original and filtered sound.",
                          Banner::Style::info, std::string("Got it"), "listenTip"});
    }
}

// MARK: Observation

void LabModel::observe(std::function<void(ModelChange)> handler) { observers_.push_back(std::move(handler)); }

void LabModel::notify(ModelChange change) {
    for (auto& handler : observers_) handler(change);
    if (intersects(change, Change::filter | Change::source | Change::output | Change::display | Change::sampleRate))
        scheduleSave();
}

// MARK: Settings mirrored to the audio engine

void LabModel::setFrequency(double v) {
    frequency_ = v;
    audio.params.frequency.set(v);
    notify(Change::source);
}

void LabModel::setMicGain(double v) {
    micGain_ = v;
    audio.params.micGain.set(v);
    notify(Change::source);
}

void LabModel::setListen(bool v) {
    listen_ = v;
    audio.params.listen.set(v ? 1 : 0);
    notify(Change::output);
}

void LabModel::setVolume(double v) {
    volume_ = v;
    audio.params.volume.set(v);
    notify(Change::output);
}

void LabModel::setFilterOn(bool v) {
    filterOn_ = v;
    audio.params.filterOn.set(v ? 1 : 0);
    notify(Change::output);
}

void LabModel::setPaused(bool v) {
    paused_ = v;
    audio.params.paused.set(v ? 1 : 0);
    notify(Change::output);
}

void LabModel::setCursorFrequency(std::optional<double> v) {
    if (v == cursorFrequency_) return;
    cursorFrequency_ = v;
    notify(Change::cursor);
}

void LabModel::setSelectedItem(std::optional<ItemID> v) {
    if (v == selectedItem_) return;
    selectedItem_ = v;
    notify(Change::selection);
}

// MARK: Design

void LabModel::setSpec(const DesignSpec& s, bool undoable) {
    DesignSpec clamped = s.clamped(fs_);
    if (clamped == spec_) return;
    if (undoable) {
        double now = mediaTime();
        if (now - lastUndoRegistration_ > 0.6) {
            undoStack_.push_back(spec_);
            if (undoStack_.size() > 500) undoStack_.erase(undoStack_.begin());
            redoStack_.clear();
        }
        lastUndoRegistration_ = now;
    }
    spec_ = clamped;
    redesign();
}

void LabModel::updateSpec(const std::function<void(DesignSpec&)>& change) {
    DesignSpec s = spec_;
    change(s);
    setSpec(s);
}

void LabModel::undo() {
    if (undoStack_.empty()) return;
    DesignSpec s = undoStack_.back();
    undoStack_.pop_back();
    redoStack_.push_back(spec_);
    lastUndoRegistration_ = 0;
    spec_ = s.clamped(fs_);
    redesign();
}

void LabModel::redo() {
    if (redoStack_.empty()) return;
    DesignSpec s = redoStack_.back();
    redoStack_.pop_back();
    undoStack_.push_back(spec_);
    lastUndoRegistration_ = 0;
    spec_ = s.clamped(fs_);
    redesign();
}

void LabModel::redesign() {
    designGeneration_ += 1;
    DigitalFilter f = buildFilter(spec_, fs_);
    zerosPending_ = false;
    if (f.structure.isFIR) {
        zerosPending_ = true;
        f.zpk.poles.clear();
        rootGeneration_->store(designGeneration_);
        int generation = designGeneration_;
        auto token = rootGeneration_;
        std::vector<double> h = f.structure.taps;
        rootQueue().async([this, generation, token, h] {
            // Skip stale requests: only the newest design is worth solving.
            if (token->load() != generation) return;
            auto zeros = std::make_shared<std::vector<Complex>>(firZeros(h));
            dispatchMain([this, generation, zeros] {
                if (designGeneration_ != generation) return;
                filter_.zpk.zeros = *zeros;
                filter_.zpk.poles.assign(zeros->size(), Complex());
                zerosPending_ = false;
                notify(Change::zeros);
            });
        });
    }
    filter_ = f;
    audio.setFilter(f);
    updateStabilityBanner();
    notify(Change::filter);
}

const FilterMeasurements& LabModel::measurements() {
    if (cachedMeasurements_ && cachedMeasurements_->first == designGeneration_) return cachedMeasurements_->second;
    std::optional<BandType> band;
    if (spec_.method != DesignMethod::poleZero) band = spec_.band;
    cachedMeasurements_ = std::make_pair(designGeneration_, measure(filter_, band));
    return cachedMeasurements_->second;
}

void LabModel::convertToPoleZero() {
    if (spec_.method == DesignMethod::poleZero) return;
    DesignSpec s = spec_;
    s.items = editableItems(filter_.zpk);
    s.method = DesignMethod::poleZero;
    s.presetName = std::string("Edited ") + (spec_.method == DesignMethod::fir ? "FIR" : familyTitle(spec_.family)) + " design";
    setSpec(s);
}

void LabModel::applyPreset(PoleZeroPreset preset) {
    DesignSpec s = spec_;
    s.method = DesignMethod::poleZero;
    s.items = presetItems(preset, fs_);
    s.presetName = std::string(presetTitle(preset));
    setSpec(s);
    setSelectedItem(std::nullopt);
}

void LabModel::setItems(const std::vector<PZItem>& items, bool undoable) {
    DesignSpec s = spec_;
    s.items = items;
    s.method = DesignMethod::poleZero;
    setSpec(s, undoable);
}

void LabModel::deleteSelectedItem() {
    if (spec_.method != DesignMethod::poleZero || !selectedItem_) return;
    ItemID id = *selectedItem_;
    std::vector<PZItem> items;
    for (const auto& it : spec_.items)
        if (it.id != id) items.push_back(it);
    setItems(items);
    setSelectedItem(std::nullopt);
}

// MARK: Sample rate

void LabModel::setSampleRate(double newFs) {
    if (newFs == fs_) return;
    fs_ = newFs;
    spec_ = spec_.clamped(fs_);
    audio.start(fs_);
    if (frequency_ > fs_ * 0.45) setFrequency(fs_ * 0.2);
    redesign();
    notify(Change::sampleRate);
}

// MARK: Source

void LabModel::setSource(SourceKind newSource) {
    if (newSource == SourceKind::file && !audio.hasFile()) {
        openAudioFile();
        return;
    }
    SourceKind previous = source_;
    if (newSource == SourceKind::microphone) {
        if (previous != SourceKind::microphone) setListen(false);
        source_ = SourceKind::microphone;
        audio.setSource(SourceKind::microphone);
        notify(Change::source);
        audio.startMic([this](std::optional<std::string> error) {
            if (error) {
                micProblem_ = *error == "denied"
                                  ? "Filter Lab isn't allowed to use the microphone. Turn it on in Settings \xE2\x86\x92 "
                                    "Privacy & security \xE2\x86\x92 Microphone."
                                  : *error;
            } else {
                micProblem_.reset();
                if (!dismissedBanners_.count("micTip")) {
                    showBanner(Banner{"Microphone on. Use headphones before turning on Listen, or the speakers will feed "
                                      "back into the mic.",
                                      Banner::Style::info, std::string("OK"), "micTip"});
                }
            }
            updateMicBanner();
        });
    } else {
        if (previous == SourceKind::microphone) audio.stopMic();
        micProblem_.reset();
        updateMicBanner();
        source_ = newSource;
        audio.setSource(newSource);
        notify(Change::source);
    }
}

void LabModel::openAudioFile() {
    auto path = chooseAudioFile(window, "Choose a song or recording to run through your filter.");
    if (!path) {
        notify(Change::source);
        return;
    }
    loadFile(*path);
}

void LabModel::loadFile(const std::wstring& path) {
    if (source_ == SourceKind::microphone) audio.stopMic();
    std::string error;
    if (audio.loadFile(path, error)) {
        source_ = SourceKind::file;
        notify(Change::source);
    } else {
        std::wstring name = path;
        size_t slash = name.find_last_of(L"\\/");
        if (slash != std::wstring::npos) name = name.substr(slash + 1);
        if (!error.empty()) error[0] = char(toupper((unsigned char)error[0]));
        showBanner(Banner{"Couldn't open \xE2\x80\x9C" + narrow(name) + "\xE2\x80\x9D: " + error + ".", Banner::Style::warning,
                          std::string("OK"), "file"});
        notify(Change::source);
    }
}

// MARK: Banners

void LabModel::showBanner(const Banner& b) {
    banner_ = b;
    notify(Change::banner);
}

void LabModel::dismissBanner() {
    if (!banner_) return;
    Banner b = *banner_;
    dismissedBanners_.insert(b.id);
    if (b.id == "listenTip") {
        Settings::shared().set("seenListenTip", true);
        Settings::shared().save();
    }
    if (b.id == "micDenied") micProblem_.reset();
    banner_.reset();
    updateStabilityBanner();
    notify(Change::banner);
}

void LabModel::performBannerAction() {
    if (!banner_) return;
    if (banner_->id == "micDenied") {
        ShellExecuteW(nullptr, L"open", L"ms-settings:privacy-microphone", nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    dismissBanner();
}

void LabModel::updateStabilityBanner() {
    if (!filter_.isStable()) {
        double r = filter_.zpk.maxPoleRadius();
        banner_ = Banner{strf("Unstable: a pole is at |z| = %.3f, outside the unit circle. The impulse response grows "
                              "forever, so the audio is muted.",
                              r),
                         Banner::Style::danger, std::nullopt, "unstable"};
        notify(Change::banner);
    } else if (banner_ && banner_->id == "unstable") {
        banner_.reset();
        notify(Change::banner);
    }
}

void LabModel::updateMicBanner() {
    if (micProblem_) {
        banner_ = Banner{*micProblem_, Banner::Style::warning, std::string("Open Settings"), "micDenied"};
    } else if (banner_ && banner_->id == "micDenied") {
        banner_.reset();
    }
    notify(Change::banner);
}

// MARK: Persistence

void LabModel::scheduleSave() {
    if (saveScheduled_) return;
    saveScheduled_ = true;
    dispatchMainAfter(1, [this] {
        saveScheduled_ = false;
        save();
    });
}

void LabModel::save() {
    auto& d = Settings::shared();
    d.set("spec", spec_.serialize());
    d.set("fs", fs_);
    d.set("source", int(source_ == SourceKind::microphone || source_ == SourceKind::file ? SourceKind::music : source_));
    d.set("frequency", frequency_);
    d.set("volume", volume_);
    d.set("micGain", micGain_);
    d.set("logAxis", logAxis_);
    d.set("showHMap", showHMap_);
    d.set("liveView", int(liveView_));
    d.set("responseMode", int(responseMode_));
    d.set("scopeWindowMs", scopeWindowMs_);
    d.save();
}

void LabModel::restore() {
    auto& d = Settings::shared();
    if (auto text = d.getString("spec")) {
        if (auto s = DesignSpec::deserialize(*text)) spec_ = *s;
    }
    double savedFs = d.getDouble("fs");
    if (std::find(sampleRates.begin(), sampleRates.end(), savedFs) != sampleRates.end()) fs_ = savedFs;
    if (d.has("source")) {
        int s = d.getInt("source");
        if (s >= 0 && s <= int(SourceKind::file) && SourceKind(s) != SourceKind::microphone && SourceKind(s) != SourceKind::file)
            source_ = SourceKind(s);
    }
    if (d.has("frequency")) frequency_ = std::min(std::max(d.getDouble("frequency"), 20.0), fs_ * 0.45);
    if (d.has("volume")) volume_ = d.getDouble("volume");
    if (d.has("micGain")) micGain_ = d.getDouble("micGain");
    if (d.has("logAxis")) logAxis_ = d.getBool("logAxis");
    if (d.has("showHMap")) showHMap_ = d.getBool("showHMap");
    int lv = d.getInt("liveView", 0);
    if (lv >= 0 && lv <= 2) liveView_ = LiveView(lv);
    int rm = d.getInt("responseMode", 0);
    if (rm >= 0 && rm <= 4) responseMode_ = ResponseMode(rm);
    if (d.has("scopeWindowMs")) scopeWindowMs_ = d.getDouble("scopeWindowMs");
    spec_ = spec_.clamped(fs_);
}
