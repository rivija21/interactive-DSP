#include "Export.h"
#include "../audio/AudioFile.h"
#include "../audio/FilterKernel.h"
#include "../platform/Dialogs.h"
#include "../util/Format.h"
#include "../util/Text.h"
#include "LabModel.h"
#include <windows.h>
#include <cmath>

const char* languageTitle(CodeLanguage l) {
    switch (l) {
    case CodeLanguage::matlab: return "MATLAB";
    case CodeLanguage::python: return "Python (SciPy)";
    case CodeLanguage::c: return "C";
    }
    return "";
}

const char* languageRawValue(CodeLanguage l) {
    switch (l) {
    case CodeLanguage::matlab: return "matlab";
    case CodeLanguage::python: return "python";
    case CodeLanguage::c: return "c";
    }
    return "";
}

std::string ordinal(int n) {
    const char* suffix;
    int h = n % 100, t = n % 10;
    if (h >= 11 && h <= 13) suffix = "th";
    else if (t == 1) suffix = "st";
    else if (t == 2) suffix = "nd";
    else if (t == 3) suffix = "rd";
    else suffix = "th";
    return std::to_string(n) + suffix;
}

std::string filterTitle(const DesignSpec& spec) {
    switch (spec.method) {
    case DesignMethod::iir:
        return ordinal(spec.order) + "-order " + familyTitle(spec.family) + " " + lowercased(bandTitle(spec.band));
    case DesignMethod::fir:
        return std::to_string(spec.taps) + "-tap FIR " + lowercased(bandTitle(spec.band)) + " (" + windowTitle(spec.window) +
               " window)";
    case DesignMethod::poleZero: return spec.presetName ? *spec.presetName : "Hand-placed poles and zeros";
    }
    return "";
}

std::string filterSubtitle(const DesignSpec& spec, double fs) {
    std::string rate = "fs = " + formatHz(fs);
    bool isBand = bandIsBand(spec.band);
    std::string edges = isBand ? formatHz(spec.f1) + " – " + formatHz(spec.f2) : formatHz(spec.f1);
    switch (spec.method) {
    case DesignMethod::iir: {
        std::string s = std::string(isBand ? "Band edges" : "Cutoff") + " " + edges + " (" + familyCutoffMeaning(spec.family) + ")";
        if (familyUsesPassbandRipple(spec.family)) s += strf(" · ripple %.2f dB", spec.rippleDB);
        if (familyUsesStopbandAttenuation(spec.family)) s += strf(" · stopband %.0f dB", spec.stopDB);
        return s + " · " + rate;
    }
    case DesignMethod::fir: return std::string(isBand ? "Band edges" : "Cutoff") + " " + edges + " · " + rate;
    case DesignMethod::poleZero: return "Gain scaled so the peak is 0 dB · " + rate;
    }
    return "";
}

static std::string num(double v) {
    if (v == 0) return "0";
    return strf("%.17g", v);
}

static std::string complexList(const std::vector<Complex>& zs, bool python) {
    const char* j = python ? "j" : "i";
    std::vector<std::string> parts;
    for (const auto& z : zs) {
        if (z.isReal(1e-12)) parts.push_back(num(z.re));
        else parts.push_back(num(z.re) + (z.im < 0 ? "-" : "+") + num(std::fabs(z.im)) + j);
    }
    return join(parts, ", ");
}

static std::vector<std::vector<double>> sosRows(const DigitalFilter& filter) {
    std::vector<std::vector<double>> rows;
    if (filter.structure.isFIR) return rows;
    for (const auto& s : filter.structure.sos) rows.push_back({s.b0, s.b1, s.b2, 1, s.a1, s.a2});
    return rows;
}

static std::string joinNums(const std::vector<double>& v, const std::string& sep) {
    std::vector<std::string> parts;
    for (double x : v) parts.push_back(num(x));
    return join(parts, sep);
}

