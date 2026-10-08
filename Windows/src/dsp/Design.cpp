#include "Design.h"
#include "Polynomial.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <sstream>

const char* methodTitle(DesignMethod m) {
    switch (m) {
    case DesignMethod::iir: return "IIR";
    case DesignMethod::fir: return "FIR";
    case DesignMethod::poleZero: return "Pole\xE2\x80\x93Zero";
    }
    return "";
}

const char* methodRawValue(DesignMethod m) {
    switch (m) {
    case DesignMethod::iir: return "iir";
    case DesignMethod::fir: return "fir";
    case DesignMethod::poleZero: return "poleZero";
    }
    return "";
}

ItemID newItemID() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1);
}

PZItem::PZItem(Kind k, Complex pos, std::optional<bool> isPaired) : id(newItemID()), kind(k) {
    bool pair = isPaired.has_value() ? *isPaired : !pos.isReal(1e-9);
    paired = pair;
    position = pair ? Complex(pos.re, std::fabs(pos.im)) : Complex(pos.re, 0);
}

PZItem PZItem::pole(double r, double f, double fs) { return PZItem(Kind::pole, Complex::polar(r, 2 * kPi * f / fs), true); }
PZItem PZItem::zero(double r, double f, double fs) { return PZItem(Kind::zero, Complex::polar(r, 2 * kPi * f / fs), true); }

DesignSpec DesignSpec::clamped(double fs) const {
    DesignSpec s = *this;
    double lo = 5.0, hi = fs / 2 * 0.98;
    s.order = std::min(std::max(s.order, orderMin), orderMax);
    s.taps = std::min(std::max(s.taps, tapMin), tapMax);
    if (s.method == DesignMethod::fir && firNeedsOddTaps(s.band) && s.taps % 2 == 0) s.taps += 1;
    s.f1 = std::min(std::max(s.f1, lo), hi);
    s.f2 = std::min(std::max(s.f2, lo), hi);
    if (bandIsBand(s.band)) {
        if (s.f2 < s.f1 * 1.02) {
            s.f2 = std::min(hi, s.f1 * 1.02);
            if (s.f2 < s.f1 * 1.02) s.f1 = s.f2 / 1.02;
        }
    }
    s.rippleDB = std::min(std::max(s.rippleDB, 0.01), 6.0);
    s.stopDB = std::min(std::max(s.stopDB, 10.0), 120.0);
    s.kaiserBeta = std::min(std::max(s.kaiserBeta, 0.0), 16.0);
    return s;
}

// MARK: - Persistence (a small line-based format; the Swift version stores JSON in UserDefaults)

static std::string hexDouble(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

std::string DesignSpec::serialize() const {
    std::ostringstream o;
    o << "method " << methodRawValue(method) << "\n";
    o << "band " << bandRawValue(band) << "\n";
    o << "family " << familyRawValue(family) << "\n";
    o << "order " << order << "\n";
    o << "f1 " << hexDouble(f1) << "\n";
    o << "f2 " << hexDouble(f2) << "\n";
    o << "rippleDB " << hexDouble(rippleDB) << "\n";
    o << "stopDB " << hexDouble(stopDB) << "\n";
    o << "taps " << taps << "\n";
    o << "window " << windowRawValue(window) << "\n";
    o << "kaiserBeta " << hexDouble(kaiserBeta) << "\n";
    for (const auto& it : items) {
        o << "item " << (it.kind == PZItem::Kind::pole ? "pole" : "zero") << " " << hexDouble(it.position.re) << " "
          << hexDouble(it.position.im) << " " << (it.paired ? 1 : 0) << "\n";
    }
    if (presetName) o << "preset " << *presetName << "\n";
    return o.str();
}

std::optional<DesignSpec> DesignSpec::deserialize(const std::string& text) {
    DesignSpec s;
    std::istringstream in(text);
    std::string line;
    bool any = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        std::string key = line.substr(0, sp), val = line.substr(sp + 1);
        any = true;
        if (key == "method") {
            for (auto m : kAllMethods) if (val == methodRawValue(m)) s.method = m;
        } else if (key == "band") {
            for (auto b : kAllBandTypes) if (val == bandRawValue(b)) s.band = b;
        } else if (key == "family") {
            for (auto f : kAllFamilies) if (val == familyRawValue(f)) s.family = f;
        } else if (key == "order") {
            s.order = std::atoi(val.c_str());
        } else if (key == "f1") {
            s.f1 = std::strtod(val.c_str(), nullptr);
        } else if (key == "f2") {
            s.f2 = std::strtod(val.c_str(), nullptr);
        } else if (key == "rippleDB") {
            s.rippleDB = std::strtod(val.c_str(), nullptr);
        } else if (key == "stopDB") {
            s.stopDB = std::strtod(val.c_str(), nullptr);
        } else if (key == "taps") {
            s.taps = std::atoi(val.c_str());
        } else if (key == "window") {
            for (auto w : kAllWindows) if (val == windowRawValue(w)) s.window = w;
        } else if (key == "kaiserBeta") {
            s.kaiserBeta = std::strtod(val.c_str(), nullptr);
        } else if (key == "item") {
            std::istringstream is(val);
            std::string kind;
            double re = 0, im = 0;
            int paired = 0;
            if (is >> kind >> re >> im >> paired) {
                PZItem item(kind == "pole" ? PZItem::Kind::pole : PZItem::Kind::zero, Complex(re, im), paired != 0);
                s.items.push_back(item);
            }
        } else if (key == "preset") {
            s.presetName = val;
        }
    }
    if (!any) return std::nullopt;
    if (!std::isfinite(s.f1) || !std::isfinite(s.f2) || !std::isfinite(s.rippleDB) || !std::isfinite(s.stopDB) ||
        !std::isfinite(s.kaiserBeta))
        return std::nullopt;
    return s;
}

