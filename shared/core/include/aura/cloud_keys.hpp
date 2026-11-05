#pragma once

#include <string>

namespace aura {

// The keys of the device's own assistant: Groq (speech recognition and answers)
// and Azure Speech (the voice). The owner sends them once from the PC over the
// code-protected LAN session (scripts/send-device-keys.ps1, /api/keys); the
// device keeps them in encrypted NVS. Nothing logs, shows or echoes them.
struct CloudKeys {
    std::string groq_api_key;
    std::string azure_speech_key;
    std::string azure_speech_region;
};

// Whether the keys look like Groq's ("gsk_...") and Azure's (32 to 128 letters
// and digits) and the region like an Azure region. Only the format: the
// services decide whether the keys work. Azure may be left out (key and
// region both empty): AURA then answers without a voice.
bool cloud_keys_valid(const CloudKeys& keys) noexcept;
// Overwrites the key text before its memory is released.
void wipe_cloud_keys(CloudKeys& keys) noexcept;

// Where the platform keeps keys sent from the PC (the device: encrypted NVS).
class ICloudKeySink {
public:
    virtual ~ICloudKeySink() = default;
    virtual bool store(const CloudKeys& keys) = 0;
};

}  // namespace aura
