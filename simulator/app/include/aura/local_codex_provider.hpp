#pragma once

#include "aura/provider.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace aura::simulator {

class LocalCodexProvider final : public IAssistantProvider {
public:
    LocalCodexProvider(std::string token, std::uint16_t port);
    std::future<AssistantReply> start_turn(TurnRequest request) override;
    void cancel() override;
    const char* name() const noexcept override { return "Local Codex CLI"; }

private:
    std::string token_;
    std::uint16_t port_;
    // Shared with in-flight tasks so a turn may outlive a replaced provider.
    std::shared_ptr<std::atomic<std::uint64_t>> generation_{
        std::make_shared<std::atomic<std::uint64_t>>(0)};
    std::string active_id_;
};

}  // namespace aura::simulator

