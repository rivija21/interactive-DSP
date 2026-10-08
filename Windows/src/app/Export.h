#pragma once
#include "../dsp/Design.h"
#include <string>

class LabModel;

// Copyable code for coursework (MATLAB, SciPy, C), and filtered-audio export.

enum class CodeLanguage { matlab, python, c };
inline constexpr CodeLanguage kAllLanguages[] = {CodeLanguage::matlab, CodeLanguage::python, CodeLanguage::c};
const char* languageTitle(CodeLanguage l);
const char* languageRawValue(CodeLanguage l);

std::string ordinal(int n);
/// "4th-order Butterworth low-pass" etc.
std::string filterTitle(const DesignSpec& spec);
std::string filterSubtitle(const DesignSpec& spec, double fs);
std::string exportCode(CodeLanguage lang, const DesignSpec& spec, const DigitalFilter& filter);

/// Runs the current source through the current filter offline and writes a WAV file.
void exportFilteredAudio(LabModel& model);

/// Puts UTF-8 text on the clipboard.
void copyToClipboard(void* owner, const std::string& text);
