#pragma once
#include "RTSupport.h"
#include <cstdint>
#include <vector>

/// What feeds the filter.
enum class SourceKind { music, musicHum, sine, square, sweep, whiteNoise, pinkNoise, clicks, microphone, file };
inline constexpr SourceKind kAllSources[] = {SourceKind::music,      SourceKind::musicHum,  SourceKind::sine,
                                             SourceKind::square,     SourceKind::sweep,     SourceKind::whiteNoise,
                                             SourceKind::pinkNoise,  SourceKind::clicks,    SourceKind::microphone,
                                             SourceKind::file};

const char* sourceTitle(SourceKind s);
inline bool sourceUsesFrequency(SourceKind s) { return s == SourceKind::sine || s == SourceKind::square; }
inline bool sourceUsesLoop(SourceKind s) {
    return s == SourceKind::music || s == SourceKind::musicHum || s == SourceKind::file;
}
inline bool sourceIsTone(SourceKind s) {
    return s == SourceKind::sine || s == SourceKind::square || s == SourceKind::sweep;
}

/// Generates the test signals. Lives on the audio thread.
class SourceGenerator {
public:
    explicit SourceGenerator(double fs) : fs(fs) {}

    const double fs;
    int loopPosition = 0;

    static constexpr double sweepStart = 20.0;
    static constexpr double sweepSeconds = 8.0;
    double sweepEnd() const { return fs * 0.45; }

    /// Fills x with n samples. Returns the instantaneous frequency for sine/square/sweep.
    double render(SourceKind kind, double frequency, const LoopBuffer* loop, float* x, int n);

private:
    double white();
    double phase_ = 0.0;
    double smoothedFrequency_ = 440.0;
    double sweepPhase_ = 0.0;
    double sweepTime_ = 0.0;
    uint64_t rng_ = 0x9E3779B97F4A7C15ull;
    double pink_[7] = {0, 0, 0, 0, 0, 0, 0};
    int clickCounter_ = 0;
    double humPhase_ = 0.0;
};

/// Synthesizes a 4-bar loop (drums, bass, pad, arpeggio) with energy across the whole
/// spectrum, so every kind of filter has something audible to work on.
std::vector<float> synthesizeMusicLoop(double fs);
