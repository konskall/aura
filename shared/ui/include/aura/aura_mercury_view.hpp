#pragma once

#include <array>
#include <cstdint>

#include "lvgl.h"

namespace aura::ui {

enum class MercuryState { Idle, Listening, Thinking, Speaking };

class AuraMercuryView {
public:
    static constexpr int size = 176;
    static constexpr std::uint32_t frame_interval_ms = 66;

    AuraMercuryView();

    lv_obj_t* attach(lv_obj_t* parent, int x, int y);
    void detach() noexcept;
    void set_state(MercuryState state) noexcept;
    void set_level(float level) noexcept;
    void set_reduced_motion(bool reduced) noexcept;
    bool tick(std::uint32_t elapsed_ms);

    std::uint64_t frame_count() const noexcept { return frame_count_; }
    std::size_t buffer_bytes() const noexcept { return buffer_.size(); }
    lv_obj_t* object() const noexcept { return image_; }

private:
    void render();
    static std::uint16_t rgb565(int red, int green, int blue) noexcept;
    static int wave(int phase) noexcept;

    alignas(4) std::array<std::uint8_t, size * size * 3> buffer_{};
    lv_image_dsc_t descriptor_{};
    lv_obj_t* image_{};
    MercuryState state_{MercuryState::Idle};
    float level_{};
    std::uint32_t accumulator_ms_{};
    std::uint32_t phase_{};
    std::uint64_t frame_count_{};
    bool reduced_motion_{};
};

}  // namespace aura::ui
