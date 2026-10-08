#include "LabAudio.h"
#include "../platform/Dispatch.h"
#include "AudioFile.h"
#include "WasapiCommon.h"
#include <avrt.h>
#include <cmath>
#include <future>

using namespace wasapi;

// MARK: - RenderState

RenderState::RenderState(double fs, RTParams& params, Exchange<FilterKernel>& kernels, Exchange<LoopBuffer>& loops,
                         Exchange<MicLink>& micLinks, SampleRing& inputRing, SampleRing& outputRing)
    : fs_(fs), params_(params), kernels_(kernels), loops_(loops), micLinks_(micLinks), inputRing_(inputRing),
      outputRing_(outputRing), generator_(fs), x_(FilterKernel::maxBlock), y_(FilterKernel::maxBlock),
      o_(FilterKernel::maxBlock) {}

RenderState::~RenderState() {
    delete kernel_;
    delete loop_;
    delete micLink_;
}

void RenderState::render(int frameCount, float* out, int channels) {
    FilterKernel* old = kernel_;
    if (kernels_.take(kernel_) && kernel_ && old) kernel_->adoptState(*old);
    loops_.take(loop_);
    micLinks_.take(micLink_);

    SourceKind source = SourceKind(params_.source.value());
    if (int(source) != lastSource_) {
        generator_.loopPosition = 0;
        lastSource_ = int(source);
    }
    const bool paused = params_.paused.value() != 0;
    const float targetWet = params_.filterOn.value() != 0 ? 1.0f : 0.0f;
    const float targetGain = params_.listen.value() != 0 ? float(params_.volume.value()) : 0.0f;
    const float rampStep = float(1 / (0.01 * fs_));
    float inPeak = 0, outPeak = 0;
    bool clipped = false;
    float* x = x_.data();
    float* y = y_.data();
    float* o = o_.data();

    int offset = 0;
    while (offset < frameCount) {
        int n = std::min(frameCount - offset, FilterKernel::maxBlock);
        if (paused) {
            std::fill(x, x + n, 0.0f);
        } else if (source == SourceKind::microphone) {
            if (micLink_ && micLink_->reader) {
                micLink_->reader->read(x, n, float(params_.micGain.value()));
            } else {
                std::fill(x, x + n, 0.0f);
            }
        } else {
            double f = generator_.render(source, params_.frequency.value(), loop_, x, n);
            params_.currentFrequency.set(f);
        }
        if (kernel_) {
            if (!kernel_->process(x, y, n) && kernel_->isStable) params_.ranAway.set(1);
        } else {
            std::copy(x, x + n, y);
        }
        float* dst = out + size_t(offset) * channels;
        for (int i = 0; i < n; i++) {
            wet_ += std::max(-rampStep, std::min(rampStep, targetWet - wet_));
            gain_ += std::max(-rampStep, std::min(rampStep, targetGain - gain_));
            float heard = x[i] + (y[i] - x[i]) * wet_;
            o[i] = heard;
            float s = heard * gain_;
            if (s > 1) {
                s = 1;
                clipped = true;
            } else if (s < -1) {
                s = -1;
                clipped = true;
            }
            for (int c = 0; c < channels; c++) dst[size_t(i) * channels + c] = s;
            inPeak = std::max(inPeak, std::fabs(x[i]));
            outPeak = std::max(outPeak, std::fabs(heard));
        }
        inputRing_.write(x, n);
        outputRing_.write(o, n);
        offset += n;
    }
    params_.inputPeak.set(double(inPeak));
    params_.outputPeak.set(double(outPeak));
    if (clipped) params_.clipped.set(1);
}

// MARK: - Device change notifications

namespace {

/// Restarts the output when the default playback device changes.
class DeviceNotifier : public IMMNotificationClient {
public:
    explicit DeviceNotifier(LabAudio* audio) : audio_(audio) {}
    virtual ~DeviceNotifier() = default;

    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(++refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        long r = --refs_;
        if (r == 0) delete this;
        return ULONG(r);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, kIidIMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) {
            LabAudio* audio = audio_;
            dispatchMain([audio] { audio->restart(); });
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

    IMMDeviceEnumerator* enumerator = nullptr;

private:
    std::atomic<long> refs_{1};
    LabAudio* audio_;
};

} // namespace

// MARK: - LabAudio

LabAudio::LabAudio() {
    IMMDeviceEnumerator* enumerator = nullptr;
    if (SUCCEEDED(CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIidIMMDeviceEnumerator,
                                   reinterpret_cast<void**>(&enumerator)))) {
        auto* notifier = new DeviceNotifier(this);
        notifier->enumerator = enumerator;
        enumerator->RegisterEndpointNotificationCallback(notifier);
        deviceNotifier_ = notifier;
    }
}

