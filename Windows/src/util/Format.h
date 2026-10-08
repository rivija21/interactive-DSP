#pragma once
#include <optional>
#include <string>

// MARK: - Number formatting (from Theme.swift)

/// "250 Hz", "1.50 kHz", "12.0 kHz".
std::string formatHz(double f);
/// Short tick label: "50", "500", "2k", "10k".
std::string formatHzTick(double f);
std::string formatDB(double db, int decimals = 1);
/// A signed number with a proper minus sign.
std::string formatSigned(double v, const char* format = "%.4f");
std::string formatMs(double ms);
/// Parses "1000", "1k", "1.5 kHz", "800hz".
std::optional<double> parseHz(const std::string& text);
/// Parses a plain number the way Swift's Double(String) does (whole string must be a number).
std::optional<double> parseDouble(const std::string& text);

/// Six significant digits; scientific numbers as 2.1x10^-5 with superscripts.
std::string fmt(double v);
std::string subscriptDigits(int n);
std::string superscript(int n);
/// "%g" the way Swift prints it.
std::string formatG(double v);