static std::string matlabCode(const DesignSpec& spec, const DigitalFilter& filter, const std::string& header, const std::string& fs) {
    std::string out = "% " + header + "\nfs = " + fs + ";\n";
    std::string wn = bandIsBand(spec.band) ? "[" + num(spec.f1) + " " + num(spec.f2) + "]/(fs/2)" : num(spec.f1) + "/(fs/2)";
    std::string type;
    switch (spec.band) {
    case BandType::lowpass: type = "'low'"; break;
    case BandType::highpass: type = "'high'"; break;
    case BandType::bandpass: type = "'bandpass'"; break;
    case BandType::bandstop: type = "'stop'"; break;
    }
    switch (spec.method) {
    case DesignMethod::iir: {
        std::string call;
        std::string order = std::to_string(spec.order);
        switch (spec.family) {
        case IIRFamily::butterworth: call = "butter(" + order + ", " + wn + ", " + type + ")"; break;
        case IIRFamily::chebyshev1: call = "cheby1(" + order + ", " + num(spec.rippleDB) + ", " + wn + ", " + type + ")"; break;
        case IIRFamily::chebyshev2: call = "cheby2(" + order + ", " + num(spec.stopDB) + ", " + wn + ", " + type + ")"; break;
        case IIRFamily::elliptic:
            call = "ellip(" + order + ", " + num(spec.rippleDB) + ", " + num(spec.stopDB) + ", " + wn + ", " + type + ")";
            break;
        case IIRFamily::bessel: break;
        }
        if (!call.empty()) {
            out += "[z, p, k] = " + call + ";\nsos = zp2sos(z, p, k);   % rows: b0 b1 b2 a0 a1 a2\n\n";
            out += "% The same filter as designed by Filter Lab (zp2sos may order the sections differently):\n";
        } else {
            out += "% MATLAB's besself() is analog-only, so here are Filter Lab's digital coefficients\n";
            out += "% (Bessel prototype normalised like SciPy's norm='phase', then bilinear transform):\n";
        }
        std::vector<std::string> rows;
        for (const auto& r : sosRows(filter)) rows.push_back("    " + joinNums(r, " ") + ";");
        out += "sos_lab = [\n" + join(rows, "\n") + "\n];\n";
        if (call.empty()) out += "sos = sos_lab;\n";
        out += "\nfreqz(sos, 4096, fs);   % plot the response\n% y = sosfilt(sos, x);  % filter a signal x\n";
        break;
    }
    case DesignMethod::fir: {
        if (!filter.structure.isFIR) return out;
        const auto& h = filter.structure.taps;
        std::string n = std::to_string(h.size());
        std::string window;
        switch (spec.window) {
        case WindowType::rectangular: window = "rectwin(" + n + ")"; break;
        case WindowType::hann: window = "hann(" + n + ")"; break;
        case WindowType::hamming: window = "hamming(" + n + ")"; break;
        case WindowType::blackman: window = "blackman(" + n + ")"; break;
        case WindowType::kaiser: window = "kaiser(" + n + ", " + num(spec.kaiserBeta) + ")"; break;
        }
        out += "b = fir1(" + std::to_string(h.size() - 1) + ", " + wn + ", " + type + ", " + window + ");   % " + n + " taps\n\n";
        out += "% Filter Lab's taps (identical to fir1 above):\nb_lab = [" + joinNums(h, " ") + "];\n";
        out += "\nfreqz(b, 1, 4096, fs);\n% y = filter(b, 1, x);\n";
        break;
    }
    case DesignMethod::poleZero:
        out += "z = [" + complexList(filter.zpk.zeros, false) + "].';\n";
        out += "p = [" + complexList(filter.zpk.poles, false) + "].';\n";
        out += "k = " + num(filter.zpk.gain) + ";\n";
        out += "sos = zp2sos(z, p, k);\n\nzplane(z, p);\nfigure; freqz(sos, 4096, fs);\n% y = sosfilt(sos, x);\n";
        break;
    }
    return out;
}