LabAudio::~LabAudio() {
    shutdown();
    if (auto* notifier = static_cast<DeviceNotifier*>(deviceNotifier_)) {
        notifier->enumerator->UnregisterEndpointNotificationCallback(notifier);
        notifier->enumerator->Release();
        notifier->Release();
    }
}

void LabAudio::setSource(SourceKind s) {
    params.source.set(int64_t(s));
    refreshLoop();
}

std::optional<std::string> LabAudio::fileName() const {
    if (fileAudio_) return fileAudio_->name;
    return std::nullopt;
}

void LabAudio::start(double newFs) {
    stopEngine();
    fs_ = newFs;
    renderState_ = std::make_unique<RenderState>(fs_, params, kernels_, loops_, micLinks_, inputRing, outputRing);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::promise<std::string> ready;
    auto result = ready.get_future();
    threadFailed_ = false;
    thread_ = std::thread([this, &ready] {
        startError_.clear();
        // Initialisation happens on the audio thread itself; the result comes back here.
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IMMDeviceEnumerator* enumerator = nullptr;
        IMMDevice* device = nullptr;
        IAudioClient* client = nullptr;
        IAudioRenderClient* renderClient = nullptr;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HANDLE task = nullptr;
        bool signalled = false;
        auto signal = [&](const std::string& message) {
            if (!signalled) {
                signalled = true;
                ready.set_value(message);
            }
        };
        do {
            HRESULT hr = CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIidIMMDeviceEnumerator,
                                          reinterpret_cast<void**>(&enumerator));
            if (SUCCEEDED(hr)) hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
            if (FAILED(hr) || !device) {
                signal("no output device was found");
                break;
            }
            hr = device->Activate(kIidIAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
            if (FAILED(hr)) {
                signal("the output device couldn't be opened (error " + hex(hr) + ")");
                break;
            }
            // Ask for the lab's own rate in float stereo; Windows converts to the device's format.
            WAVEFORMATEXTENSIBLE wf = {};
            wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            wf.Format.nChannels = 2;
            wf.Format.nSamplesPerSec = DWORD(std::lround(fs_));
            wf.Format.wBitsPerSample = 32;
            wf.Format.nBlockAlign = WORD(wf.Format.nChannels * 4);
            wf.Format.nAvgBytesPerSec = wf.Format.nSamplesPerSec * wf.Format.nBlockAlign;
            wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
            wf.Samples.wValidBitsPerSample = 32;
            wf.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
            wf.SubFormat = kSubtypeFloat;
            DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                          AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200000, 0, &wf.Format, nullptr);
            channels_ = 2;
            if (FAILED(hr)) {
                // Older systems: fall back to the device's own format if the rates agree.
                release(client);
                device->Activate(kIidIAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
                WAVEFORMATEX* mix = nullptr;
                if (client && SUCCEEDED(client->GetMixFormat(&mix)) && mix &&
                    std::fabs(double(mix->nSamplesPerSec) - fs_) < 0.5 && mix->wBitsPerSample == 32) {
                    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000, 0, mix,
                                            nullptr);
                    channels_ = mix->nChannels;
                }
                if (mix) CoTaskMemFree(mix);
            }
            if (FAILED(hr) || !client) {
                signal("the output device refused the format (error " + hex(hr) + ")");
                break;
            }
            client->SetEventHandle(event);
            hr = client->GetService(kIidIAudioRenderClient, reinterpret_cast<void**>(&renderClient));
            UINT32 bufferFrames = 0;
            if (SUCCEEDED(hr)) hr = client->GetBufferSize(&bufferFrames);
            if (FAILED(hr)) {
                signal("the output stream couldn't be created (error " + hex(hr) + ")");
                break;
            }
            // Prime with silence, then start.
            BYTE* data = nullptr;
            if (SUCCEEDED(renderClient->GetBuffer(bufferFrames, &data)))
                renderClient->ReleaseBuffer(bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
            hr = client->Start();
            if (FAILED(hr)) {
                signal("the output stream couldn't start (error " + hex(hr) + ")");
                break;
            }
            signal("");
            DWORD taskIndex = 0;
            task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
            HANDLE waits[2] = {HANDLE(stopEvent_), event};
            for (;;) {
                DWORD w = WaitForMultipleObjects(2, waits, FALSE, 500);
                if (w == WAIT_OBJECT_0) break;
                UINT32 padding = 0;
                hr = client->GetCurrentPadding(&padding);
                if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                    threadFailed_ = true;
                    LabAudio* self = this;
                    dispatchMain([self] { self->restart(); });
                    break;
                }
                if (FAILED(hr)) continue;
                UINT32 frames = bufferFrames - padding;
                if (frames == 0) continue;
                if (FAILED(renderClient->GetBuffer(frames, &data))) continue;
                renderState_->render(int(frames), reinterpret_cast<float*>(data), channels_);
                renderClient->ReleaseBuffer(frames, 0);
            }
            client->Stop();
        } while (false);
        if (task) AvRevertMmThreadCharacteristics(task);
        release(renderClient);
        release(client);
        release(device);
        release(enumerator);
        CloseHandle(event);
        CoUninitialize();
        signal("the output stopped");
    });
    std::string error = result.get();
    if (!error.empty()) {
        if (onStatus) onStatus("Audio output couldn't start: " + error + ".");
        stopEngine();
        return;
    }
    running_ = true;
    if (lastFilter_) setFilter(*lastFilter_);
    refreshLoop();
    if (mic_) connectMic(true);
}

