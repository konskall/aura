#pragma once

#include "aura/voice_decoder.hpp"

namespace aura::simulator {

// Media Foundation's decoders for voice messages in the simulator: AAC (every
// Windows with Media Foundation) and Opus (only where an Opus decoder is
// installed, such as the Web Media Extensions). Frames come from the shared
// demuxer, so Safari's fragmented MP4 and Firefox's WebM both work.
class WindowsVoiceDecoder final : public IVoiceDecoder {
public:
    bool decode(const std::uint8_t* data, const VoiceTrack& track, PcmClip& clip) override;
};

}  // namespace aura::simulator