static std::string pythonCode(const DesignSpec& spec, const DigitalFilter& filter, const std::string& header, const std::string& fs) {
    std::string out = "# " + header + "\nimport numpy as np\nfrom scipy import signal\n\nfs = " + fs + "\n";
    std::string wn = bandIsBand(spec.band) ? "[" + num(spec.f1) + ", " + num(spec.f2) + "]" : num(spec.f1);
    std::string btype = std::string("'") + bandRawValue(spec.band) + "'";
    switch (spec.method) {
    case DesignMethod::iir: {
        std::string order = std::to_string(spec.order), call;
        std::string tail = ", btype=" + btype + ", fs=fs, output='sos')";
        switch (spec.family) {
        case IIRFamily::butterworth: call = "signal.butter(" + order + ", " + wn + tail; break;
        case IIRFamily::chebyshev1: call = "signal.cheby1(" + order + ", " + num(spec.rippleDB) + ", " + wn + tail; break;
        case IIRFamily::chebyshev2: call = "signal.cheby2(" + order + ", " + num(spec.stopDB) + ", " + wn + tail; break;
        case IIRFamily::elliptic:
            call = "signal.ellip(" + order + ", " + num(spec.rippleDB) + ", " + num(spec.stopDB) + ", " + wn + tail;
            break;
        case IIRFamily::bessel:
            call = "signal.bessel(" + order + ", " + wn + ", btype=" + btype + ", norm='phase', fs=fs, output='sos')";
            break;
        }
        out += "sos = " + call + "\n\n";
        out += "# The same filter as designed by Filter Lab (sections may be ordered differently):\n";
        std::vector<std::string> rows;
        for (const auto& r : sosRows(filter)) rows.push_back("    [" + joinNums(r, ", ") + "],");
        out += "sos_lab = np.array([\n" + join(rows, "\n") + "\n])\n";
        out += "\nw, h = signal.sosfreqz(sos, worN=4096, fs=fs)\n# y = signal.sosfilt(sos, x)\n";
        break;
    }
    case DesignMethod::fir: {
        if (!filter.structure.isFIR) return out;
        const auto& h = filter.structure.taps;
        std::string window;
        switch (spec.window) {
        case WindowType::rectangular: window = "'boxcar'"; break;
        case WindowType::hann: window = "'hann'"; break;
        case WindowType::hamming: window = "'hamming'"; break;
        case WindowType::blackman: window = "'blackman'"; break;
        case WindowType::kaiser: window = "('kaiser', " + num(spec.kaiserBeta) + ")"; break;
        }
        out += "h = signal.firwin(" + std::to_string(h.size()) + ", " + wn + ", window=" + window + ", pass_zero=" + btype +
               ", fs=fs)\n\n";
        out += "# Filter Lab's taps (identical to firwin above):\nh_lab = np.array([" + joinNums(h, ", ") + "])\n";
        out += "\nw, H = signal.freqz(h, worN=4096, fs=fs)\n# y = signal.lfilter(h, 1, x)\n";
        break;
    }
    case DesignMethod::poleZero:
        out += "z = np.array([" + complexList(filter.zpk.zeros, true) + "])\n";
        out += "p = np.array([" + complexList(filter.zpk.poles, true) + "])\n";
        out += "k = " + num(filter.zpk.gain) + "\n";
        out += "sos = signal.zpk2sos(z, p, k)\n\nw, h = signal.sosfreqz(sos, worN=4096, fs=fs)\n# y = signal.sosfilt(sos, x)\n";
        break;
    }
    return out;
}

static std::string cCode(const DigitalFilter& filter, const std::string& header) {
    if (!filter.structure.isFIR) {
        const auto& sos = filter.structure.sos;
        std::string out = "/* " + header + "\n   Cascade of biquads, transposed direct form II. Call filter_sample() once per sample. */\n\n";
        out += "#define NUM_SECTIONS " + std::to_string(sos.size()) + "\n\n";
        out += "/* b0, b1, b2, a1, a2 for each section (a0 = 1) */\nstatic const double sos[NUM_SECTIONS][5] = {\n";
        std::vector<std::string> rows;
        for (const auto& s : sos)
            rows.push_back("    { " + num(s.b0) + ", " + num(s.b1) + ", " + num(s.b2) + ", " + num(s.a1) + ", " + num(s.a2) + " },");
        out += join(rows, "\n");
        out += "\n};\n\nstatic double state[NUM_SECTIONS][2];\n\n";
        out += "double filter_sample(double x)\n"
               "{\n"
               "    for (int s = 0; s < NUM_SECTIONS; s++) {\n"
               "        const double *c = sos[s];\n"
               "        double y = c[0] * x + state[s][0];\n"
               "        state[s][0] = c[1] * x - c[3] * y + state[s][1];\n"
               "        state[s][1] = c[2] * x - c[4] * y;\n"
               "        x = y;\n"
               "    }\n"
               "    return x;\n"
               "}\n";
        return out;
    }
    const auto& h = filter.structure.taps;
    std::string out = "/* " + header + "\n   Direct-form FIR. Call filter_sample() once per sample. */\n\n";
    out += "#define NUM_TAPS " + std::to_string(h.size()) + "\n\nstatic const double h[NUM_TAPS] = {\n";
    std::vector<std::string> lines;
    for (size_t i = 0; i < h.size(); i += 4) {
        std::vector<double> chunk(h.begin() + long(i), h.begin() + long(std::min(h.size(), i + 4)));
        lines.push_back("    " + joinNums(chunk, ", ") + ",");
    }
    out += join(lines, "\n") + "\n};\n\nstatic double delay[NUM_TAPS];\nstatic int pos = 0;\n\n";
    out += "double filter_sample(double x)\n"
           "{\n"
           "    delay[pos] = x;\n"
           "    double y = 0.0;\n"
           "    int i = pos;\n"
           "    for (int k = 0; k < NUM_TAPS; k++) {\n"
           "        y += h[k] * delay[i];          /* h[k] * x[n-k] */\n"
           "        i = (i == 0) ? NUM_TAPS - 1 : i - 1;\n"
           "    }\n"
           "    pos = (pos + 1) % NUM_TAPS;\n"
           "    return y;\n"
           "}\n";
    return out;
}

