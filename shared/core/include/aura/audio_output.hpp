#pragma once

#include <cstddef>
#include <cstdint>

namespace aura {

class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;
    virtual bool available() const noexcept = 0;
    virtual void stop() = 0;
    virtual bool write(const std::int16_t* samples, std::size_t count) = 0;
};

class NullAudioOutput final : public IAudioOutput {
public:
    bool available() const noexcept override { return false; }
    void stop() override {}
    bool write(const std::int16_t*, std::size_t) override { return false; }
};

}  // namespace aura

