#pragma once

#include "aura/aoraton_account_worker.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace aura::simulator {

// The way back from Google sign-in on the PC: opens the default browser at
// the Supabase authorize URL and accepts exactly one
// GET /aoraton/callback?code=… on 127.0.0.1, then stops listening.
class LoopbackSignInChannel final : public IAoratonSignInChannel {
public:
    using BrowserOpener = std::function<bool(const std::string& url)>;

    explicit LoopbackSignInChannel(std::uint16_t port, BrowserOpener opener = {});
    ~LoopbackSignInChannel() override;
    LoopbackSignInChannel(const LoopbackSignInChannel&) = delete;
    LoopbackSignInChannel& operator=(const LoopbackSignInChannel&) = delete;

    bool open(const std::string& authorize_url) override;
    AoratonCallbackStatus poll(std::string& code) override;
    void close() override;

    static std::string redirect_url(std::uint16_t port);

private:
    void serve(std::uintptr_t listener);

    std::uint16_t port_;
    BrowserOpener opener_;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
    std::mutex mutex_;
    std::string code_;
    bool received_{false};
    bool failed_{false};
};

}  // namespace aura::simulator
