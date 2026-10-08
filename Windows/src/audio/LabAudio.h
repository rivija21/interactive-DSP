#pragma once
#include "FilterKernel.h"
#include "MicCapture.h"
#include "RTSupport.h"
#include "Sources.h"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>

/// Settings the audio thread reads every block.
struct RTParams {
    SharedInt source{int64_t(SourceKind::music)};
    SharedDouble frequency{440};
    SharedInt listen{0};
    SharedDouble volume{0.7};
    SharedInt filterOn{1};
    SharedDouble micGain{4};
    SharedInt paused{0};
    // Written by the audio thread.
    SharedDouble currentFrequency{0};
    SharedDouble inputPeak{0};
    SharedDouble outputPeak{0};
    SharedInt clipped{0};
    SharedInt ranAway{0};
};

/// Connects the microphone ring to the output thread (or disconnects it when reader is null).
struct MicLink {
    explicit MicLink(std::unique_ptr<MicReader> r) : reader(std::move(r)) {}
    std::unique_ptr<MicReader> reader;
};

/// Everything the output render callback touches. One instance per engine run.
class RenderState {
public:
    RenderState(double fs, RTParams& params, Exchange<FilterKernel>& kernels, Exchange<LoopBuffer>& loops,
                Exchange<MicLink>& micLinks, SampleRing& inputRing, SampleRing& outputRing);
    ~RenderState();
    /// Renders frameCount frames into `out` (interleaved, `channels` per frame).
    void render(int frameCount, float* out, int channels);

private:
    double fs_;
    RTParams& params_;
    Exchange<FilterKernel>& kernels_;
    Exchange<LoopBuffer>& loops_;
    Exchange<MicLink>& micLinks_;
    SampleRing& inputRing_;
    SampleRing& outputRing_;
    FilterKernel* kernel_ = nullptr;
    LoopBuffer* loop_ = nullptr;
    MicLink* micLink_ = nullptr;
    SourceGenerator generator_;
    std::vector<float> x_, y_, o_;
    float wet_ = 1;
    float gain_ = 0;
    int lastSource_ = -1;
};

/// Owns the audio output (WASAPI), the microphone and the loop buffers.
class LabAudio {
public:
    LabAudio();
    ~LabAudio();

    RTParams params;
    SampleRing inputRing{1 << 18};
    SampleRing outputRing{1 << 18};
    /// Called with a message when the output can't start.
    std::function<void(std::optional<std::string>)> onStatus;

    SourceKind source() const { return SourceKind(params.source.value()); }
    void setSource(SourceKind s);

    std::optional<std::string> fileName() const;
    bool hasFile() const { return fileAudio_.has_value(); }
    double fs() const { return fs_; }
    bool isRunning() const { return running_; }

    void start(double fs);
    void shutdown();
    /// Main-thread housekeeping: frees objects the audio thread has finished with.
    void tick();
    /// Rebuilds the output after the default device changed (headphones plugged in).
    void restart() { start(fs_); }

    void setFilter(const DigitalFilter& filter);

    /// Music samples at the current rate (for exporting).
    std::optional<std::pair<std::vector<float>, std::string>> currentLoopSamples();

    /// Decodes an audio file to mono. Returns false with a message if the file can't be read.
    bool loadFile(const std::wstring& path, std::string& error);

    /// Starts recording; completion gets nullopt on success, "denied" or a message otherwise.
    void startMic(const std::function<void(std::optional<std::string>)>& completion);
    void stopMic();
    bool micActive() const { return mic_ != nullptr; }

private:
    void stopEngine();
    void refreshLoop();
    void connectMic(bool restartCapture);
    void outputThread();
    const std::vector<float>& fileAtCurrentRate();

    Exchange<FilterKernel> kernels_;
    Exchange<LoopBuffer> loops_;
    Exchange<MicLink> micLinks_;
    std::unique_ptr<RenderState> renderState_;
    double fs_ = 48000;
    std::optional<DigitalFilter> lastFilter_;
    std::map<double, std::vector<float>> musicCache_;
    struct FileAudio {
        std::vector<float> samples;
        double rate;
        std::string name;
        std::vector<float> resampled;
        double resampledRate = 0;
    };
    std::optional<FileAudio> fileAudio_;
    std::unique_ptr<MicCapture> mic_;
    bool running_ = false;

    // WASAPI output thread.
    std::thread thread_;
    void* stopEvent_ = nullptr;
    std::atomic<bool> threadFailed_{false};
    std::string startError_;
    int channels_ = 2;
    void* deviceNotifier_ = nullptr;
};
