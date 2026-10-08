#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

// Main-thread and background dispatch (the Windows stand-ins for GCD's DispatchQueue).

/// Call once on the UI thread with the window whose message loop runs main-thread work.
void dispatchInit(void* hwnd, unsigned message);
/// Runs fn on the UI thread soon. Safe to call from any thread.
void dispatchMain(std::function<void()> fn);
/// Runs fn on the UI thread after a delay. UI thread only.
void dispatchMainAfter(double seconds, std::function<void()> fn);
/// UI thread: runs queued work (called from the window procedure).
void dispatchDrain();
/// UI thread: runs delayed work that is due (called from the frame timer).
void dispatchTimers();
/// Seconds since an arbitrary start, high resolution (like CACurrentMediaTime).
double mediaTime();

/// A serial background queue with one worker thread.
class SerialQueue {
public:
    SerialQueue();
    ~SerialQueue();
    void async(std::function<void()> fn);

private:
    void run();
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> work_;
    bool stopping_ = false;
    std::thread thread_;
};

/// Shared background queues (they live for the whole run of the app).
SerialQueue& backgroundQueue();
