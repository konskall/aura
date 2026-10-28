#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace aura {

// Decoded MP3 audio: interleaved 16-bit samples.
struct Mp3Pcm {
    std::uint32_t sample_rate{0};
    std::uint8_t channels{0};
    std::vector<std::int16_t> samples;
};

// Incremental MP3 decoding for radio streams (minimp3, CC0). Bytes arrive in
// any chunk size; whole frames come out. Junk between frames is skipped and
// the buffered input is bounded, so a hostile stream cannot grow it.
class Mp3StreamDecoder {
public:
    static constexpr std::size_t kMaxBuffered = 64 * 1024;

    Mp3StreamDecoder();
    ~Mp3StreamDecoder();
    Mp3StreamDecoder(const Mp3StreamDecoder&) = delete;
    Mp3StreamDecoder& operator=(const Mp3StreamDecoder&) = delete;

    void feed(const std::uint8_t* data, std::size_t size);
    // Appends the next decoded frames to `out` (same rate and channels) and
    // returns how many sample frames were added. A format change ends the call
    // early; the next call continues with the new format. `drain` also decodes
    // the last bytes (end of stream) instead of waiting for more.
    std::size_t decode(Mp3Pcm& out, std::size_t max_frames = 1152 * 8, bool drain = false);
    void reset();

private:
    struct State;
    std::unique_ptr<State> state_;
    std::vector<std::uint8_t> input_;
    std::size_t consumed_{0};
};

}  // namespace aura