std::string exportCode(CodeLanguage lang, const DesignSpec& rawSpec, const DigitalFilter& filter) {
    double fs = filter.sampleRate;
    DesignSpec spec = rawSpec.clamped(fs);
    std::string header = "Filter Lab: " + filterTitle(spec) + ". " + replaceAll(filterSubtitle(spec, fs), kMinus, "-");
    std::string fsText = fs == std::round(fs) ? strf("%.0f", fs) : num(fs);
    switch (lang) {
    case CodeLanguage::matlab: return matlabCode(spec, filter, header, fsText);
    case CodeLanguage::python: return pythonCode(spec, filter, header, fsText);
    case CodeLanguage::c: return cCode(filter, header);
    }
    return "";
}

void copyToClipboard(void* owner, const std::string& text) {
    // Windows line endings so the code pastes cleanly into Notepad and MATLAB alike.
    std::wstring w = widen(replaceAll(text, "\n", "\r\n"));
    if (!OpenClipboard(HWND(owner))) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (mem) {
        void* p = GlobalLock(mem);
        memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(mem);
        if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
    }
    CloseClipboard();
}

void exportFilteredAudio(LabModel& model) {
    double fs = model.fs();
    std::vector<float> input;
    std::string name;
    if (auto loop = model.audio.currentLoopSamples()) {
        input = loop->first;
        name = loop->second;
    } else if (model.source() == SourceKind::microphone) {
        MessageBeep(MB_ICONWARNING);
        return;
    } else {
        // Render 8 seconds of the test signal.
        SourceGenerator gen(fs);
        int n = int(fs * 8);
        input.assign(size_t(n), 0.0f);
        int offset = 0;
        while (offset < n) {
            int m = std::min(1024, n - offset);
            gen.render(model.source(), model.frequency(), nullptr, input.data() + offset, m);
            offset += m;
        }
        name = sourceTitle(model.source());
    }
    if (model.source() == SourceKind::musicHum) {
        double phase = 0.0;
        const double amps[] = {0.10, 0.05, 0.07, 0.03, 0.05};
        for (auto& v : input) {
            phase += 50 / fs;
            if (phase >= 1) phase -= 1;
            double h = 0.0;
            for (int k = 0; k < 5; k++) h += amps[k] * std::sin(2 * kPi * phase * double(k + 1));
            v += float(h);
        }
    }
    FilterKernel kernel(model.filter());
    std::vector<float> output(input.size(), 0.0f);
    size_t offset = 0;
    while (offset < input.size()) {
        int m = int(std::min<size_t>(FilterKernel::maxBlock, input.size() - offset));
        kernel.process(input.data() + offset, output.data() + offset, m);
        offset += size_t(m);
    }
    auto path = chooseSaveLocation(model.window, name + " – filtered.wav", "WAV audio", "wav", "");
    if (!path) return;
    float peak = 0;
    for (float v : output) peak = std::max(peak, std::fabs(v));
    float g = peak > 0.99f ? 0.99f / peak : 1.0f;
    for (auto& v : output) v *= g;
    std::string error;
    if (!writeWav16(*path, output, fs, error)) showAlert(model.window, "Filter Lab", error);
}
