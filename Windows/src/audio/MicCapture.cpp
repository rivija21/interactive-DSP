#include "MicCapture.h"
#include "../dsp/Design.h"
#include "WasapiCommon.h"
#include <avrt.h>
#include <future>
#include <vector>

using namespace wasapi;

MicCapture::MicCapture() : ring(std::make_shared<SampleRing>(1 << 17)) {}

MicCapture::~MicCapture() { stop(); }

std::string MicCapture::start(double targetRate) {
    stop();
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::promise<std::string> ready;
    auto result = ready.get_future();
    running_ = true;
    thread_ = std::thread([this, targetRate, &ready] { run(targetRate, &ready); });
    std::string error = result.get();
    if (!error.empty()) stop();
    return error;
}

void MicCapture::stop() {
    if (stopEvent_) SetEvent(HANDLE(stopEvent_));
    if (thread_.joinable()) thread_.join();
    running_ = false;
    if (stopEvent_) {
        CloseHandle(HANDLE(stopEvent_));
        stopEvent_ = nullptr;
    }
}

namespace {

/// Converts one packet of interleaved device samples to mono float.
void toMono(const BYTE* data, UINT32 frames, const WAVEFORMATEX* wf, bool isFloat, float* mono) {
    const int channels = wf->nChannels;
    const int bits = wf->wBitsPerSample;
    const int stride = wf->nBlockAlign;
    const float scale = 1.0f / float(channels);
    for (UINT32 i = 0; i < frames; i++) {
        const BYTE* frame = data + size_t(i) * stride;
        float sum = 0;
        for (int c = 0; c < channels; c++) {
            float v = 0;
            if (isFloat && bits == 32) {
                v = reinterpret_cast<const float*>(frame)[c];
            } else if (bits == 16) {
                v = float(reinterpret_cast<const int16_t*>(frame)[c]) / 32768.0f;
            } else if (bits == 24) {
                const BYTE* p = frame + c * 3;
                int32_t s = (int32_t(p[0]) << 8) | (int32_t(p[1]) << 16) | (int32_t(p[2]) << 24);
                v = float(s) / 2147483648.0f;
            } else if (bits == 32) {
                v = float(reinterpret_cast<const int32_t*>(frame)[c]) / 2147483648.0f;
            }
            sum += v;
        }
        mono[i] = sum * scale;
    }
}

} // namespace

