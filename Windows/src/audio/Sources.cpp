#include "Sources.h"
#include "../dsp/Complex.h"
#include <cmath>

const char* sourceTitle(SourceKind s) {
    switch (s) {
    case SourceKind::music: return "Music loop";
    case SourceKind::musicHum: return "Music + 50 Hz hum";
    case SourceKind::sine: return "Sine wave";
    case SourceKind::square: return "Square wave";
    case SourceKind::sweep: return "Frequency sweep";
    case SourceKind::whiteNoise: return "White noise";
    case SourceKind::pinkNoise: return "Pink noise";
    case SourceKind::clicks: return "Clicks (impulses)";
    case SourceKind::microphone: return "Microphone";
    case SourceKind::file: return "Audio file\xE2\x80\xA6";
    }
    return "";
}

double SourceGenerator::white() {
    // xorshift64*
    rng_ ^= rng_ >> 12;
    rng_ ^= rng_ << 25;
    rng_ ^= rng_ >> 27;
    uint64_t v = rng_ * 2685821657736338717ull;
    return double(v >> 11) / double(uint64_t(1) << 53) * 2 - 1;
}

static inline double polyBLEP(double t, double dt) {
    if (t < dt) {
        double x = t / dt;
        return x + x - x * x - 1;
    } else if (t > 1 - dt) {
        double x = (t - 1) / dt;
        return x * x + x + x + 1;
    }
    return 0;
}

double SourceGenerator::render(SourceKind kind, double frequency, const LoopBuffer* loop, float* x, int n) {
    const double glide = 1 - std::exp(-1 / (0.02 * fs));
    switch (kind) {
    case SourceKind::sine:
        for (int i = 0; i < n; i++) {
            smoothedFrequency_ += (frequency - smoothedFrequency_) * glide;
            phase_ += smoothedFrequency_ / fs;
            if (phase_ >= 1) phase_ -= 1;
            x[i] = float(0.35 * std::sin(2 * kPi * phase_));
        }
        return smoothedFrequency_;
    case SourceKind::square:
        for (int i = 0; i < n; i++) {
            smoothedFrequency_ += (frequency - smoothedFrequency_) * glide;
            double dt = smoothedFrequency_ / fs;
            phase_ += dt;
            if (phase_ >= 1) phase_ -= 1;
            double v = phase_ < 0.5 ? 1.0 : -1.0;
            v += polyBLEP(phase_, dt);
            double t2 = phase_ + 0.5;
            if (t2 >= 1) t2 -= 1;
            v -= polyBLEP(t2, dt);
            x[i] = float(0.3 * v);
        }
        return smoothedFrequency_;
    case SourceKind::sweep: {
        double ratio = sweepEnd() / sweepStart;
        double f = sweepStart;
        for (int i = 0; i < n; i++) {
            f = sweepStart * std::pow(ratio, sweepTime_ / sweepSeconds);
            sweepPhase_ += f / fs;
            if (sweepPhase_ >= 1) sweepPhase_ -= 1;
            x[i] = float(0.35 * std::sin(2 * kPi * sweepPhase_));
            sweepTime_ += 1 / fs;
            if (sweepTime_ >= sweepSeconds) sweepTime_ = 0;
        }
        return f;
    }
    case SourceKind::whiteNoise:
        for (int i = 0; i < n; i++) x[i] = float(0.3 * white());
        break;
    case SourceKind::pinkNoise:
        // Paul Kellet's pink-noise filter.
        for (int i = 0; i < n; i++) {
            double w = white() * 0.08;
            pink_[0] = 0.99886 * pink_[0] + w * 0.0555179;
            pink_[1] = 0.99332 * pink_[1] + w * 0.0750759;
            pink_[2] = 0.96900 * pink_[2] + w * 0.1538520;
            pink_[3] = 0.86650 * pink_[3] + w * 0.3104856;
            pink_[4] = 0.55000 * pink_[4] + w * 0.5329522;
            pink_[5] = -0.7616 * pink_[5] - w * 0.0168980;
            double v = pink_[0] + pink_[1] + pink_[2] + pink_[3] + pink_[4] + pink_[5] + pink_[6] + w * 0.5362;
            pink_[6] = w * 0.115926;
            x[i] = float(v);
        }
        break;
    case SourceKind::clicks: {
        int period = int(fs / 2);
        for (int i = 0; i < n; i++) {
            x[i] = clickCounter_ == 0 ? 0.8f : 0.0f;
            clickCounter_ += 1;
            if (clickCounter_ >= period) clickCounter_ = 0;
        }
        break;
    }
    case SourceKind::music:
    case SourceKind::musicHum:
    case SourceKind::file:
        if (loop) {
            const float* s = loop->samples.data();
            int count = loop->count();
            for (int i = 0; i < n; i++) {
                if (loopPosition >= count) loopPosition = 0;
                x[i] = s[loopPosition];
                loopPosition += 1;
            }
        } else {
            for (int i = 0; i < n; i++) x[i] = 0;
        }
        if (kind == SourceKind::musicHum) {
            // 50 Hz mains hum with harmonics up to 250 Hz.
            static const double amps[] = {0.10, 0.05, 0.07, 0.03, 0.05};
            for (int i = 0; i < n; i++) {
                humPhase_ += 50 / fs;
                if (humPhase_ >= 1) humPhase_ -= 1;
                double h = 0.0;
                for (int k = 0; k < 5; k++) h += amps[k] * std::sin(2 * kPi * humPhase_ * double(k + 1));
                x[i] += float(h);
            }
        }
        break;
    case SourceKind::microphone:
        for (int i = 0; i < n; i++) x[i] = 0;
        break;
    }
    return 0;
}

