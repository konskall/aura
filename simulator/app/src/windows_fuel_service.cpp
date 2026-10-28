#include "aura/windows_fuel_service.hpp"

#include "aura/bridge_http.hpp"

namespace aura::simulator {

std::future<FuelFetchResult> WindowsFuelService::fetch() const {
    return std::async(std::launch::async, [token = token_, port = port_] {
        if (token.empty()) return FuelFetchResult{false, {}, "BRIDGE_NOT_CONFIGURED"};
        const auto response = bridge_request(L"GET", port, "/v1/fuel/prices", token, {}, nullptr, 5000, 8192);
        if (response.status == 200) {
            if (auto parsed = parse_fuel_price_json(response.body)) return FuelFetchResult{true, std::move(*parsed), {}};
            return FuelFetchResult{false, {}, "INVALID_FUEL_RESPONSE"};
        }
        auto code = json_string(response.body, "code");
        if (code.empty()) code = response.status == 0 ? "BRIDGE_UNAVAILABLE" : "FUEL_DATA_UNAVAILABLE";
        return FuelFetchResult{false, {}, std::move(code)};
    });
}

}  // namespace aura::simulator