// MARK: - Building

DigitalFilter buildFilter(const DesignSpec& rawSpec, double fs) {
    DesignSpec spec = rawSpec.clamped(fs);
    switch (spec.method) {
    case DesignMethod::iir: {
        ZPK zpk = designIIR(spec.band, spec.family, spec.order, spec.f1, spec.f2, spec.rippleDB, spec.stopDB, fs);
        DigitalFilter f;
        f.structure = FilterStructure::iir(zpkToSOS(zpk));
        f.zpk = std::move(zpk);
        f.sampleRate = fs;
        return f;
    }
    case DesignMethod::fir: {
        auto h = designFIR(spec.band, spec.taps, spec.f1, spec.f2, spec.window, spec.kaiserBeta, fs);
        DigitalFilter f;
        f.zpk = ZPK{{}, std::vector<Complex>(h.size() - 1, Complex()), h[0]};
        f.structure = FilterStructure::fir(std::move(h));
        f.sampleRate = fs;
        return f;
    }
    case DesignMethod::poleZero: return buildPoleZeroFilter(spec.items, fs);
    }
    return DigitalFilter::passthrough(fs);
}

std::vector<Complex> firZeros(const std::vector<double>& h) {
    // Leading/trailing taps that are zero (to rounding) are just delay; dropping them
    // avoids roots at 0 and enormous spurious roots.
    double mx = 0;
    for (double v : h) mx = std::max(mx, std::fabs(v));
    double tiny = 1e-12 * mx;
    std::vector<double> taps = h;
    while (taps.size() > 1 && std::fabs(taps.front()) <= tiny) taps.erase(taps.begin());
    while (taps.size() > 1 && std::fabs(taps.back()) <= tiny) taps.pop_back();
    return polyRoots(taps);
}

DigitalFilter buildPoleZeroFilter(const std::vector<PZItem>& items, double fs) {
    std::vector<Complex> zeros, poles;
    for (const auto& it : items) {
        auto r = it.roots();
        if (it.kind == PZItem::Kind::zero) zeros.insert(zeros.end(), r.begin(), r.end());
    }
    for (const auto& it : items) {
        auto r = it.roots();
        if (it.kind == PZItem::Kind::pole) poles.insert(poles.end(), r.begin(), r.end());
    }
    if (zeros.size() > poles.size()) {
        poles.resize(zeros.size(), Complex());
    } else if (poles.size() > zeros.size()) {
        zeros.resize(poles.size(), Complex());
    }
    ZPK zpk{zeros, poles, 1};
    DigitalFilter filter;
    filter.zpk = zpk;
    filter.structure = FilterStructure::iir(zpkToSOS(zpk));
    filter.sampleRate = fs;
    // Normalise the peak of |H| on the unit circle to 1.
    double peak = 0.0;
    const int n = 4096;
    for (int i = 0; i <= n; i++) {
        // Dense near DC so very low notches/resonances are not missed.
        double f = fs / 2 * std::pow(double(i) / double(n), 2);
        peak = std::max(peak, filter.response(f).magnitude());
    }
    for (const auto& p : poles) {
        if (!(p.magnitude() > 0.5)) continue;
        double f = std::fabs(p.phase()) / (2 * kPi) * fs;
        peak = std::max(peak, filter.response(f).magnitude());
    }
    if (std::isfinite(peak) && peak > 1e-12) {
        zpk.gain = 1 / peak;
        filter.zpk = zpk;
        filter.structure = FilterStructure::iir(zpkToSOS(zpk));
    }
    return filter;
}

