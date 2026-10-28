#pragma once

#include "aura/fuel_prices.hpp"

#include <cstdint>
#include <future>
#include <string>
#include <utility>

namespace aura::simulator {

struct FuelFetchResult {
    bool ok{false};
    FuelPriceData data{};
    std::string error_code;
};

class WindowsFuelService {
public:
    WindowsFuelService(std::string token, std::uint16_t port) : token_(std::move(token)), port_(port) {}
    bool configured() const noexcept { return !token_.empty(); }
    std::future<FuelFetchResult> fetch() const;

private:
    std::string token_;
    std::uint16_t port_{4778};
};

}  // namespace aura::simulator
