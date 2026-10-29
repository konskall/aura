#pragma once

#include "aura/provider.hpp"

#include <atomic>
#include <cstdint>
#include <memory>

namespace aura {

class MockProvider final : public IAssistantProvider {
public:
    // `delay_ms` 0 answers at once (deterministic tests).
    explicit MockProvider(std::uint32_t delay_ms = 850) : delay_ms_(delay_ms) {}
    std::future<AssistantReply> start_turn(TurnRequest request) override;
    void cancel() override;
    // Shown as the assistant service on the Status screen, in any language.
    const char* name() const noexcept override { return "DEMO"; }

private:
    std::uint32_t delay_ms_;
    // Shared with in-flight tasks so a turn may outlive a replaced provider.
    std::shared_ptr<std::atomic<std::uint64_t>> generation_{
        std::make_shared<std::atomic<std::uint64_t>>(0)};
};

// The deterministic DEMO answer for a request (also used by tests).
AssistantReply mock_answer(const TurnRequest& request);

}  // namespace aura
