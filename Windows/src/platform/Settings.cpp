#include "Settings.h"
#include "../util/Text.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cstdlib>

// File format: one entry per line, "key=value", with "\n" and "\\" escaped in values.

static std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out.push_back(c);
    }
    return out;
}

static std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            out.push_back(n == 'n' ? '\n' : (n == 'r' ? '\r' : n));
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

Settings& Settings::shared() {
    static Settings* s = new Settings();
    return *s;
}

Settings::Settings() {
    PWSTR appData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)) && appData) {
        std::wstring dir = std::wstring(appData) + L"\\Filter Lab";
        CoTaskMemFree(appData);
        CreateDirectoryW(dir.c_str(), nullptr);
        path_ = dir + L"\\settings.txt";
    }
    if (path_.empty()) return;
    FILE* f = _wfopen(path_.c_str(), L"rb");
    if (!f) return;
    std::string data;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
    std::fclose(f);
    size_t start = 0;
    while (start < data.size()) {
        size_t end = data.find('\n', start);
        if (end == std::string::npos) end = data.size();
        std::string line = data.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq != std::string::npos) values_[line.substr(0, eq)] = unescape(line.substr(eq + 1));
        start = end + 1;
    }
}

double Settings::getDouble(const std::string& key, double fallback) const {
    auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    char* end = nullptr;
    double v = std::strtod(it->second.c_str(), &end);
    return end == it->second.c_str() ? fallback : v;
}

int Settings::getInt(const std::string& key, int fallback) const {
    auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    return std::atoi(it->second.c_str());
}

bool Settings::getBool(const std::string& key, bool fallback) const {
    auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    return it->second == "1" || it->second == "true";
}

std::optional<std::string> Settings::getString(const std::string& key) const {
    auto it = values_.find(key);
    if (it == values_.end()) return std::nullopt;
    return it->second;
}

void Settings::set(const std::string& key, double v) { values_[key] = strf("%.17g", v); }
void Settings::set(const std::string& key, int v) { values_[key] = std::to_string(v); }
void Settings::set(const std::string& key, bool v) { values_[key] = v ? "1" : "0"; }
void Settings::set(const std::string& key, const std::string& v) { values_[key] = v; }

void Settings::save() {
    if (path_.empty()) return;
    std::string data;
    for (const auto& [k, v] : values_) data += k + "=" + escape(v) + "\n";
    std::wstring tmp = path_ + L".tmp";
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) return;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fclose(f) == 0) && ok;
    if (ok) MoveFileExW(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING);
}
