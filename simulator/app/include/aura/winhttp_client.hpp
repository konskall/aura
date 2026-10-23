#pragma once

#include "aura/aoraton_account_worker.hpp"

#include <cstddef>

namespace aura::simulator {

// Blocking HTTP(S) for the Aoraton worker thread. It follows no redirects,
// caps every response at 1 MiB and never logs a URL, header or body (they
// carry bearer tokens and ciphertext).
class WinHttpClient final : public IAoratonHttp {
public:
    static constexpr std::size_t kMaxResponseBytes = 1024 * 1024;

    WinHttpClient();
    ~WinHttpClient() override;
    WinHttpClient(const WinHttpClient&) = delete;
    WinHttpClient& operator=(const WinHttpClient&) = delete;

    AoratonHttpResponse send(const AoratonHttpRequest& request) override;

    // GET that follows redirects, used only by the fake-backend smoke to stand
    // in for the browser; it returns the final status.
    int fetch_following_redirects(const std::string& url);

private:
    void* session_{nullptr};
};

}  // namespace aura::simulator
