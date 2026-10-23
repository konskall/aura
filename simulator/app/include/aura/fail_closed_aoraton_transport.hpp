#pragma once

#include "aura/aoraton_real.hpp"

#include <cstddef>

namespace aura::simulator {

// Consumes production-service commands when no reviewed isolated backend is
// configured. It never fabricates data and turns every network-bearing action
// into a typed failure event that the UI can render truthfully.
class FailClosedAoratonTransport final {
public:
    explicit FailClosedAoratonTransport(AoratonMailbox& mailbox) : mailbox_(mailbox) {}

    std::size_t poll(std::size_t maximum_commands = 8);

private:
    AoratonMailbox& mailbox_;
};

}  // namespace aura::simulator
