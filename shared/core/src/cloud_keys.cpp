#include "aura/cloud_keys.hpp"

#include <algorithm>

namespace aura {
namespace {

template <typename Allowed>
bool only(const std::string& text, Allowed allowed) {
    return std::all_of(text.begin(), text.end(), [&](char ch) { return allowed(static_cast<unsigned char>(ch)); });
}

bool letter_or_digit(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

void wipe(std::string& text) noexcept {
    std::fill(text.begin(), text.end(), '\0');
    text.clear();
    text.shrink_to_fit();
}

}  // namespace

bool cloud_keys_valid(const CloudKeys& keys) noexcept {
    const auto& groq = keys.groq_api_key;
    const bool groq_ok = groq.size() >= 20 && groq.size() <= 200 && groq.rfind("gsk_", 0) == 0 &&
                         only(groq, [](unsigned char ch) { return letter_or_digit(ch) || ch == '_' || ch == '-'; });
    const auto& azure = keys.azure_speech_key;
    const auto& region = keys.azure_speech_region;
    const bool no_azure = azure.empty() && region.empty();
    const bool azure_ok = azure.size() >= 32 && azure.size() <= 128 && only(azure, letter_or_digit) &&
                          region.size() >= 2 && region.size() <= 40 &&
                          only(region, [](unsigned char ch) {
                              return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
                          });
    return groq_ok && (no_azure || azure_ok);
}

void wipe_cloud_keys(CloudKeys& keys) noexcept {
    wipe(keys.groq_api_key);
    wipe(keys.azure_speech_key);
    wipe(keys.azure_speech_region);
}

}  // namespace aura
