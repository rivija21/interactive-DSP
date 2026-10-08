#pragma once
#include "FilterKernel.h"
#include "RTSupport.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

/// Records the default input device with WASAPI (shared mode, event driven) and writes
/// mono samples into a ring. It runs at the device's own rate; MicReader converts the
/// rate on the output thread.
class MicCapture {
public:
    MicCapture();
    ~MicCapture();

    std::shared_ptr<SampleRing> ring;
    double sampleRate() const { return sampleRate_; }

    /// Starts recording. `targetRate` is the lab's processing rate; when it's lower than the
    /// microphone's rate an anti-aliasing filter runs before the samples are handed over.
    /// Returns an empty string on success, "denied" if Windows privacy settings block the
    /// microphone, or a message describing the problem.
    std::string start(double targetRate);
    void stop();

private:
    void run(double targetRate, void* readyPromise);
    double sampleRate_ = 0;
    std::atomic<bool> running_{false};
    std::thread thread_;
    void* stopEvent_ = nullptr;
};

/// Reads the microphone ring on the output thread at the lab's sample rate. A 4-point
/// Hermite interpolator converts the rate, and the read speed is nudged so the buffer
/// stays about 40 ms full even though mic and speakers run on separate clocks.
class MicReader {
public:
    MicReader(std::shared_ptr<SampleRing> ring, double micRate, double outputRate);
    void read(float* dst, int n, float gain);

    std::shared_ptr<SampleRing> ring;

private:
    double nominalRatio_;
    double target_;
    double position_ = -1.0;
    double adjust_ = 1.0;
};