void LabAudio::stopEngine() {
    if (stopEvent_) SetEvent(HANDLE(stopEvent_));
    if (thread_.joinable()) thread_.join();
    if (stopEvent_) {
        CloseHandle(HANDLE(stopEvent_));
        stopEvent_ = nullptr;
    }
    renderState_.reset();
    running_ = false;
    kernels_.drain();
    loops_.drain();
    micLinks_.drain();
}

void LabAudio::shutdown() {
    stopMic();
    stopEngine();
}

void LabAudio::tick() {
    kernels_.drain();
    loops_.drain();
    micLinks_.drain();
}

void LabAudio::setFilter(const DigitalFilter& filter) {
    lastFilter_ = filter;
    params.ranAway.set(0);
    kernels_.publish(new FilterKernel(filter));
}

// MARK: Loops

const std::vector<float>& LabAudio::fileAtCurrentRate() {
    FileAudio& f = *fileAudio_;
    if (f.resampledRate != fs_) {
        f.resampled = resample(f.samples, f.rate, fs_);
        f.resampledRate = fs_;
    }
    return f.resampled;
}

void LabAudio::refreshLoop() {
    switch (source()) {
    case SourceKind::music:
    case SourceKind::musicHum: {
        auto it = musicCache_.find(fs_);
        if (it != musicCache_.end()) {
            loops_.publish(new LoopBuffer(it->second, "Music"));
        } else {
            double rate = fs_;
            backgroundQueue().async([this, rate] {
                auto samples = std::make_shared<std::vector<float>>(synthesizeMusicLoop(rate));
                dispatchMain([this, rate, samples] {
                    musicCache_[rate] = *samples;
                    if (fs_ == rate && (source() == SourceKind::music || source() == SourceKind::musicHum)) {
                        loops_.publish(new LoopBuffer(*samples, "Music"));
                    }
                });
            });
        }
        break;
    }
    case SourceKind::file:
        if (fileAudio_) loops_.publish(new LoopBuffer(fileAtCurrentRate(), fileAudio_->name));
        break;
    default: break;
    }
}

std::optional<std::pair<std::vector<float>, std::string>> LabAudio::currentLoopSamples() {
    switch (source()) {
    case SourceKind::music:
    case SourceKind::musicHum: {
        auto it = musicCache_.find(fs_);
        if (it == musicCache_.end()) return std::nullopt;
        return std::make_pair(it->second, std::string("Music loop"));
    }
    case SourceKind::file:
        if (!fileAudio_) return std::nullopt;
        return std::make_pair(fileAtCurrentRate(), fileAudio_->name);
    default: return std::nullopt;
    }
}

static std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

bool LabAudio::loadFile(const std::wstring& path, std::string& error) {
    std::vector<float> mono;
    double rate = 0;
    if (!decodeAudioFile(path, 600, mono, rate, error)) return false;
    // Normalise to a comfortable level.
    float peak = 0;
    for (float v : mono) peak = std::max(peak, std::fabs(v));
    if (peak > 0) {
        float g = std::min(0.7f / peak, 4.0f);
        for (auto& v : mono) v *= g;
    }
    std::wstring name = path;
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name = name.substr(0, dot);
    fileAudio_ = FileAudio{std::move(mono), rate, utf8(name), {}, 0};
    setSource(SourceKind::file);
    return true;
}

// MARK: Microphone

void LabAudio::startMic(const std::function<void(std::optional<std::string>)>& completion) {
    auto capture = mic_ ? std::move(mic_) : std::make_unique<MicCapture>();
    std::string error = capture->start(fs_);
    if (!error.empty()) {
        completion(error);
        return;
    }
    mic_ = std::move(capture);
    connectMic(false);
    completion(std::nullopt);
}

void LabAudio::connectMic(bool restartCapture) {
    if (!mic_) return;
    if (restartCapture) mic_->start(fs_);
    if (!(mic_->sampleRate() > 0)) return;
    micLinks_.publish(new MicLink(std::make_unique<MicReader>(mic_->ring, mic_->sampleRate(), fs_)));
}

void LabAudio::stopMic() {
    micLinks_.publish(new MicLink(nullptr));
    if (mic_) mic_->stop();
    mic_.reset();
}
