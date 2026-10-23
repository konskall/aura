#include "aura_board_profile.h"

#include "bsp/esp-bsp.h"

static_assert(aura_board::Display::width == 480);
static_assert(aura_board::Display::height == 480);
static_assert(aura_board::Display::partial_buffer_lines < aura_board::Display::height);
static_assert(aura_board::Pins::i2c_scl == BSP_I2C_SCL);
static_assert(aura_board::Pins::i2c_sda == BSP_I2C_SDA);
static_assert(aura_board::Pins::i2s_master_clock == BSP_I2S_MCLK);
static_assert(aura_board::Pins::i2s_bit_clock == BSP_I2S_SCLK);
static_assert(aura_board::Pins::i2s_word_select == BSP_I2S_LCLK);
static_assert(aura_board::Pins::audio_playback_data == BSP_I2S_DOUT);
static_assert(aura_board::Pins::audio_capture_data == BSP_I2S_DSIN);
static_assert(aura_board::Pins::amplifier_enable == BSP_POWER_AMP_IO);