const char* presetTitle(PoleZeroPreset p) {
    switch (p) {
    case PoleZeroPreset::resonator: return "Resonator (1 kHz ping)";
    case PoleZeroPreset::notch: return "Notch (1 kHz)";
    case PoleZeroPreset::humRemover: return "Mains hum remover (50 Hz)";
    case PoleZeroPreset::comb: return "Comb filter (metallic)";
    case PoleZeroPreset::allpass: return "All-pass (phase only)";
    case PoleZeroPreset::movingAverage: return "Moving average (8 samples)";
    case PoleZeroPreset::dcBlocker: return "DC blocker";
    }
    return "";
}

const char* presetSummary(PoleZeroPreset p) {
    switch (p) {
    case PoleZeroPreset::resonator:
        return "A pole pair close to the unit circle makes a sharp peak; zeros at DC and Nyquist.";
    case PoleZeroPreset::notch:
        return "Zeros on the circle remove one frequency completely; nearby poles keep the notch narrow.";
    case PoleZeroPreset::humRemover:
        return "Notches at 50, 100, 150, 200 and 250 Hz remove Sri Lankan mains hum and its harmonics.";
    case PoleZeroPreset::comb:
        return "Poles evenly spaced round the circle: y[n] = x[n] + 0.8\xC2\xB7y[n\xE2\x88\x92" "16]. Peaks every fs/16.";
    case PoleZeroPreset::allpass:
        return "Each zero mirrors its pole across the circle (1/p*), so |H| is flat but the phase bends.";
    case PoleZeroPreset::movingAverage:
        return "The average of the last 8 samples: zeros at every multiple of fs/8 except DC.";
    case PoleZeroPreset::dcBlocker: return "A zero at z = 1 kills DC; a pole just inside keeps everything else.";
    }
    return "";
}

std::vector<PZItem> presetItems(PoleZeroPreset p, double fs) {
    using K = PZItem::Kind;
    switch (p) {
    case PoleZeroPreset::resonator:
        return {PZItem::pole(0.995, 1000, fs), PZItem(K::zero, Complex(1)), PZItem(K::zero, Complex(-1))};
    case PoleZeroPreset::notch: return {PZItem::zero(1, 1000, fs), PZItem::pole(0.97, 1000, fs)};
    case PoleZeroPreset::humRemover: {
        std::vector<PZItem> items;
        for (double f : {50.0, 100.0, 150.0, 200.0, 250.0}) {
            // Pole radius sets the notch width: about (1 - r) fs/pi Hz.
            double r = 1 - 4 * kPi / fs;
            items.push_back(PZItem::zero(1, f, fs));
            items.push_back(PZItem::pole(r, f, fs));
        }
        return items;
    }
    case PoleZeroPreset::comb: {
        int d = 16;
        double g = 0.8;
        double r = std::pow(g, 1 / double(d));
        std::vector<PZItem> items{PZItem(K::pole, Complex(r)), PZItem(K::pole, Complex(-r))};
        for (int k = 1; k < d / 2; k++) {
            items.push_back(PZItem(K::pole, Complex::polar(r, 2 * kPi * double(k) / double(d)), true));
        }
        return items;
    }
    case PoleZeroPreset::allpass: {
        Complex pp = Complex::polar(0.9, 2 * kPi * 2000 / fs);
        return {PZItem(K::pole, pp, true), PZItem(K::zero, Complex::polar(1 / 0.9, pp.phase()), true)};
    }
    case PoleZeroPreset::movingAverage: {
        std::vector<PZItem> items{PZItem(K::zero, Complex(-1))};
        for (int k = 1; k <= 3; k++) items.push_back(PZItem(K::zero, Complex::polar(1, 2 * kPi * double(k) / 8), true));
        return items;
    }
    case PoleZeroPreset::dcBlocker: return {PZItem(K::zero, Complex(1)), PZItem(K::pole, Complex(0.995))};
    }
    return {};
}

std::optional<PoleZeroPreset> presetNamed(const std::string& title) {
    for (auto p : kAllPresets)
        if (title == presetTitle(p)) return p;
    return std::nullopt;
}

std::vector<PZItem> editableItems(const ZPK& zpk) {
    std::vector<PZItem> items;
    std::pair<PZItem::Kind, const std::vector<Complex>*> groups[] = {{PZItem::Kind::zero, &zpk.zeros},
                                                                    {PZItem::Kind::pole, &zpk.poles}};
    for (const auto& [kind, roots] : groups) {
        std::vector<Complex> pairs;
        std::vector<double> reals;
        splitConjugates(*roots, pairs, reals);
        for (const auto& p : pairs)
            if (p.magnitude() > 1e-9) items.push_back(PZItem(kind, p, true));
        for (double r : reals)
            if (std::fabs(r) > 1e-9) items.push_back(PZItem(kind, Complex(r), false));
    }
    return items;
}
