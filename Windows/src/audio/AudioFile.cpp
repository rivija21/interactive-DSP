#include "AudioFile.h"
#include "../dsp/FIRDesign.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>

namespace {

template <class T>
void release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

std::string describe(HRESULT hr) {
    char buf[96];
    switch (hr) {
    case HRESULT(0xC00D36C4): return "the file type isn't supported";            // MF_E_UNSUPPORTED_BYTESTREAM_TYPE
    case HRESULT(0xC00D36B4): return "the audio format isn't supported";          // MF_E_INVALIDMEDIATYPE
    case HRESULT(0x80070002): return "the file doesn't exist";                    // ERROR_FILE_NOT_FOUND
    case HRESULT(0x80070005): return "access to the file was denied";             // E_ACCESSDENIED
    case HRESULT(0xC00D36B2): return "the file doesn't contain audio";            // MF_E_INVALIDREQUEST
    default: break;
    }
    std::snprintf(buf, sizeof buf, "the file couldn't be read (error 0x%08lX)", (unsigned long)hr);
    return buf;
}

} // namespace

void audioFileStartup() { MFStartup(MF_VERSION, MFSTARTUP_LITE); }
void audioFileShutdown() { MFShutdown(); }

bool decodeAudioFile(const std::wstring& path, double maxSeconds, std::vector<float>& mono, double& rate,
                     std::string& error) {
    IMFSourceReader* reader = nullptr;
    IMFMediaType* type = nullptr;
    IMFMediaType* current = nullptr;
    mono.clear();
    rate = 0;
    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
    if (SUCCEEDED(hr)) hr = reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (SUCCEEDED(hr)) hr = reader->SetStreamSelection(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&type);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, type);
    if (SUCCEEDED(hr)) hr = reader->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &current);
    UINT32 sampleRate = 0, channels = 0;
    if (SUCCEEDED(hr)) {
        sampleRate = MFGetAttributeUINT32(current, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
        channels = MFGetAttributeUINT32(current, MF_MT_AUDIO_NUM_CHANNELS, 0);
        if (sampleRate == 0 || channels == 0) hr = HRESULT(0xC00D36B4);
    }
    if (SUCCEEDED(hr)) {
        const size_t maxFrames = size_t(double(sampleRate) * maxSeconds);
        const float scale = 1.0f / float(channels);
        for (;;) {
            DWORD flags = 0;
            IMFSample* sample = nullptr;
            hr = reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, &sample);
            if (FAILED(hr)) break;
            if (sample) {
                IMFMediaBuffer* buffer = nullptr;
                if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                    BYTE* data = nullptr;
                    DWORD length = 0;
                    if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
                        const float* f = reinterpret_cast<const float*>(data);
                        size_t frames = length / (sizeof(float) * channels);
                        frames = std::min(frames, maxFrames - mono.size());
                        size_t base = mono.size();
                        mono.resize(base + frames);
                        for (size_t i = 0; i < frames; i++) {
                            float sum = 0;
                            for (UINT32 c = 0; c < channels; c++) sum += f[i * channels + c];
                            mono[base + i] = sum * scale;
                        }
                        buffer->Unlock();
                    }
                    release(buffer);
                }
                release(sample);
            }
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) || mono.size() >= maxFrames) break;
        }
    }
    release(current);
    release(type);
    release(reader);
    if (FAILED(hr)) {
        error = describe(hr);
        mono.clear();
        return false;
    }
    if (mono.empty()) {
        error = "the file contains no audio";
        return false;
    }
    rate = double(sampleRate);
    return true;
}

// MARK: - Resampling

std::vector<float> resample(const std::vector<float>& samples, double inRate, double outRate) {
    if (std::fabs(inRate - outRate) < 0.5 || samples.empty()) return samples;
    const double ratio = outRate / inRate;
    // Kaiser-windowed sinc low-pass at 97 % of the lower Nyquist frequency.
    const double cutoff = std::min(1.0, ratio) * 0.97;
    const int zeroCrossings = 20;
    const double halfWidth = zeroCrossings / cutoff;        // in input samples
    const double beta = 9.0;
    const double i0beta = besselI0(beta);
    auto kernel = [&](double t) {
        double a = std::fabs(t);
        if (a >= halfWidth) return 0.0;
        double r = a / halfWidth;
        return cutoff * sinc(cutoff * t) * besselI0(beta * std::sqrt(1 - r * r)) / i0beta;
    };

    const size_t inCount = samples.size();
    const size_t outCount = size_t(std::floor(double(inCount) * ratio));
    std::vector<float> out(outCount, 0.0f);
    const int taps = int(std::ceil(2 * halfWidth)) + 1;

    long long a = llround(outRate), b = llround(inRate);
    bool integral = std::fabs(double(a) - outRate) < 1e-9 && std::fabs(double(b) - inRate) < 1e-9;
    long long g = integral ? std::gcd(a, b) : 1;
    long long phases = integral ? a / g : 0;           // output samples per period
    long long step = integral ? b / g : 0;             // input samples per period
    if (integral && phases <= 4096) {
        // Rational ratio: precompute one kernel per output phase (polyphase).
        std::vector<float> table(size_t(phases) * size_t(taps));
        std::vector<int> firstTap((size_t(phases)));
        for (long long p = 0; p < phases; p++) {
            double t = double(p * step) / double(phases);   // fractional input position of output p
            double start = std::ceil(t - halfWidth);
            firstTap[size_t(p)] = int(start - std::floor(t));
            for (int k = 0; k < taps; k++) table[size_t(p) * taps + k] = float(kernel(t - (start + k)));
        }
        for (size_t j = 0; j < outCount; j++) {
            long long p = (long long)(j % size_t(phases));
            long long baseIn = (long long)(j / size_t(phases)) * step + (p * step) / phases;
            long long i0 = baseIn + firstTap[size_t(p)];
            const float* kw = table.data() + size_t(p) * taps;
            double acc = 0;
            for (int k = 0; k < taps; k++) {
                long long i = i0 + k;
                if (i >= 0 && i < (long long)inCount) acc += double(kw[k]) * double(samples[size_t(i)]);
            }
            out[j] = float(acc);
        }
    } else {
        for (size_t j = 0; j < outCount; j++) {
            double t = double(j) / ratio;
            long long start = (long long)std::ceil(t - halfWidth);
            double acc = 0;
            for (int k = 0; k < taps; k++) {
                long long i = start + k;
                if (i >= 0 && i < (long long)inCount) acc += kernel(t - double(i)) * double(samples[size_t(i)]);
            }
            out[j] = float(acc);
        }
    }
    return out;
}

// MARK: - WAV

bool writeWav16(const std::wstring& path, const std::vector<float>& samples, double fs, std::string& error) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) {
        error = "The file couldn't be saved.";
        return false;
    }
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    const uint32_t rate = uint32_t(std::lround(fs));
    const uint32_t dataBytes = uint32_t(samples.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    u32(16);
    u16(1);          // PCM
    u16(1);          // mono
    u32(rate);
    u32(rate * 2);   // byte rate
    u16(2);          // block align
    u16(16);         // bits
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    std::vector<int16_t> pcm(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        double v = std::round(double(samples[i]) * 32767.0);
        pcm[i] = int16_t(std::max(-32768.0, std::min(32767.0, v)));
    }
    size_t written = std::fwrite(pcm.data(), 2, pcm.size(), f);
    bool ok = written == pcm.size() && std::fclose(f) == 0;
    if (!ok) error = "The file couldn't be saved.";
    return ok;
}
