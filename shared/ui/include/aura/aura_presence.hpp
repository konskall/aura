#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "aura/appearance.hpp"
#include "lvgl.h"

namespace aura::ui {

enum class PresenceState : std::uint8_t { Idle, Listening, Thinking, Speaking };

// The assistant's animated presence on Home and in the assistant states. Each
// appearance has its own; exactly one is attached at a time, on the UI thread.
class PresenceView {
public:
    virtual ~PresenceView() = default;
    // Creates the presence's LVGL objects centred on (center_x, center_y).
    virtual lv_obj_t* attach(lv_obj_t* parent, int center_x, int center_y) = 0;
    // Forgets the objects; their parent owns and deletes them.
    virtual void detach() noexcept = 0;
    virtual void set_state(PresenceState state) noexcept = 0;
    virtual void set_level(float level) noexcept = 0;  // 0..1: microphone or speech envelope
    virtual void set_reduced_motion(bool reduced) noexcept = 0;
    // Advances the animation; true when a new frame was drawn.
    virtual bool tick(std::uint32_t elapsed_ms) = 0;
    virtual lv_obj_t* object() const noexcept = 0;
    virtual std::uint64_t frame_count() const noexcept = 0;
    virtual std::size_t buffer_bytes() const noexcept = 0;
};

std::unique_ptr<PresenceView> make_presence(Appearance appearance);

}  // namespace aura::ui
