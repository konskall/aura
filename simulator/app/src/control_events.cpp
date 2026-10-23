#include "aura/control_events.hpp"

#include <filesystem>
#include <fstream>

namespace aura::simulator {

namespace {
int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

std::string decode(std::string_view encoded) {
    std::string value;
    value.reserve(encoded.size());
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        if (encoded[index] == '%' && index + 2 < encoded.size()) {
            const int high = hex_value(encoded[index + 1]);
            const int low = hex_value(encoded[index + 2]);
            if (high >= 0 && low >= 0) {
                value.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        value.push_back(encoded[index] == '+' ? ' ' : encoded[index]);
    }
    return value;
}
}

ControlEventReader::ControlEventReader(std::string path) : path_(std::move(path)) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path_, error);
    if (!error) offset_ = size;
}

std::vector<ControlEvent> ControlEventReader::poll() {
    std::vector<ControlEvent> events;
    std::error_code error;
    const auto size = std::filesystem::file_size(path_, error);
    if (error) return events;
    if (size < offset_) offset_ = 0;
    if (size == offset_) return events;

    std::ifstream input(path_, std::ios::binary);
    if (!input) return events;
    input.seekg(static_cast<std::streamoff>(offset_));
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos) continue;
        events.push_back({line.substr(first + 1, second - first - 1), decode(line.substr(second + 1))});
    }
    const auto position = input.tellg();
    offset_ = position < 0 ? size : static_cast<std::uint64_t>(position);
    return events;
}

}  // namespace aura::simulator
