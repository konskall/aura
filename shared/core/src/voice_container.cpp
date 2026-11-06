#include "aura/voice_container.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

namespace aura {
namespace {

constexpr std::size_t kMaxConfigBytes = 256;

struct Reader {
    const std::uint8_t* data;
    std::size_t size;

    bool has(std::size_t at, std::size_t count) const { return at <= size && count <= size - at; }
    std::uint32_t u8(std::size_t at) const { return data[at]; }
    std::uint32_t u16(std::size_t at) const { return (std::uint32_t{data[at]} << 8) | data[at + 1]; }
    std::uint32_t u24(std::size_t at) const { return (u16(at) << 8) | data[at + 2]; }
    std::uint32_t u32(std::size_t at) const { return (u16(at) << 16) | u16(at + 2); }
    std::uint64_t u64(std::size_t at) const { return (std::uint64_t{u32(at)} << 32) | u32(at + 4); }
    bool tag(std::size_t at, const char* four) const { return std::memcmp(data + at, four, 4) == 0; }
};

// ---- MP4 ----------------------------------------------------------------------

struct Box {
    std::size_t start{0};    // the header's first byte
    std::size_t body{0};     // the first byte after the header
    std::size_t end{0};
    char type[5]{};
};

bool next_box(const Reader& in, std::size_t at, std::size_t limit, Box& box) {
    if (!in.has(at, 8) || at + 8 > limit) return false;
    std::uint64_t length = in.u32(at);
    box.start = at;
    box.body = at + 8;
    std::memcpy(box.type, in.data + at + 4, 4);
    box.type[4] = '\0';
    if (length == 1) {
        if (!in.has(at, 16) || at + 16 > limit) return false;
        length = in.u64(at + 8);
        box.body = at + 16;
    } else if (length == 0) {
        length = limit - at;
    }
    if (length < box.body - at || length > limit - at) return false;
    box.end = at + static_cast<std::size_t>(length);
    return true;
}

struct Mp4Track {
    bool sound{false};
    std::uint32_t timescale{0};
    std::uint64_t duration{0};
    VoiceCodec codec{VoiceCodec::None};
    std::uint32_t rate{0};
    std::uint8_t channels{0};
    std::uint16_t pre_skip{0};
    std::vector<std::uint8_t> config;
    std::vector<std::uint32_t> sizes;
    std::vector<std::uint64_t> chunk_offsets;
    std::vector<std::uint32_t> chunk_runs;  // stsc: first_chunk, samples_per_chunk pairs
    std::uint32_t default_size{0};
};

// MPEG-4 descriptors carry an expandable length of up to four bytes.
bool descriptor(const Reader& in, std::size_t& at, std::size_t end, std::uint32_t& tag, std::size_t& length) {
    if (at >= end) return false;
    tag = in.u8(at++);
    length = 0;
    for (int index = 0; index < 4; ++index) {
        if (at >= end) return false;
        const auto byte = in.u8(at++);
        length = (length << 7) | (byte & 0x7F);
        if (!(byte & 0x80)) return at + length <= end;
    }
    return false;
}

bool parse_esds(const Reader& in, const Box& box, Mp4Track& track) {
    std::size_t at = box.body + 4;  // version and flags
    std::uint32_t tag = 0;
    std::size_t length = 0;
    if (!descriptor(in, at, box.end, tag, length) || tag != 0x03 || at + 3 > box.end) return false;
    const auto flags = in.u8(at + 2);
    at += 3;
    if (flags & 0x80) at += 2;                                  // dependsOn_ES_ID
    if ((flags & 0x40) && at < box.end) at += 1 + in.u8(at);    // URL
    if (flags & 0x20) at += 2;                                  // OCR_ES_Id
    if (!descriptor(in, at, box.end, tag, length) || tag != 0x04 || length < 13 || in.u8(at) != 0x40) return false;
    at += 13;
    if (!descriptor(in, at, box.end, tag, length) || tag != 0x05 || length == 0 || length > kMaxConfigBytes)
        return false;
    track.config.assign(in.data + at, in.data + at + length);
    track.codec = VoiceCodec::Aac;
    return true;
}

bool parse_sample_entry(const Reader& in, const Box& entry, Mp4Track& track) {
    const bool aac = std::strcmp(entry.type, "mp4a") == 0;
    const bool opus = std::strcmp(entry.type, "Opus") == 0;
    if ((!aac && !opus) || entry.body + 28 > entry.end) return false;
    track.channels = static_cast<std::uint8_t>(in.u16(entry.body + 16));
    track.rate = in.u32(entry.body + 24) >> 16;
    for (std::size_t at = entry.body + 28; at < entry.end;) {
        Box child;
        if (!next_box(in, at, entry.end, child)) return false;
        if (aac && std::strcmp(child.type, "esds") == 0 && !parse_esds(in, child, track)) return false;
        if (opus && std::strcmp(child.type, "dOps") == 0) {
            if (child.body + 11 > child.end) return false;
            track.codec = VoiceCodec::Opus;
            track.channels = static_cast<std::uint8_t>(in.u8(child.body + 1));
            track.pre_skip = static_cast<std::uint16_t>(in.u16(child.body + 2));
            track.rate = 48000;
        }
        at = child.end;
    }
    return track.codec != VoiceCodec::None && track.channels > 0;
}

bool parse_table(const Reader& in, const Box& box, Mp4Track& track) {
    if (box.body + 8 > box.end) return false;
    const std::size_t at = box.body + 4;
    if (std::strcmp(box.type, "stsd") == 0) {
        Box entry;
        return in.u32(at) == 0 || (next_box(in, at + 4, box.end, entry) && parse_sample_entry(in, entry, track));
    }
    if (std::strcmp(box.type, "stsz") == 0) {
        track.default_size = in.u32(at);
        const std::uint32_t count = in.u32(at + 4);
        if (count > kMaxVoiceFrames) return false;
        if (track.default_size != 0) {
            track.sizes.assign(count, track.default_size);
            return true;
        }
        if (!in.has(at + 8, std::size_t{count} * 4) || at + 8 + std::size_t{count} * 4 > box.end) return false;
        track.sizes.resize(count);
        for (std::uint32_t index = 0; index < count; ++index) track.sizes[index] = in.u32(at + 8 + index * 4);
        return true;
    }
    const std::uint32_t count = in.u32(at);
    if (count > kMaxVoiceFrames) return false;
    if (std::strcmp(box.type, "stsc") == 0) {
        if (at + 4 + std::size_t{count} * 12 > box.end) return false;
        for (std::uint32_t index = 0; index < count; ++index) {
            track.chunk_runs.push_back(in.u32(at + 4 + index * 12));
            track.chunk_runs.push_back(in.u32(at + 8 + index * 12));
        }
        return true;
    }
    if (std::strcmp(box.type, "stco") == 0 || std::strcmp(box.type, "co64") == 0) {
        const std::size_t width = box.type[1] == 't' ? 4 : 8;
        if (at + 4 + std::size_t{count} * width > box.end) return false;
        for (std::uint32_t index = 0; index < count; ++index)
            track.chunk_offsets.push_back(width == 4 ? in.u32(at + 4 + index * 4) : in.u64(at + 4 + index * 8));
        return true;
    }
    return true;
}

bool parse_container(const Reader& in, std::size_t start, std::size_t end, Mp4Track& track) {
    for (std::size_t at = start; at < end;) {
        Box box;
        if (!next_box(in, at, end, box)) return false;
        const std::string_view type(box.type, 4);
        if (type == "mdia" || type == "minf" || type == "stbl") {
            if (!parse_container(in, box.body, box.end, track)) return false;
        } else if (type == "mdhd") {
            if (box.body + 4 > box.end) return false;
            const bool wide = in.u8(box.body) == 1;
            const std::size_t fields = box.body + 4 + (wide ? 16 : 8);
            if (fields + (wide ? 12 : 8) > box.end) return false;
            track.timescale = in.u32(fields);
            track.duration = wide ? in.u64(fields + 4) : in.u32(fields + 4);
        } else if (type == "hdlr") {
            if (box.body + 12 > box.end) return false;
            track.sound = in.tag(box.body + 8, "soun");
        } else if (type == "stsd" || type == "stsz" || type == "stsc" || type == "stco" || type == "co64") {
            if (!parse_table(in, box, track)) return false;
        }
        at = box.end;
    }
    return true;
}

// Whole files: frames from the sample table (stsc runs over stco chunks).
bool table_frames(const Mp4Track& track, std::size_t size, std::vector<VoiceFrame>& frames) {
    if (track.sizes.empty()) return true;
    if (track.chunk_runs.empty() || track.chunk_offsets.empty()) return false;
    std::size_t sample = 0;
    for (std::size_t chunk = 0; chunk < track.chunk_offsets.size() && sample < track.sizes.size(); ++chunk) {
        std::uint32_t per_chunk = 0;
        for (std::size_t run = 0; run + 1 < track.chunk_runs.size(); run += 2) {
            if (track.chunk_runs[run] <= chunk + 1) per_chunk = track.chunk_runs[run + 1];
        }
        std::uint64_t offset = track.chunk_offsets[chunk];
        for (std::uint32_t index = 0; index < per_chunk && sample < track.sizes.size(); ++index, ++sample) {
            const std::uint64_t length = track.sizes[sample];
            if (offset > size || length > size - offset) return false;
            frames.push_back({static_cast<std::size_t>(offset), static_cast<std::size_t>(length)});
            offset += length;
        }
    }
    return sample == track.sizes.size();
}

// Fragmented files: each moof's traf/trun lists sizes; data follows the moof.
bool fragment_frames(const Reader& in, const Box& moof, std::uint32_t default_size, std::vector<VoiceFrame>& frames,
                     std::uint64_t& ticks) {
    for (std::size_t at = moof.body; at < moof.end;) {
        Box traf;
        if (!next_box(in, at, moof.end, traf)) return false;
        at = traf.end;
        if (std::strcmp(traf.type, "traf") != 0) continue;
        std::uint64_t base = moof.start;
        std::uint32_t sample_size = default_size;
        std::uint32_t sample_duration = 0;
        std::uint64_t next_data = 0;
        bool have_next = false;
        for (std::size_t child_at = traf.body; child_at < traf.end;) {
            Box child;
            if (!next_box(in, child_at, traf.end, child)) return false;
            child_at = child.end;
            if (child.body + 4 > child.end) return false;
            const std::uint32_t flags = in.u24(child.body + 1);
            std::size_t field = child.body + 4;
            if (std::strcmp(child.type, "tfhd") == 0) {
                field += 4;  // track_ID
                if (flags & 0x000001) {
                    if (field + 8 > child.end) return false;
                    base = in.u64(field);
                    field += 8;
                }
                if (flags & 0x000002) field += 4;
                if (flags & 0x000008) {
                    if (field + 4 > child.end) return false;
                    sample_duration = in.u32(field);
                    field += 4;
                }
                if (flags & 0x000010) {
                    if (field + 4 > child.end) return false;
                    sample_size = in.u32(field);
                }
            } else if (std::strcmp(child.type, "trun") == 0) {
                if (field + 4 > child.end) return false;
                const std::uint32_t count = in.u32(field);
                field += 4;
                std::uint64_t data = have_next ? next_data : base;
                if (flags & 0x000001) {
                    if (field + 4 > child.end) return false;
                    data = base + static_cast<std::int64_t>(static_cast<std::int32_t>(in.u32(field)));
                    field += 4;
                }
                if (flags & 0x000004) field += 4;
                const std::size_t per_sample = ((flags & 0x100) ? 4 : 0) + ((flags & 0x200) ? 4 : 0) +
                                               ((flags & 0x400) ? 4 : 0) + ((flags & 0x800) ? 4 : 0);
                if (count > kMaxVoiceFrames || frames.size() + count > kMaxVoiceFrames ||
                    field + std::size_t{count} * per_sample > child.end)
                    return false;
                for (std::uint32_t index = 0; index < count; ++index) {
                    std::uint32_t duration = sample_duration;
                    std::uint32_t length = sample_size;
                    if (flags & 0x100) {
                        duration = in.u32(field);
                        field += 4;
                    }
                    if (flags & 0x200) {
                        length = in.u32(field);
                        field += 4;
                    }
                    field += ((flags & 0x400) ? 4 : 0) + ((flags & 0x800) ? 4 : 0);
                    if (data > in.size || length > in.size - data || length == 0) return false;
                    frames.push_back({static_cast<std::size_t>(data), length});
                    data += length;
                    ticks += duration;
                }
                next_data = data;
                have_next = true;
            }
        }
    }
    return true;
}

bool demux_mp4(const Reader& in, VoiceTrack& out) {
    Mp4Track track;
    std::uint32_t trex_size = 0;
    bool found = false;
    std::vector<VoiceFrame> fragments;
    std::uint64_t ticks = 0;
    for (std::size_t at = 0; at < in.size;) {
        Box box;
        if (!next_box(in, at, in.size, box)) return false;
        if (std::strcmp(box.type, "moov") == 0) {
            for (std::size_t child_at = box.body; child_at < box.end;) {
                Box child;
                if (!next_box(in, child_at, box.end, child)) return false;
                child_at = child.end;
                if (std::strcmp(child.type, "trak") == 0 && !found) {
                    Mp4Track candidate;
                    if (!parse_container(in, child.body, child.end, candidate)) return false;
                    if (candidate.sound && candidate.codec != VoiceCodec::None) {
                        track = std::move(candidate);
                        found = true;
                    }
                } else if (std::strcmp(child.type, "mvex") == 0) {
                    Box trex;
                    if (next_box(in, child.body, child.end, trex) && std::strcmp(trex.type, "trex") == 0 &&
                        trex.body + 24 <= trex.end)
                        trex_size = in.u32(trex.body + 16);
                }
            }
        } else if (std::strcmp(box.type, "moof") == 0) {
            if (!fragment_frames(in, box, trex_size, fragments, ticks)) return false;
        }
        at = box.end;
    }
    if (!found) return false;
    std::vector<VoiceFrame> frames;
    if (!table_frames(track, in.size, frames)) return false;
    frames.insert(frames.end(), fragments.begin(), fragments.end());
    if (frames.empty() || frames.size() > kMaxVoiceFrames) return false;
    out.codec = track.codec;
    out.sample_rate = track.rate;
    out.channels = track.channels;
    out.pre_skip = track.pre_skip;
    out.config = std::move(track.config);
    out.frames = std::move(frames);
    const std::uint64_t units = ticks ? ticks : track.duration;
    out.duration_ms = track.timescale ? static_cast<std::uint32_t>(units * 1000 / track.timescale) : 0;
    return true;
}

// ---- WebM (EBML) ------------------------------------------------------------

constexpr std::uint32_t kSegment = 0x18538067;
constexpr std::uint32_t kCluster = 0x1F43B675;
constexpr std::uint32_t kTracks = 0x1654AE6B;
constexpr std::uint32_t kInfo = 0x1549A966;

bool read_vint(const Reader& in, std::size_t& at, std::size_t end, std::uint64_t& value, bool keep_marker,
               bool& unknown) {
    if (at >= end) return false;
    const auto first = in.u8(at);
    std::size_t length = 1;
    while (length <= 8 && !(first & (0x80U >> (length - 1)))) ++length;
    if (length > 8 || at + length > end) return false;
    value = keep_marker ? first : (first & (0xFFU >> length));
    bool all_ones = (first & (0xFFU >> length)) == (0xFFU >> length);
    for (std::size_t index = 1; index < length; ++index) {
        value = (value << 8) | in.u8(at + index);
        all_ones = all_ones && in.u8(at + index) == 0xFF;
    }
    unknown = !keep_marker && all_ones;
    at += length;
    return true;
}

struct Element {
    std::uint32_t id{0};
    std::size_t body{0};
    std::size_t end{0};
    bool unknown{false};
};

bool next_element(const Reader& in, std::size_t at, std::size_t end, Element& element) {
    std::uint64_t id = 0;
    std::uint64_t size = 0;
    bool unknown = false;
    if (!read_vint(in, at, end, id, true, unknown) || id > 0xFFFFFFFFU) return false;
    if (!read_vint(in, at, end, size, false, unknown)) return false;
    element.id = static_cast<std::uint32_t>(id);
    element.body = at;
    element.unknown = unknown;
    if (unknown) {
        element.end = end;
    } else {
        if (size > end - at) return false;
        element.end = at + static_cast<std::size_t>(size);
    }
    return true;
}

std::uint64_t uint_of(const Reader& in, const Element& element) {
    std::uint64_t value = 0;
    for (std::size_t at = element.body; at < element.end && at < element.body + 8; ++at) value = (value << 8) | in.u8(at);
    return value;
}

double float_of(const Reader& in, const Element& element) {
    const std::size_t length = element.end - element.body;
    if (length == 4) {
        const std::uint32_t bits = in.u32(element.body);
        float value = 0;
        std::memcpy(&value, &bits, 4);
        return value;
    }
    if (length == 8) {
        const std::uint64_t bits = in.u64(element.body);
        double value = 0;
        std::memcpy(&value, &bits, 8);
        return value;
    }
    return 0;
}

bool is_segment_child(std::uint32_t id) {
    return id == kCluster || id == kTracks || id == kInfo || id == 0x1C53BB6B || id == 0x1254C367 ||
           id == 0x114D9B74 || id == 0x1043A770 || id == 0x1941A469;
}

struct WebmTrack {
    std::uint64_t number{0};
    bool opus{false};
    std::uint8_t channels{0};
    std::uint32_t rate{0};
    std::vector<std::uint8_t> config;
    std::uint64_t timecode_scale{1000000};
    std::int64_t first_ms{-1};
    std::int64_t last_ms{0};
};

bool parse_tracks(const Reader& in, const Element& tracks, WebmTrack& track) {
    for (std::size_t at = tracks.body; at < tracks.end;) {
        Element entry;
        if (!next_element(in, at, tracks.end, entry) || entry.unknown) return false;
        at = entry.end;
        if (entry.id != 0xAE || track.opus) continue;
        WebmTrack candidate = track;
        for (std::size_t field_at = entry.body; field_at < entry.end;) {
            Element field;
            if (!next_element(in, field_at, entry.end, field) || field.unknown) return false;
            field_at = field.end;
            if (field.id == 0xD7) candidate.number = uint_of(in, field);
            if (field.id == 0x86) {
                const std::string codec(reinterpret_cast<const char*>(in.data + field.body), field.end - field.body);
                candidate.opus = codec == "A_OPUS";
            }
            if (field.id == 0x63A2) {
                if (field.end - field.body > kMaxConfigBytes) return false;
                candidate.config.assign(in.data + field.body, in.data + field.end);
            }
            if (field.id == 0xE1) {
                for (std::size_t audio_at = field.body; audio_at < field.end;) {
                    Element audio;
                    if (!next_element(in, audio_at, field.end, audio) || audio.unknown) return false;
                    audio_at = audio.end;
                    if (audio.id == 0xB5) candidate.rate = static_cast<std::uint32_t>(float_of(in, audio));
                    if (audio.id == 0x9F) candidate.channels = static_cast<std::uint8_t>(uint_of(in, audio));
                }
            }
        }
        if (candidate.opus) track = std::move(candidate);
    }
    return true;
}

bool parse_cluster(const Reader& in, std::size_t start, std::size_t end, WebmTrack& track,
                   std::vector<VoiceFrame>& frames, std::size_t& stopped) {
    std::int64_t cluster_time = 0;
    std::size_t at = start;
    while (at < end) {
        Element element;
        const std::size_t here = at;
        if (!next_element(in, at, end, element)) return false;
        if (is_segment_child(element.id)) {  // an unknown-size cluster ends where its sibling begins
            stopped = here;
            return true;
        }
        at = element.end;
        if (element.id == 0xE7) cluster_time = static_cast<std::int64_t>(uint_of(in, element));
        Element block = element;
        if (element.id == 0xA0) {  // BlockGroup: its Block
            bool found = false;
            for (std::size_t inner = element.body; inner < element.end;) {
                Element child;
                if (!next_element(in, inner, element.end, child) || child.unknown) return false;
                inner = child.end;
                if (child.id == 0xA1) {
                    block = child;
                    found = true;
                }
            }
            if (!found) continue;
        } else if (element.id != 0xA3) {
            continue;
        }
        std::size_t cursor = block.body;
        std::uint64_t number = 0;
        bool unknown = false;
        if (!read_vint(in, cursor, block.end, number, false, unknown) || cursor + 3 > block.end) return false;
        if (number != track.number) continue;
        const auto relative = static_cast<std::int16_t>(in.u16(cursor));
        const auto flags = in.u8(cursor + 2);
        cursor += 3;
        if (flags & 0x06) return false;  // laced blocks: MediaRecorder writes one packet per block
        if (cursor >= block.end || frames.size() >= kMaxVoiceFrames) return false;
        frames.push_back({cursor, block.end - cursor});
        const std::int64_t when = (cluster_time + relative) * static_cast<std::int64_t>(track.timecode_scale) / 1000000;
        if (track.first_ms < 0) track.first_ms = when;
        track.last_ms = when;
    }
    stopped = end;
    return true;
}

bool demux_webm(const Reader& in, VoiceTrack& out) {
    Element header;
    if (!next_element(in, 0, in.size, header) || header.id != 0x1A45DFA3 || header.unknown) return false;
    Element segment;
    if (!next_element(in, header.end, in.size, segment) || segment.id != kSegment) return false;
    WebmTrack track;
    std::vector<VoiceFrame> frames;
    std::size_t at = segment.body;
    while (at < segment.end) {
        Element element;
        if (!next_element(in, at, segment.end, element)) return false;
        if (element.id == kTracks) {
            if (element.unknown || !parse_tracks(in, element, track)) return false;
            at = element.end;
        } else if (element.id == kInfo) {
            for (std::size_t field_at = element.body; field_at < element.end;) {
                Element field;
                if (!next_element(in, field_at, element.end, field) || field.unknown) return false;
                field_at = field.end;
                if (field.id == 0x2AD7B1) track.timecode_scale = std::max<std::uint64_t>(1, uint_of(in, field));
            }
            at = element.end;
        } else if (element.id == kCluster) {
            if (!track.opus) return false;  // a cluster before its track
            std::size_t stopped = element.end;
            if (!parse_cluster(in, element.body, element.end, track, frames, stopped)) return false;
            at = element.unknown ? stopped : element.end;
        } else {
            if (element.unknown) return false;
            at = element.end;
        }
    }
    if (!track.opus || frames.empty() || track.config.size() < 19) return false;
    out.codec = VoiceCodec::Opus;
    out.sample_rate = 48000;
    out.channels = track.channels ? track.channels : static_cast<std::uint8_t>(track.config[9]);
    out.pre_skip = static_cast<std::uint16_t>(track.config[10] | (track.config[11] << 8));
    out.config = std::move(track.config);
    out.frames = std::move(frames);
    out.duration_ms = static_cast<std::uint32_t>(std::max<std::int64_t>(0, track.last_ms - track.first_ms) + 20);
    return true;
}

// ---- WAV ----------------------------------------------------------------------

std::uint32_t le32(const Reader& in, std::size_t at) {
    return in.u8(at) | (in.u8(at + 1) << 8) | (in.u8(at + 2) << 16) | (in.u8(at + 3) << 24);
}
std::uint32_t le16(const Reader& in, std::size_t at) { return in.u8(at) | (in.u8(at + 1) << 8); }

bool demux_wav(const Reader& in, VoiceTrack& out) {
    if (in.size < 12 || !in.tag(0, "RIFF") || !in.tag(8, "WAVE")) return false;
    std::uint32_t rate = 0;
    std::uint32_t channels = 0;
    bool format = false;
    for (std::size_t at = 12; in.has(at, 8);) {
        const std::uint32_t length = le32(in, at + 4);
        const std::size_t body = at + 8;
        if (!in.has(body, length)) return false;
        if (in.tag(at, "fmt ")) {
            if (length < 16 || le16(in, body) != 1 || le16(in, body + 14) != 16) return false;
            channels = le16(in, body + 2);
            rate = le32(in, body + 4);
            format = channels >= 1 && channels <= 2 && rate >= 8000 && rate <= 48000;
        } else if (in.tag(at, "data")) {
            if (!format || length == 0 || length % (2 * channels) != 0) return false;
            out.codec = VoiceCodec::Pcm16;
            out.sample_rate = rate;
            out.channels = static_cast<std::uint8_t>(channels);
            out.frames = {{body, length}};
            out.duration_ms = static_cast<std::uint32_t>(std::uint64_t{length} / (2 * channels) * 1000 / rate);
            return true;
        }
        at = body + length + (length & 1);
    }
    return false;
}

}  // namespace

bool voice_demux(const std::uint8_t* data, std::size_t size, VoiceTrack& track) {
    track = {};
    if (data == nullptr || size < 12) return false;
    const Reader in{data, size};
    VoiceTrack parsed;
    bool ok = false;
    if (in.tag(0, "RIFF")) ok = demux_wav(in, parsed);
    else if (in.u32(0) == 0x1A45DFA3) ok = demux_webm(in, parsed);
    else if (in.tag(4, "ftyp")) ok = demux_mp4(in, parsed);
    if (!ok) return false;
    track = std::move(parsed);
    return true;
}

bool voice_adts(const std::vector<std::uint8_t>& audio_specific_config, const std::uint8_t* frame, std::size_t size,
                std::vector<std::uint8_t>& adts) {
    adts.clear();
    if (audio_specific_config.size() < 2 || frame == nullptr || size == 0 || size + 7 > 0x1FFF) return false;
    const unsigned object = audio_specific_config[0] >> 3;
    const unsigned frequency = ((audio_specific_config[0] & 7U) << 1) | (audio_specific_config[1] >> 7);
    const unsigned channels = (audio_specific_config[1] >> 3) & 15U;
    if (object < 1 || object > 4 || frequency > 12 || channels == 0 || channels > 7) return false;
    const std::size_t length = size + 7;
    adts = {0xFF, 0xF1,
            static_cast<std::uint8_t>(((object - 1) << 6) | (frequency << 2) | (channels >> 2)),
            static_cast<std::uint8_t>(((channels & 3U) << 6) | (length >> 11)),
            static_cast<std::uint8_t>((length >> 3) & 0xFF),
            static_cast<std::uint8_t>(((length & 7U) << 5) | 0x1F),
            0xFC};
    adts.insert(adts.end(), frame, frame + size);
    return true;
}

bool voice_wav(const std::int16_t* samples, std::size_t count, std::uint32_t sample_rate, std::vector<std::uint8_t>& wav) {
    wav.clear();
    if (samples == nullptr || count == 0 || count > 48000U * 600U || sample_rate < 8000 || sample_rate > 48000)
        return false;
    const auto put32 = [&](std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) wav.push_back(static_cast<std::uint8_t>(value >> shift));
    };
    const auto put16 = [&](std::uint32_t value) {
        wav.push_back(static_cast<std::uint8_t>(value));
        wav.push_back(static_cast<std::uint8_t>(value >> 8));
    };
    const auto data = static_cast<std::uint32_t>(count * 2);
    wav.reserve(44 + data);
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
    put32(36 + data);
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(16);
    put16(1);
    put16(1);
    put32(sample_rate);
    put32(sample_rate * 2);
    put16(2);
    put16(16);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'});
    put32(data);
    for (std::size_t index = 0; index < count; ++index) put16(static_cast<std::uint16_t>(samples[index]));
    return true;
}

}  // namespace aura