// MARK: - Music loop

std::vector<float> synthesizeMusicLoop(double fs) {
    const double bpm = 112.0;
    const double beat = 60 / bpm;
    const int bars = 4;
    const int total = int(std::round(double(bars * 4) * beat * fs));
    std::vector<double> out(size_t(total), 0.0);
    uint64_t seed = 12345;
    auto noise = [&]() {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return double(seed >> 11) / double(uint64_t(1) << 53) * 2 - 1;
    };
    auto add = [&](int start, int i, double v) { out[size_t((start + i) % total)] += v; };
    auto midi = [](int n) { return 440 * std::pow(2.0, double(n - 69) / 12); };
    auto blep = [](double t, double dt) {
        if (t < dt) {
            double x = t / dt;
            return x + x - x * x - 1;
        }
        if (t > 1 - dt) {
            double x = (t - 1) / dt;
            return x * x + x + x + 1;
        }
        return 0.0;
    };
    auto at = [&](double beats) { return int(std::round(beats * beat * fs)); };

    auto kick = [&](int s) {
        double ph = 0.0;
        int len = int(0.45 * fs);
        for (int i = 0; i < len; i++) {
            double t = double(i) / fs;
            double f = 48 + 100 * std::exp(-t / 0.03);
            ph += 2 * kPi * f / fs;
            double v = 0.95 * std::sin(ph) * std::exp(-t / 0.2);
            if (t < 0.003) v += 0.25 * noise() * (1 - t / 0.003);
            add(s, i, v);
        }
    };
    auto snare = [&](int s) {
        double prev = 0.0, ph = 0.0;
        int len = int(0.3 * fs);
        for (int i = 0; i < len; i++) {
            double t = double(i) / fs;
            double n = noise();
            double hp = n - prev;
            prev = n;
            ph += 2 * kPi * 185 / fs;
            add(s, i, 0.32 * hp * std::exp(-t / 0.07) + 0.35 * std::sin(ph) * std::exp(-t / 0.045));
        }
    };
    auto hat = [&](int s, bool open, double level) {
        double p1 = 0.0, p2 = 0.0;
        double decay = open ? 0.16 : 0.035;
        int len = int((open ? 0.4 : 0.1) * fs);
        for (int i = 0; i < len; i++) {
            double t = double(i) / fs;
            double n = noise();
            double d1 = n - p1;
            p1 = n;
            double d2 = d1 - p2;
            p2 = d1;
            add(s, i, level * 0.5 * d2 * std::exp(-t / decay));
        }
    };
    auto bass = [&](int note, int s, double dur) {
        double f = midi(note), dt = f / fs;
        double ph = 0.0, lp = 0.0;
        int len = int((dur + 0.05) * fs);
        for (int i = 0; i < len; i++) {
            double t = double(i) / fs;
            ph += dt;
            if (ph >= 1) ph -= 1;
            double saw = 2 * ph - 1 - blep(ph, dt);
            double cutoff = 220 + 900 * std::exp(-t / 0.08);
            lp += (1 - std::exp(-2 * kPi * cutoff / fs)) * (saw - lp);
            double env = std::min(1.0, t / 0.004) * (t < dur ? 1 : std::max(0.0, 1 - (t - dur) / 0.05));
            add(s, i, 0.42 * lp * env);
        }
    };
    auto pad = [&](const std::vector<int>& notes, int s, double dur) {
        for (int note : notes) {
            for (double detune : {-0.004, 0.004}) {
                double f = midi(note) * (1 + detune), dt = f / fs;
                double ph = double(note % 7) / 7, l1 = 0.0, l2 = 0.0;
                int len = int((dur + 0.5) * fs);
                double a = 1 - std::exp(-2 * kPi * 1600 / fs);
                for (int i = 0; i < len; i++) {
                    double t = double(i) / fs;
                    ph += dt;
                    if (ph >= 1) ph -= 1;
                    double saw = 2 * ph - 1 - blep(ph, dt);
                    l1 += a * (saw - l1);
                    l2 += a * (l1 - l2);
                    double env = std::min(1.0, t / 0.25) * (t < dur ? 1 : std::max(0.0, 1 - (t - dur) / 0.5));
                    add(s, i, 0.055 * l2 * env);
                }
            }
        }
    };
    auto pluck = [&](int note, int s) {
        double f = midi(note), dt = f / fs;
        double ph = 0.0, lp = 0.0;
        double a = 1 - std::exp(-2 * kPi * 4200 / fs);
        int len = int(0.3 * fs);
        for (int i = 0; i < len; i++) {
            double t = double(i) / fs;
            ph += dt;
            if (ph >= 1) ph -= 1;
            double v = ph < 0.3 ? 1.0 : -1.0;
            v += blep(ph, dt);
            double t2 = ph + 0.7;
            if (t2 >= 1) t2 -= 1;
            v -= blep(t2, dt);
            lp += a * (v - lp);
            add(s, i, 0.075 * lp * std::exp(-t / 0.09));
        }
    };

    // Am - F - C - G
    struct Chord {
        int bass;
        std::vector<int> chord;
    };
    const Chord progression[] = {{45, {57, 60, 64}}, {41, {53, 57, 60}}, {48, {55, 60, 64}}, {43, {55, 59, 62}}};
    for (int bar = 0; bar < bars; bar++) {
        double b0 = double(bar * 4);
        for (double k : {0.0, 2.0, 2.5}) kick(at(b0 + k));
        for (double k : {1.0, 3.0}) snare(at(b0 + k));
        for (int e = 0; e < 8; e++) {
            bool open = e == 7;
            hat(at(b0 + double(e) * 0.5), open, e % 2 == 0 ? 0.8 : 0.5);
        }
        int root = progression[bar].bass;
        const auto& chord = progression[bar].chord;
        for (int e = 0; e < 8; e++) {
            if (e == 3) continue;
            int note = e % 4 == 2 ? root + 12 : root;
            bass(note, at(b0 + double(e) * 0.5), beat * 0.42);
        }
        pad(chord, at(b0), beat * 4 - 0.1);
        const int pattern[] = {0, 1, 2, 1, 0, 2, 1, 2};
        for (int s = 0; s < 16; s++) {
            int note = chord[size_t(pattern[s % 8]) % chord.size()] + 12 + (s >= 8 && s % 4 == 3 ? 12 : 0);
            pluck(note, at(b0 + double(s) * 0.25));
        }
    }
    double peak = 0;
    for (double v : out) peak = std::max(peak, std::fabs(v));
    double scale = 0.6 / std::max(peak, 1e-9);
    std::vector<float> result(out.size());
    for (size_t i = 0; i < out.size(); i++) result[i] = float(out[i] * scale);
    return result;
}
