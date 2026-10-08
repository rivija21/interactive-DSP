#pragma once
#include <map>
#include <optional>
#include <string>

/// A tiny persistent key-value store (the stand-in for UserDefaults), kept in
/// %APPDATA%\Filter Lab\settings.txt.
class Settings {
public:
    static Settings& shared();

    bool has(const std::string& key) const { return values_.count(key) != 0; }
    double getDouble(const std::string& key, double fallback = 0) const;
    int getInt(const std::string& key, int fallback = 0) const;
    bool getBool(const std::string& key, bool fallback = false) const;
    std::optional<std::string> getString(const std::string& key) const;

    void set(const std::string& key, double v);
    void set(const std::string& key, int v);
    void set(const std::string& key, bool v);
    void set(const std::string& key, const std::string& v);

    void save();

private:
    Settings();
    std::wstring path_;
    std::map<std::string, std::string> values_;
};
