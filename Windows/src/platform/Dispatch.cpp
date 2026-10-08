#include "Dispatch.h"
#include <windows.h>
#include <vector>

namespace {
HWND gWindow = nullptr;
UINT gMessage = 0;
std::mutex gMutex;
std::vector<std::function<void()>> gQueue;

struct Delayed {
    double due;
    std::function<void()> fn;
};
std::vector<Delayed> gDelayed;
} // namespace

void dispatchInit(void* hwnd, unsigned message) {
    gWindow = HWND(hwnd);
    gMessage = message;
}

void dispatchMain(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gQueue.push_back(std::move(fn));
    }
    if (gWindow) PostMessageW(gWindow, gMessage, 0, 0);
}

void dispatchMainAfter(double seconds, std::function<void()> fn) {
    gDelayed.push_back({mediaTime() + seconds, std::move(fn)});
}

void dispatchDrain() {
    std::vector<std::function<void()>> work;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        work.swap(gQueue);
    }
    for (auto& fn : work) fn();
}

void dispatchTimers() {
    if (gDelayed.empty()) return;
    double now = mediaTime();
    std::vector<std::function<void()>> due;
    for (size_t i = 0; i < gDelayed.size();) {
        if (gDelayed[i].due <= now) {
            due.push_back(std::move(gDelayed[i].fn));
            gDelayed.erase(gDelayed.begin() + long(i));
        } else {
            i++;
        }
    }
    for (auto& fn : due) fn();
}

double mediaTime() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return double(now.QuadPart) / double(freq.QuadPart);
}

SerialQueue::SerialQueue() : thread_([this] { run(); }) {}

SerialQueue::~SerialQueue() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void SerialQueue::async(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        work_.push_back(std::move(fn));
    }
    cv_.notify_one();
}

void SerialQueue::run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        std::function<void()> fn;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !work_.empty(); });
            if (stopping_ && work_.empty()) return;
            fn = std::move(work_.front());
            work_.pop_front();
        }
        fn();
    }
}

SerialQueue& backgroundQueue() {
    static SerialQueue* q = new SerialQueue(); // never destroyed: work may still be running at exit
    return *q;
}
