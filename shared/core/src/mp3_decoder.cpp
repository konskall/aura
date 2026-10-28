#include "aura/mp3_decoder.hpp"

#include <algorithm>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "../../third_party/minimp3/minimp3.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace aura {

struct Mp3StreamDecoder::State {
    mp3dec_t decoder{};
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME]{};
};

Mp3StreamDecoder::Mp3StreamDecoder() : state_(std::make_unique<State>()) { mp3dec_init(&state_->decoder); }

Mp3StreamDecoder::~Mp3StreamDecoder() = default;

void Mp3StreamDecoder::reset() {
    mp3dec_init(&state_->decoder);
    input_.clear();
    consumed_ = 0;
}

void Mp3StreamDecoder::feed(const std::uint8_t* data, std::size_t size) {
    if (!data || size == 0) return;
    if (consumed_ > 0) {
        input_.erase(input_.begin(), input_.begin() + static_cast<std::ptrdiff_t>(consumed_));
        consumed_ = 0;
    }
    // A stream that never yields a frame cannot grow the buffer without bound.
    if (input_.size() + size > kMaxBuffered) {
        const auto drop = std::min(input_.size(), input_.size() + size - kMaxBuffered);
        input_.erase(input_.begin(), input_.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    input_.insert(input_.end(), data, data + std::min(size, kMaxBuffered));
}

std::size_t Mp3StreamDecoder::decode(Mp3Pcm& out, std::size_t max_frames, bool drain) {
    // A frame cut off at the end of the buffer would be skipped as junk, so
    // two of the largest frames must be waiting unless the stream has ended.
    constexpr std::size_t kLookahead = 2 * 1441;
    std::size_t added = 0;
    while (added < max_frames && consumed_ < input_.size()) {
        const auto available = input_.size() - consumed_;
        if (!drain && available < kLookahead) break;
        mp3dec_frame_info_t info{};
        const int samples = mp3dec_decode_frame(&state_->decoder, input_.data() + consumed_,
                                                static_cast<int>(available), state_->pcm, &info);
        if (info.frame_bytes == 0) break;  // needs more data
        if (samples > 0 && info.channels > 0 && info.hz > 0) {
            const auto rate = static_cast<std::uint32_t>(info.hz);
            const auto channels = static_cast<std::uint8_t>(info.channels);
            if (out.sample_rate != 0 && (out.sample_rate != rate || out.channels != channels)) break;
            out.sample_rate = rate;
            out.channels = channels;
            out.samples.insert(out.samples.end(), state_->pcm, state_->pcm + samples * info.channels);
            added += static_cast<std::size_t>(samples);
        }
        consumed_ += static_cast<std::size_t>(info.frame_bytes);
    }
    return added;
}

}  // namespace aura
