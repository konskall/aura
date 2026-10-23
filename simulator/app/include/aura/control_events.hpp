#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aura::simulator {

struct ControlEvent {
    std::string type;
    std::string payload;
};

class ControlEventReader {
public:
    explicit ControlEventReader(std::string path);
    std::vector<ControlEvent> poll();

private:
    std::string path_;
    std::uint64_t offset_{0};
};

}  // namespace aura::simulator

