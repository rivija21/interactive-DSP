#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

// Text helpers. Strings are UTF-8 everywhere in the app and converted to UTF-16 only
// at the Windows API boundary.

/// printf into a std::string (C locale, like Swift's String(format:)).
inline std::string strf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return {};
    if (size_t(n) < sizeof buf) return std::string(buf, size_t(n));
    std::string s(size_t(n) + 1, '\0');
    va_start(ap, fmt);
    std::vsnprintf(s.data(), s.size(), fmt, ap);
    va_end(ap);
    s.resize(size_t(n));
    return s;
}

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& w);

/// Decodes UTF-8 into code points.
std::u32string toU32(const std::string& s);
std::string fromU32(const std::u32string& s);
/// Number of code points (Swift's String.count for the text used here).
size_t utf8Length(const std::string& s);
/// Pads with spaces (or truncates) to exactly `length` code points.
std::string padded(const std::string& s, size_t length);
std::string repeated(const std::string& s, size_t count);
std::string uppercased(const std::string& s);
std::string lowercased(const std::string& s);
std::string replaceAll(std::string s, const std::string& from, const std::string& to);
std::string trim(const std::string& s);
std::string join(const std::vector<std::string>& parts, const std::string& sep);
bool hasPrefix(const std::string& s, const std::string& prefix);
bool hasSuffix(const std::string& s, const std::string& suffix);

/// The Unicode minus sign, U+2212.
extern const char* const kMinus;
