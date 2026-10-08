#pragma once
#include "../audio/LabAudio.h"
#include "../dsp/Design.h"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

enum class LiveView { spectrum, spectrogram, scope };
inline constexpr LiveView kAllLiveViews[] = {LiveView::spectrum, LiveView::spectrogram, LiveView::scope};
const char* liveViewTitle(LiveView v);

enum class ResponseMode { magnitude, phase, groupDelay, impulse, step };
inline constexpr ResponseMode kAllResponseModes[] = {ResponseMode::magnitude, ResponseMode::phase, ResponseMode::groupDelay,
                                                     ResponseMode::impulse, ResponseMode::step};
const char* responseModeTitle(ResponseMode m);

enum class ZTool { move, addZero, addPole };

struct TheoryPage {
    enum class Kind { filter, lessons, lesson, experiments };
    Kind kind = Kind::filter;
    std::string lessonID;
    static TheoryPage filter() { return {Kind::filter, {}}; }
    static TheoryPage lessons() { return {Kind::lessons, {}}; }
    static TheoryPage lesson(const std::string& id) { return {Kind::lesson, id}; }
    static TheoryPage experiments() { return {Kind::experiments, {}}; }
    bool operator==(const TheoryPage& o) const = default;
};

using ModelChange = unsigned;
namespace Change {
constexpr ModelChange filter = 1u << 0;      // the designed filter changed
constexpr ModelChange zeros = 1u << 1;       // FIR zeros finished computing
constexpr ModelChange source = 1u << 2;      // input source or its settings
constexpr ModelChange output = 1u << 3;      // listen / volume / filter on-off
constexpr ModelChange display = 1u << 4;     // axis, tabs, view options
constexpr ModelChange cursor = 1u << 5;      // hover frequency
constexpr ModelChange selection = 1u << 6;   // selected pole/zero or tool
constexpr ModelChange theory = 1u << 7;      // theory page
constexpr ModelChange banner = 1u << 8;
constexpr ModelChange sampleRate = 1u << 9;
constexpr ModelChange all = ~0u;
} // namespace Change

inline bool intersects(ModelChange a, ModelChange b) { return (a & b) != 0; }

struct Banner {
    enum class Style { info, warning, danger };
    std::string text;
    Style style = Style::info;
    std::optional<std::string> actionTitle;
    std::string id;
};

/// The whole state of the lab. Views observe it; the audio engine mirrors it.
class LabModel {
public:
    static const std::vector<double> sampleRates;

    LabModel();
    void start();

    LabAudio audio;
    /// The window, for dialogs.
    void* window = nullptr;

    // MARK: Observation
    void observe(std::function<void(ModelChange)> handler);
    void notify(ModelChange change);

    // MARK: Design
    const DesignSpec& spec() const { return spec_; }
    const DigitalFilter& filter() const { return filter_; }
    double fs() const { return fs_; }
    bool zerosPending() const { return zerosPending_; }
    /// Replaces the design. Rapid changes (a slider drag) collapse into one undo step.
    void setSpec(const DesignSpec& s, bool undoable = true);
    void updateSpec(const std::function<void(DesignSpec&)>& change);
    const FilterMeasurements& measurements();
    /// The design's poles/zeros become hand-editable items (switching to Pole-Zero mode).
    void convertToPoleZero();
    void applyPreset(PoleZeroPreset preset);
    void setItems(const std::vector<PZItem>& items, bool undoable = true);
    void deleteSelectedItem();

