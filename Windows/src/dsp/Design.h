#pragma once
#include "FIRDesign.h"
#include "Filter.h"
#include "IIRDesign.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class DesignMethod { iir, fir, poleZero };
inline constexpr DesignMethod kAllMethods[] = {DesignMethod::iir, DesignMethod::fir, DesignMethod::poleZero};
const char* methodTitle(DesignMethod m);
const char* methodRawValue(DesignMethod m);

/// Unique identifier for a hand-placed pole or zero (the Swift version uses UUIDs).
using ItemID = uint64_t;
ItemID newItemID();

/// A pole or zero placed by hand. A paired item stands for p and its conjugate p*.
struct PZItem {
    enum class Kind { pole, zero };
    ItemID id = newItemID();
    Kind kind = Kind::pole;
    Complex position;
    bool paired = false;

    PZItem() = default;
    PZItem(Kind k, Complex pos, std::optional<bool> isPaired = std::nullopt);
    static PZItem pole(double r, double f, double fs);
    static PZItem zero(double r, double f, double fs);

    std::vector<Complex> roots() const {
        if (paired) return {position, position.conj()};
        return {position};
    }
    bool operator==(const PZItem& o) const = default;
};

/// Everything the user chose in the design panel.
struct DesignSpec {
    DesignMethod method = DesignMethod::iir;
    BandType band = BandType::lowpass;
    IIRFamily family = IIRFamily::butterworth;
    int order = 4;
    double f1 = 1000;
    double f2 = 4000;
    double rippleDB = 1;
    double stopDB = 60;
    int taps = 63;
    WindowType window = WindowType::hamming;
    double kaiserBeta = 8;
    std::vector<PZItem> items;
    std::optional<std::string> presetName;

    static constexpr int orderMin = 1, orderMax = 12;
    static constexpr int tapMin = 3, tapMax = 255;

    /// Keeps frequencies inside (0, Nyquist) and band edges in order.
    DesignSpec clamped(double fs) const;
    bool operator==(const DesignSpec& o) const = default;

    std::string serialize() const;
    static std::optional<DesignSpec> deserialize(const std::string& text);
};

/// Builds the filter for a spec. FIR zeros are left empty here because finding them is
/// slow for long filters; use `firZeros` (off the main thread) to fill them in.
DigitalFilter buildFilter(const DesignSpec& rawSpec, double fs);

/// The zeros of an FIR filter (roots of its tap polynomial).
std::vector<Complex> firZeros(const std::vector<double>& h);

/// Hand-placed poles and zeros, balanced with poles/zeros at the origin so the filter is
/// causal, and scaled so the loudest frequency sits at 0 dB.
DigitalFilter buildPoleZeroFilter(const std::vector<PZItem>& items, double fs);

/// Hand-made filters that show one idea each.
enum class PoleZeroPreset { resonator, notch, humRemover, comb, allpass, movingAverage, dcBlocker };
inline constexpr PoleZeroPreset kAllPresets[] = {PoleZeroPreset::resonator, PoleZeroPreset::notch, PoleZeroPreset::humRemover,
                                                 PoleZeroPreset::comb, PoleZeroPreset::allpass, PoleZeroPreset::movingAverage,
                                                 PoleZeroPreset::dcBlocker};
const char* presetTitle(PoleZeroPreset p);
const char* presetSummary(PoleZeroPreset p);
std::vector<PZItem> presetItems(PoleZeroPreset p, double fs);
std::optional<PoleZeroPreset> presetNamed(const std::string& title);

/// Turns any filter's poles and zeros into editable items (origin roots are dropped,
/// because the balancing step adds them back).
std::vector<PZItem> editableItems(const ZPK& zpk);
