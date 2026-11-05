#include "aura/aura_frame_worker.hpp"

namespace aura::ui {
namespace {

std::atomic<FrameWorker*> shared_worker{nullptr};

}  // namespace

FrameWorker::FrameWorker() : thread_([this] { run(); }) {}

FrameWorker::~FrameWorker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

bool FrameWorker::submit(Job job, void* context) {
    if (!job) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (busy_.load(std::memory_order_relaxed) || stopping_) return false;
        job_ = job;
        context_ = context;
        busy_.store(true, std::memory_order_release);
    }
    wake_.notify_one();
    return true;
}

void FrameWorker::wait_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return !busy_.load(std::memory_order_relaxed); });
}

void FrameWorker::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        wake_.wait(lock, [this] { return stopping_ || job_ != nullptr; });
        if (!job_) return;  // stopping, and nothing left to run
        const Job job = job_;
        void* const context = context_;
        lock.unlock();
        job(context);
        lock.lock();
        job_ = nullptr;
        context_ = nullptr;
        busy_.store(false, std::memory_order_release);
        idle_.notify_all();
    }
}

FrameWorker* FrameWorker::shared() noexcept { return shared_worker.load(std::memory_order_acquire); }

void FrameWorker::start_shared() {
    if (shared_worker.load(std::memory_order_acquire)) return;
    // Lives for the whole program: presences may draw on it until the very end.
    static FrameWorker worker;
    shared_worker.store(&worker, std::memory_order_release);
}

}  // namespace aura::ui