#include "aura/voice_decoder.hpp"

namespace aura {

VoiceResampler::VoiceResampler(std::uint32_t in_rate, std::uint32_t channels, std::uint32_t out_rate)
    : in_rate_(in_rate ? in_rate : 1), channels_(channels ? channels : 1), out_rate_(out_rate ? out_rate : 1) {}

void VoiceResampler::push(const std::int16_t* interleaved, std::size_t frames) {
    if (!interleaved) return;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        std::int32_t mixed = 0;
        for (std::uint32_t channel = 0; channel < channels_; ++channel) mixed += interleaved[frame * channels_ + channel];
        mixed /= static_cast<std::int32_t>(channels_);
        if (in_rate_ <= out_rate_) {
            output_.push_back(static_cast<std::int16_t>(mixed));
            continue;
        }
        sum_ += mixed;
        ++count_;
        position_ += out_rate_;
        if (position_ >= in_rate_) {
            output_.push_back(static_cast<std::int16_t>(sum_ / static_cast<std::int64_t>(count_)));
            position_ -= in_rate_;
            sum_ = 0;
            count_ = 0;
        }
    }
}

bool voice_decode(const std::uint8_t* data, std::size_t size, IVoiceDecoder* decoder, PcmClip& clip,
                  std::string& error, std::uint32_t max_seconds) {
    clip = {};
    VoiceTrack track;
    if (!voice_demux(data, size, track)) {
        error = "VOICE_DAMAGED";
        return false;
    }
    if (track.duration_ms > max_seconds * 1000U) {
        error = "VOICE_TOO_LONG";
        return false;
    }
    PcmClip decoded;
    if (track.codec == VoiceCodec::Pcm16) {
        const auto& frame = track.frames.front();
        const std::size_t count = frame.size / 2;
        decoded.format.sample_rate = track.sample_rate;
        decoded.format.channels = 1;
        decoded.samples.reserve(count / track.channels);
        for (std::size_t index = 0; index + track.channels <= count; index += track.channels) {
            std::int32_t sum = 0;
            for (std::size_t channel = 0; channel < track.channels; ++channel) {
                const std::size_t at = frame.offset + (index + channel) * 2;
                sum += static_cast<std::int16_t>(static_cast<std::uint16_t>(data[at] | (data[at + 1] << 8)));
            }
            decoded.samples.push_back(static_cast<std::int16_t>(sum / track.channels));
        }
    } else if (!decoder || !decoder->decode(data, track, decoded) || decoded.samples.empty()) {
        error = "VOICE_UNSUPPORTED";
        return false;
    }
    if (decoded.format.sample_rate > 16000) {
        VoiceResampler resampler(decoded.format.sample_rate, std::max<std::uint32_t>(1, decoded.format.channels));
        resampler.push(decoded.samples.data(),
                       decoded.samples.size() / std::max<std::uint32_t>(1, decoded.format.channels));
        decoded.samples = std::move(resampler.output());
        decoded.format.sample_rate = 16000;
        decoded.format.channels = 1;
    }
    const std::uint32_t rate = decoded.format.sample_rate;
    if (rate == 0 || decoded.samples.size() / std::max<std::uint32_t>(1, decoded.format.channels) >
                         std::size_t{rate} * max_seconds) {
        error = rate == 0 ? "VOICE_UNSUPPORTED" : "VOICE_TOO_LONG";
        return false;
    }
    clip = std::move(decoded);
    error.clear();
    return true;
}

}  // namespace aura
