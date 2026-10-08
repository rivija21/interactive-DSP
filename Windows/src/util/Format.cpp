#include "Format.h"
#include "Text.h"
#include <cmath>
#include <cstdlib>

std::string formatHz(double f) {
    if (f >= 10000) return strf("%.1f kHz", f / 1000);
    if (f >= 1000) return strf("%.2f kHz", f / 1000);
    if (f >= 100) return strf("%.0f Hz", f);
    if (f >= 10) return strf("%.1f Hz", f);
    return strf("%.2f Hz", f);
}

std::string formatHzTick(double f) {
    if (f >= 1000) {
        double k = f / 1000;
        return k == std::round(k) ? strf("%.0fk", k) : strf("%.1fk", k);
    }
    return f == std::round(f) ? strf("%.0f", f) : strf("%.1f", f);
}

std::string formatDB(double db, int decimals) {
    std::string s = strf("%.*f dB", decimals, std::fabs(db));
    return db < -0.00001 ? kMinus + s : s;
}

std::string formatSigned(double v, const char* format) {
    std::string s = strf(format, std::fabs(v));
    return v < 0 ? kMinus + s : s;
}

std::string formatMs(double ms) {
    if (ms >= 100) return strf("%.0f ms", ms);
    if (ms >= 10) return strf("%.1f ms", ms);
    return strf("%.2f ms", ms);
}

std::optional<double> parseDouble(const std::string& text) {
    std::string s = text;
    if (s.empty()) return std::nullopt;
    for (char c : s)
        if (c == ' ' || c == '\t') return std::nullopt;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') return std::nullopt;
    return v;
}

std::optional<double> parseHz(const std::string& text) {
    std::string s = replaceAll(lowercased(text), " ", "");
    s = replaceAll(s, "hz", "");
    double mult = 1.0;
    if (!s.empty() && s.back() == 'k') {
        mult = 1000;
        s.pop_back();
    }
    auto v = parseDouble(s);
    if (!v || !std::isfinite(*v) || !(*v > 0)) return std::nullopt;
    return *v * mult;
}

std::string formatG(double v) { return strf("%g", v); }

std::string fmt(double v) {
    if (v == 0) return "0";
    double a = std::fabs(v);
    std::string sign = v < 0 ? kMinus : "";
    if (a >= 1e-3 && a < 1e5) {
        std::string s = strf("%.5g", a);
        if (s.find('e') != std::string::npos) s = strf("%.5f", a);
        return sign + s;
    }
    int e = int(std::floor(std::log10(a)));
    double m = a / std::pow(10.0, double(e));
    return sign + strf("%.4g", m) + "\xC3\x97" "10" + superscript(e);
}

std::string subscriptDigits(int n) {
    static const char* map[] = {"₀", "₁", "₂", "₃", "₄", "₅", "₆", "₇", "₈", "₉"};
    std::string digits = std::to_string(n), out;
    for (char c : digits) out += (c >= '0' && c <= '9') ? map[c - '0'] : std::string(1, c);
    return out;
}

std::string superscript(int n) {
    static const char* map[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
    std::string digits = std::to_string(n), out;
    for (char c : digits) {
        if (c >= '0' && c <= '9') out += map[c - '0'];
        else if (c == '-') out += "⁻";
        else out.push_back(c);
    }
    return out;
}