    // MARK: Undo
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }
    void undo();
    void redo();

    // MARK: Sample rate
    void setSampleRate(double newFs);

    // MARK: Source
    SourceKind source() const { return source_; }
    void setSource(SourceKind s);
    void openAudioFile();
    void loadFile(const std::wstring& path);

    // MARK: Settings mirrored to the audio engine
    double frequency() const { return frequency_; }
    void setFrequency(double v);
    double micGain() const { return micGain_; }
    void setMicGain(double v);
    bool listen() const { return listen_; }
    void setListen(bool v);
    double volume() const { return volume_; }
    void setVolume(double v);
    bool filterOn() const { return filterOn_; }
    void setFilterOn(bool v);
    bool paused() const { return paused_; }
    void setPaused(bool v);

    // MARK: Display state
    bool logAxis() const { return logAxis_; }
    void setLogAxis(bool v) { logAxis_ = v; notify(Change::display); }
    bool showHMap() const { return showHMap_; }
    void setShowHMap(bool v) { showHMap_ = v; notify(Change::display); }
    bool showPrediction() const { return showPrediction_; }
    void setShowPrediction(bool v) { showPrediction_ = v; notify(Change::display); }
    LiveView liveView() const { return liveView_; }
    void setLiveView(LiveView v) { liveView_ = v; notify(Change::display); }
    ResponseMode responseMode() const { return responseMode_; }
    void setResponseMode(ResponseMode v) { responseMode_ = v; notify(Change::display); }
    bool spectrogramShowsInput() const { return spectrogramShowsInput_; }
    void setSpectrogramShowsInput(bool v) { spectrogramShowsInput_ = v; notify(Change::display); }
    double scopeWindowMs() const { return scopeWindowMs_; }
    void setScopeWindowMs(double v) { scopeWindowMs_ = v; notify(Change::display); }
    bool hold() const { return hold_; }
    void setHold(bool v) { hold_ = v; notify(Change::display); }
    std::optional<double> cursorFrequency() const { return cursorFrequency_; }
    void setCursorFrequency(std::optional<double> v);
    std::optional<ItemID> selectedItem() const { return selectedItem_; }
    void setSelectedItem(std::optional<ItemID> v);
    ZTool zTool() const { return zTool_; }
    void setZTool(ZTool t) { zTool_ = t; notify(Change::selection); }
    const TheoryPage& theoryPage() const { return theoryPage_; }
    void setTheoryPage(const TheoryPage& p) { theoryPage_ = p; notify(Change::theory); }
    const std::optional<std::string>& activeExperiment() const { return activeExperiment_; }
    void setActiveExperiment(std::optional<std::string> e) { activeExperiment_ = std::move(e); notify(Change::theory); }

    // MARK: Banners
    const std::optional<Banner>& banner() const { return banner_; }
    void showBanner(const Banner& b);
    void dismissBanner();
    void performBannerAction();

    // MARK: Experiments (Lessons.cpp)
    void runExperiment(const std::string& id);

    void save();

private:
    void redesign();
    void updateStabilityBanner();
    void updateMicBanner();
    void scheduleSave();
    void restore();

    DesignSpec spec_;
    DigitalFilter filter_;
    double fs_ = 48000;
    bool zerosPending_ = false;
    std::optional<std::pair<int, FilterMeasurements>> cachedMeasurements_;
    int designGeneration_ = 0;
    std::shared_ptr<std::atomic<int>> rootGeneration_ = std::make_shared<std::atomic<int>>(0);
    std::vector<DesignSpec> undoStack_, redoStack_;
    double lastUndoRegistration_ = 0;

    SourceKind source_ = SourceKind::music;
    double frequency_ = 440;
    double micGain_ = 4;
    bool listen_ = false;
    double volume_ = 0.7;
    bool filterOn_ = true;
    bool paused_ = false;

    bool logAxis_ = true;
    bool showHMap_ = true;
    bool showPrediction_ = false;
    LiveView liveView_ = LiveView::spectrum;
    ResponseMode responseMode_ = ResponseMode::magnitude;
    bool spectrogramShowsInput_ = false;
    double scopeWindowMs_ = 10;
    bool hold_ = false;
    std::optional<double> cursorFrequency_;
    std::optional<ItemID> selectedItem_;
    ZTool zTool_ = ZTool::move;
    TheoryPage theoryPage_;
    std::optional<std::string> activeExperiment_;

    std::optional<Banner> banner_;
    std::set<std::string> dismissedBanners_;
    std::optional<std::string> micProblem_;

    std::vector<std::function<void(ModelChange)>> observers_;
    bool saveScheduled_ = false;
};
