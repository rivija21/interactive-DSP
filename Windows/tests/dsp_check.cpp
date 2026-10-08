// Prints random filter designs so tests/check_dsp.py can compare them with SciPy.
// Build: g++ -O2 -std=c++20 -I../src tests/dsp_check.cpp ../src/dsp/*.cpp -o dsp_check
#include "dsp/Design.h"
#include <cstdio>
#include <random>

static void printRoots(const char* name, const std::vector<Complex>& r) {
    std::printf("\"%s\":[", name);
    for (size_t i = 0; i < r.size(); i++) std::printf("%s[%.17g,%.17g]", i ? "," : "", r[i].re, r[i].im);
    std::printf("]");
}

int main() {
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> u(0, 1);
    const double rates[] = {8000, 16000, 22050, 32000, 44100, 48000};
    std::printf("[\n");
    bool first = true;
    for (int t = 0; t < 780; t++) {
        DesignSpec s;
        double fs = rates[rng() % 6];
        bool fir = (t % 4 == 3);
        s.method = fir ? DesignMethod::fir : DesignMethod::iir;
        s.band = kAllBandTypes[rng() % 4];
        s.family = kAllFamilies[rng() % 5];
        s.order = 1 + int(rng() % (bandIsBand(s.band) ? 8 : 12));
        s.f1 = 30 * std::pow(fs * 0.4 / 30, u(rng));
        s.f2 = s.f1 * (1.2 + 3 * u(rng));
        s.rippleDB = 0.1 + 2.9 * u(rng);
        s.stopDB = 25 + 70 * u(rng);
        s.taps = 3 + int(rng() % 120);
        s.window = kAllWindows[rng() % 5];
        s.kaiserBeta = 12 * u(rng);
        DesignSpec c = s.clamped(fs);
        DigitalFilter f = buildFilter(c, fs);
        if (!first) std::printf(",\n");
        first = false;
        std::printf("{\"fs\":%.17g,\"method\":\"%s\",\"band\":\"%s\",\"family\":\"%s\",\"order\":%d,\"f1\":%.17g,\"f2\":%.17g,"
                    "\"rp\":%.17g,\"rs\":%.17g,\"taps\":%d,\"window\":\"%s\",\"beta\":%.17g,",
                    fs, methodRawValue(c.method), bandRawValue(c.band), familyRawValue(c.family), c.order, c.f1, c.f2,
                    c.rippleDB, c.stopDB, c.taps, windowRawValue(c.window), c.kaiserBeta);
        if (fir) {
            std::printf("\"h\":[");
            for (size_t i = 0; i < f.structure.taps.size(); i++) std::printf("%s%.17g", i ? "," : "", f.structure.taps[i]);
            std::printf("],");
            printRoots("fz", firZeros(f.structure.taps));
        } else {
            printRoots("z", f.zpk.zeros);
            std::printf(",");
            printRoots("p", f.zpk.poles);
            std::printf(",\"k\":%.17g,\"sos\":[", f.zpk.gain);
            for (size_t i = 0; i < f.structure.sos.size(); i++) {
                const auto& b = f.structure.sos[i];
                std::printf("%s[%.17g,%.17g,%.17g,1,%.17g,%.17g]", i ? "," : "", b.b0, b.b1, b.b2, b.a1, b.a2);
            }
            std::printf("]");
        }
        std::printf("}");
    }
    std::printf("\n]\n");
}