void MicCapture::run(double targetRate, void* readyPromise) {
    auto* ready = static_cast<std::promise<std::string>*>(readyPromise);
    bool signalled = false;
    auto fail = [&](const std::string& message) {
        if (!signalled) {
            signalled = true;
            ready->set_value(message);
        }
    };
    auto failed = [&](const char* what, HRESULT hr) {
        if (hr == E_ACCESSDENIED) {
            fail("denied");
        } else {
            fail(std::string("Couldn't start the microphone (") + what + ", error " + hex(hr) + ").");
        }
    };

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* wf = nullptr;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE task = nullptr;
    std::unique_ptr<FilterKernel> antiAlias;

    do {
        HRESULT hr = CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIidIMMDeviceEnumerator,
                                      reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr)) { failed("open", hr); break; }
        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
        if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) || !device) {
            fail("No microphone was found.");
            break;
        }
        if (FAILED(hr)) { failed("find device", hr); break; }
        hr = device->Activate(kIidIAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
        if (FAILED(hr)) { failed("select device", hr); break; }
        hr = client->GetMixFormat(&wf);
        if (FAILED(hr) || !wf) { failed("read format", hr); break; }
        if (wf->nSamplesPerSec == 0 || wf->nChannels == 0) {
            fail("No microphone was found.");
            break;
        }
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000, 0, wf, nullptr);
        if (FAILED(hr)) { failed("initialise", hr); break; }
        hr = client->SetEventHandle(event);
        if (FAILED(hr)) { failed("set callback", hr); break; }
        hr = client->GetService(kIidIAudioCaptureClient, reinterpret_cast<void**>(&capture));
        if (FAILED(hr)) { failed("set format", hr); break; }

        sampleRate_ = double(wf->nSamplesPerSec);
        bool isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wf);
            isFloat = IsEqualGUID(ext->SubFormat, kSubtypeFloat) != 0;
        }
        if (targetRate < sampleRate_ * 0.98) {
            // Elliptic low-pass just below the new Nyquist frequency.
            DesignSpec spec;
            spec.method = DesignMethod::iir;
            spec.family = IIRFamily::elliptic;
            spec.order = 8;
            spec.f1 = targetRate * 0.45;
            spec.rippleDB = 0.1;
            spec.stopDB = 90;
            antiAlias = std::make_unique<FilterKernel>(buildFilter(spec, sampleRate_));
        }

        hr = client->Start();
        if (FAILED(hr)) { failed("start", hr); break; }
        fail(""); // success

        DWORD taskIndex = 0;
        task = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);
        std::vector<float> mono(8192), filtered(FilterKernel::maxBlock);
        HANDLE waits[2] = {HANDLE(stopEvent_), event};
        for (;;) {
            DWORD w = WaitForMultipleObjects(2, waits, FALSE, 500);
            if (w == WAIT_OBJECT_0) break;
            UINT32 packet = 0;
            while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
                if (frames > mono.size()) mono.resize(frames);
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    std::fill(mono.begin(), mono.begin() + frames, 0.0f);
                } else {
                    toMono(data, frames, wf, isFloat, mono.data());
                }
                capture->ReleaseBuffer(frames);
                int n = int(frames);
                int offset = 0;
                while (offset < n) {
                    int m = std::min(n - offset, FilterKernel::maxBlock);
                    if (antiAlias) {
                        antiAlias->process(mono.data() + offset, filtered.data(), m);
                        ring->write(filtered.data(), m);
                    } else {
                        ring->write(mono.data() + offset, m);
                    }
                    offset += m;
                }
            }
        }
        client->Stop();
    } while (false);

    if (task) AvRevertMmThreadCharacteristics(task);
    if (wf) CoTaskMemFree(wf);
    release(capture);
    release(client);
    release(device);
    release(enumerator);
    CloseHandle(event);
    CoUninitialize();
    fail("Couldn't start the microphone.");
}

// MARK: - MicReader

MicReader::MicReader(std::shared_ptr<SampleRing> r, double micRate, double outputRate)
    : ring(std::move(r)), nominalRatio_(micRate / outputRate), target_(micRate * 0.04 + 1024) {}

void MicReader::read(float* dst, int n, float gain) {
    double written = double(ring->totalWritten());
    // Keep at least two output blocks' worth of mic samples in reserve.
    target_ = std::max(target_, double(n) * nominalRatio_ * 2 + 256);
    if (position_ < 0 || written - position_ > double(ring->capacity()) / 2) {
        position_ = written - target_;
    }
    double needed = double(n) * nominalRatio_ * adjust_ + 4;
    if (written - position_ < needed) {
        // Underrun (mic just started or stalled): wait for data to build up.
        std::fill(dst, dst + n, 0.0f);
        position_ = std::max(0.0, written - target_);
        return;
    }
    double error = (written - position_ - target_) / target_;
    adjust_ += (1 + std::max(-0.003, std::min(0.003, error * 0.01)) - adjust_) * 0.05;
    double step = nominalRatio_ * adjust_;
    for (int i = 0; i < n; i++) {
        int64_t base = int64_t(position_);
        float t = float(position_ - double(base));
        float y0 = ring->sample(base - 1), y1 = ring->sample(base), y2 = ring->sample(base + 1), y3 = ring->sample(base + 2);
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2 * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        dst[i] = gain * (((c3 * t + c2) * t + c1) * t + y1);
        position_ += step;
    }
}
