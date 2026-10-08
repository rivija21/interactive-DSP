#pragma once
#include <string>
#include <vector>

/// Decodes an audio file (WAV, MP3, AAC/M4A, FLAC, WMA, ... anything Media Foundation reads)
/// to mono float at its own sample rate. At most `maxSeconds` are read.
/// Returns false and sets `error` if the file can't be read.
bool decodeAudioFile(const std::wstring& path, double maxSeconds, std::vector<float>& mono, double& rate,
                     std::string& error);

/// Offline sample-rate conversion (windowed-sinc, high quality).
std::vector<float> resample(const std::vector<float>& samples, double inRate, double outRate);

/// Writes 16-bit PCM mono WAV. Returns false and sets `error` on failure.
bool writeWav16(const std::wstring& path, const std::vector<float>& samples, double fs, std::string& error);

/// Media Foundation start-up and shut-down (call once each).
void audioFileStartup();
void audioFileShutdown();
