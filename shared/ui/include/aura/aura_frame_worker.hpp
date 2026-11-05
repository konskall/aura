#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace aura::ui {

// A background thread that runs one job at a time, so drawing the presence
// never holds the UI thread. The firmware starts the shared worker on the
// ESP32-S3's second core before it creates the UI (app_main). The simulator
// starts none, so it draws Synapse on its UI thread as before.
class FrameWorker {
public:
    using Job = void (*)(void* context);

    FrameWorker();
    // Lets a running job finish, then stops the thread.
    ~FrameWorker();
    FrameWorker(const FrameWorker&) = delete;
    FrameWorker& operator=(const FrameWorker&) = delete;

    // Runs `job(context)` on the worker; false, and nothing runs, while a job is running.
    bool submit(Job job, void* context);
    bool busy() const noexcept { return busy_.load(std::memory_order_acquire); }
    // Blocks until no job is running.
    void wait_idle();

    // The worker the presence draws on, once the platform has started it.
    static FrameWorker* shared() noexcept;
    // Starts the shared worker; the thread takes the creating thread's settings
    // (the firmware sets core, priority and stack with esp_pthread_set_cfg first).
    static void start_shared();

private:
    void run();

    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    Job job_{};
    void* context_{};
    std::atomic<bool> busy_{false};
    bool stopping_{};
    std::thread thread_;
};

}  // namespace aura::ui
