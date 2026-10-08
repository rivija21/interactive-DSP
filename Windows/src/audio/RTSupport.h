#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// Plumbing for passing data to and from the real-time audio threads without blocking them.

/// A double that can be read and written from any thread.
class SharedDouble {
public:
    explicit SharedDouble(double v) : bits_(v) {}
    double value() const { return bits_.load(std::memory_order_relaxed); }
    void set(double v) { bits_.store(v, std::memory_order_relaxed); }

private:
    std::atomic<double> bits_;
};

class SharedInt {
public:
    explicit SharedInt(int64_t v) : storage_(v) {}
    int64_t value() const { return storage_.load(std::memory_order_relaxed); }
    void set(int64_t v) { storage_.store(v, std::memory_order_relaxed); }

private:
    std::atomic<int64_t> storage_;
};

/// A ring of float samples with one writer. Readers either consume in order (the mic
/// path) or just look at the most recent samples (the analysis displays).
class SampleRing {
public:
    explicit SampleRing(int capacity) : capacity_(capacity), mask_(capacity - 1), storage_(size_t(capacity), 0.0f) {
        assert(capacity > 0 && (capacity & (capacity - 1)) == 0 && "capacity must be a power of two");
    }

    int capacity() const { return capacity_; }

    /// Total samples ever written. Sample i (absolute) lives at storage[i & mask].
    int64_t totalWritten() const { return written_.load(std::memory_order_acquire); }

    void write(const float* src, int n) {
        int64_t w = written_.load(std::memory_order_relaxed);
        int i = 0;
        while (i < n) {
            int idx = int((w + i) & mask_);
            int chunk = std::min(n - i, capacity_ - idx);
            std::memcpy(storage_.data() + idx, src + i, size_t(chunk) * sizeof(float));
            i += chunk;
        }
        written_.store(w + n, std::memory_order_release);
    }

    float sample(int64_t absoluteIndex) const { return storage_[size_t(absoluteIndex & mask_)]; }

    /// Copies samples [start, start + count) (absolute indices) into dst.
    void copy(int64_t start, int count, float* dst) const {
        int i = 0;
        while (i < count) {
            int idx = int((start + i) & mask_);
            int chunk = std::min(count - i, capacity_ - idx);
            std::memcpy(dst + i, storage_.data() + idx, size_t(chunk) * sizeof(float));
            i += chunk;
        }
    }

    /// Copies the most recent `count` samples; returns the absolute index just past the last one.
    int64_t copyLatest(int count, float* dst) const {
        int64_t end = totalWritten();
        copy(end - count, count, dst);
        return end;
    }

private:
    int capacity_;
    int64_t mask_;
    std::vector<float> storage_;
    std::atomic<int64_t> written_{0};
};

/// A tiny spin lock with a non-blocking try for the audio thread.
class SpinLock {
public:
    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
        }
    }
    bool tryLock() { return !flag_.test_and_set(std::memory_order_acquire); }
    void unlock() { flag_.clear(std::memory_order_release); }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

/// Hands objects (filter kernels, audio loops) from the main thread to an audio thread.
/// The audio thread never blocks and never frees memory: the object it replaces is parked
/// in `retired` and deleted later on the main thread.
template <class T>
class Exchange {
public:
    ~Exchange() {
        delete pending_;
        delete retired_;
    }

    /// Main thread: queue `object` to replace whatever the audio thread is using.
    void publish(T* object) {
        lock_.lock();
        T* g1 = pending_;
        T* g2 = retired_;
        pending_ = object;
        retired_ = nullptr;
        lock_.unlock();
        delete g1; // released here, outside the lock
        delete g2;
    }

    /// Main thread: release whatever the audio thread has finished with.
    void drain() {
        lock_.lock();
        T* g = retired_;
        retired_ = nullptr;
        lock_.unlock();
        delete g;
    }

    /// Audio thread: swap in the pending object if there is one. Never blocks.
    bool take(T*& current) {
        if (!lock_.tryLock()) return false;
        bool swapped = false;
        if (pending_ != nullptr && retired_ == nullptr) {
            retired_ = current;
            current = pending_;
            pending_ = nullptr;
            swapped = true;
        }
        lock_.unlock();
        return swapped;
    }

private:
    SpinLock lock_;
    T* pending_ = nullptr;
    T* retired_ = nullptr;
};

/// A block of mono audio that loops (the synthesized music or a loaded file).
class LoopBuffer {
public:
    LoopBuffer(const std::vector<float>& data, std::string name)
        : samples(std::max<size_t>(1, data.size()), 0.0f), name(std::move(name)) {
        if (!data.empty()) std::memcpy(samples.data(), data.data(), data.size() * sizeof(float));
    }
    int count() const { return int(samples.size()); }

    std::vector<float> samples;
    std::string name;
};
