// The containers aoraton's browsers record voice messages in: MP4 (Safari's
// AAC, Chrome's Opus or AAC; whole or fragmented), WebM (Firefox's Opus, often
// with unknown sizes) and WAV (AURA's own). The test builds each container
// from the specifications and checks the frames come back exactly.
#include "aura/voice_container.hpp"
#include "aura/voice_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
void expect(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

using Bytes = std::vector<std::uint8_t>;

void put32(Bytes& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
}
void put16(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}
Bytes box(const char* type, const Bytes& body) {
    Bytes out;
    put32(out, static_cast<std::uint32_t>(body.size() + 8));
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
Bytes full_box(const char* type, std::uint8_t version, std::uint32_t flags, const Bytes& body) {
    Bytes inner;
    inner.push_back(version);
    inner.push_back(static_cast<std::uint8_t>(flags >> 16));
    inner.push_back(static_cast<std::uint8_t>(flags >> 8));
    inner.push_back(static_cast<std::uint8_t>(flags));
    inner.insert(inner.end(), body.begin(), body.end());
    return box(type, inner);
}
Bytes cat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
    return out;
}

// AAC-LC, 44.1 kHz, mono: AudioSpecificConfig 0x12 0x08.
const Bytes kAsc = {0x12, 0x08};
// OpusHead: version 1, 1 channel, pre-skip 312, 48 kHz input, gain 0, family 0.
const Bytes kOpusHead = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, 1, 0x38, 0x01, 0x80, 0xBB, 0, 0, 0, 0, 0};

Bytes esds() {
    // ES_Descriptor(3) > DecoderConfigDescriptor(4, AAC 0x40) > DecoderSpecificInfo(5) = ASC.
    Bytes specific = {0x05, static_cast<std::uint8_t>(kAsc.size())};
    specific.insert(specific.end(), kAsc.begin(), kAsc.end());
    Bytes config = {0x04, static_cast<std::uint8_t>(13 + specific.size()), 0x40, 0x15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    config.insert(config.end(), specific.begin(), specific.end());
    Bytes es = {0x03, static_cast<std::uint8_t>(3 + config.size()), 0, 1, 0};
    es.insert(es.end(), config.begin(), config.end());
    return full_box("esds", 0, 0, es);
}

Bytes audio_entry(const char* type, std::uint32_t rate, const Bytes& extension) {
    Bytes body(6, 0);
    put16(body, 1);              // data reference index
    body.insert(body.end(), 8, 0);  // reserved
    put16(body, 1);              // channels
    put16(body, 16);             // sample size
    put32(body, 0);              // pre-defined, reserved
    put32(body, rate << 16);     // 16.16 sample rate
    body.insert(body.end(), extension.begin(), extension.end());
    return box(type, body);
}

Bytes dops() {
    // OpusSpecificBox: version 0, 1 channel, pre-skip 312, 48 kHz, gain 0, family 0.
    return box("dOps", {0, 1, 0x01, 0x38, 0, 0, 0xBB, 0x80, 0, 0, 0});
}

Bytes track(const Bytes& sample_entry, const Bytes& sample_table_rest, std::uint32_t timescale,
            std::uint32_t duration) {
    Bytes mdhd_body;
    put32(mdhd_body, 0);
    put32(mdhd_body, 0);
    put32(mdhd_body, timescale);
    put32(mdhd_body, duration);
    put32(mdhd_body, 0);
    Bytes hdlr_body;
    put32(hdlr_body, 0);
    hdlr_body.insert(hdlr_body.end(), {'s', 'o', 'u', 'n'});
    hdlr_body.insert(hdlr_body.end(), 13, 0);
    Bytes stsd_body;
    put32(stsd_body, 1);
    stsd_body.insert(stsd_body.end(), sample_entry.begin(), sample_entry.end());
    const Bytes stbl = box("stbl", cat({full_box("stsd", 0, 0, stsd_body), sample_table_rest}));
    const Bytes minf = box("minf", stbl);
    const Bytes mdia = box("mdia", cat({full_box("mdhd", 0, 0, mdhd_body), full_box("hdlr", 0, 0, hdlr_body), minf}));
    return box("trak", mdia);
}

const std::vector<Bytes> kFrames = {{0x21, 0x10, 0x05}, {0x21, 0x10, 0x06, 0x07}, {0x21, 0x22}};

// A whole MP4: ftyp, moov with a sample table, mdat with the three frames.
Bytes whole_mp4(const Bytes& sample_entry, std::uint32_t timescale, std::uint32_t duration) {
    const Bytes ftyp = box("ftyp", {'M', '4', 'A', ' ', 0, 0, 0, 0, 'M', '4', 'A', ' '});
    Bytes payload;
    for (const auto& frame : kFrames) payload.insert(payload.end(), frame.begin(), frame.end());
    const auto tables = [&](std::uint32_t chunk_offset) {
        Bytes stsz;
        put32(stsz, 0);
        put32(stsz, static_cast<std::uint32_t>(kFrames.size()));
        for (const auto& frame : kFrames) put32(stsz, static_cast<std::uint32_t>(frame.size()));
        Bytes stsc;
        put32(stsc, 1);
        put32(stsc, 1);
        put32(stsc, static_cast<std::uint32_t>(kFrames.size()));
        put32(stsc, 1);
        Bytes stco;
        put32(stco, 1);
        put32(stco, chunk_offset);
        return cat({full_box("stsz", 0, 0, stsz), full_box("stsc", 0, 0, stsc), full_box("stco", 0, 0, stco)});
    };
    // The chunk offset depends on the moov's size, which the offset does not change.
    const Bytes probe = box("moov", track(sample_entry, tables(0), timescale, duration));
    const auto offset = static_cast<std::uint32_t>(ftyp.size() + probe.size() + 8);
    const Bytes moov = box("moov", track(sample_entry, tables(offset), timescale, duration));
    return cat({ftyp, moov, box("mdat", payload)});
}

// A fragmented MP4 as MediaRecorder writes it: an empty sample table, then one
// moof+mdat per fragment with the sizes in trun and default-base-is-moof.
Bytes fragmented_mp4(const Bytes& sample_entry, std::uint32_t timescale) {
    const Bytes ftyp = box("ftyp", {'i', 's', 'o', '5', 0, 0, 0, 0, 'i', 's', 'o', '5'});
    Bytes empty_tables;
    {
        Bytes zero;
        put32(zero, 0);
        put32(zero, 0);
        empty_tables = cat({full_box("stsz", 0, 0, zero), full_box("stsc", 0, 0, Bytes(4, 0)),
                            full_box("stco", 0, 0, Bytes(4, 0))});
    }
    Bytes trex;
    put32(trex, 1);
    put32(trex, 1);
    put32(trex, 0);
    put32(trex, 0);
    put32(trex, 0);
    const Bytes moov =
        box("moov", cat({track(sample_entry, empty_tables, timescale, 0), box("mvex", full_box("trex", 0, 0, trex))}));
    Bytes out = cat({ftyp, moov});
    for (std::size_t first = 0; first < kFrames.size(); first += 2) {
        const std::size_t count = std::min<std::size_t>(2, kFrames.size() - first);
        Bytes payload;
        Bytes trun_body;
        put32(trun_body, static_cast<std::uint32_t>(count));
        const std::size_t data_offset_at = trun_body.size();
        put32(trun_body, 0);  // data offset, patched below
        for (std::size_t index = first; index < first + count; ++index) {
            put32(trun_body, 1024);  // duration
            put32(trun_body, static_cast<std::uint32_t>(kFrames[index].size()));
            payload.insert(payload.end(), kFrames[index].begin(), kFrames[index].end());
        }
        Bytes tfhd;
        put32(tfhd, 1);
        const auto build = [&](const Bytes& trun) {
            return box("moof", cat({full_box("mfhd", 0, 0, Bytes(4, 0)),
                                    box("traf", cat({full_box("tfhd", 0, 0x020000, tfhd),
                                                     full_box("trun", 0, 0x000301, trun)}))}));
        };
        const Bytes probe = build(trun_body);
        const auto offset = static_cast<std::uint32_t>(probe.size() + 8);
        for (int byte = 0; byte < 4; ++byte)
            trun_body[data_offset_at + static_cast<std::size_t>(byte)] =
                static_cast<std::uint8_t>(offset >> (24 - 8 * byte));
        out = cat({out, build(trun_body), box("mdat", payload)});
    }
    return out;
}

// EBML: an element with a one-byte size, or an unknown size (0x01FFFFFFFFFFFFFF).
Bytes element(std::initializer_list<std::uint8_t> id, const Bytes& body, bool unknown_size = false) {
    Bytes out(id);
    if (unknown_size) {
        out.insert(out.end(), {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    } else {
        out.push_back(static_cast<std::uint8_t>(0x80 | body.size()));
    }
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

Bytes webm(bool unknown_sizes) {
    const Bytes header = element({0x1A, 0x45, 0xDF, 0xA3}, element({0x42, 0x82}, {'w', 'e', 'b', 'm'}));
    const Bytes rate = {0x40, 0xE7, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00};  // 48000.0 as a double
    const Bytes audio = element({0xE1}, cat({element({0xB5}, rate), element({0x9F}, {1})}));
    const Bytes entry = element({0xAE}, cat({element({0xD7}, {1}), element({0x86}, {'A', '_', 'O', 'P', 'U', 'S'}),
                                             element({0x63, 0xA2}, kOpusHead), audio}));
    const Bytes tracks = element({0x16, 0x54, 0xAE, 0x6B}, entry);
    Bytes cluster_body = element({0xE7}, {0});
    for (std::size_t index = 0; index < kFrames.size(); ++index) {
        Bytes block = {0x81, 0, static_cast<std::uint8_t>(index * 20), 0x80};
        block.insert(block.end(), kFrames[index].begin(), kFrames[index].end());
        cluster_body = cat({cluster_body, element({0xA3}, block)});
    }
    const Bytes cluster = element({0x1F, 0x43, 0xB6, 0x75}, cluster_body, unknown_sizes);
    const Bytes segment = element({0x18, 0x53, 0x80, 0x67}, cat({tracks, cluster}), unknown_sizes);
    return cat({header, segment});
}

void same_frames(const aura::VoiceTrack& track, const Bytes& source, const char* what) {
    expect(track.frames.size() == kFrames.size(), what);
    for (std::size_t index = 0; index < kFrames.size(); ++index) {
        const auto& frame = track.frames[index];
        expect(frame.offset + frame.size <= source.size() &&
                   Bytes(source.begin() + static_cast<std::ptrdiff_t>(frame.offset),
                         source.begin() + static_cast<std::ptrdiff_t>(frame.offset + frame.size)) == kFrames[index],
               what);
    }
}
}  // namespace

int main() {
    using aura::VoiceCodec;
    aura::VoiceTrack track;

    const Bytes aac = whole_mp4(audio_entry("mp4a", 44100, esds()), 44100, 3072);
    expect(aura::voice_demux(aac.data(), aac.size(), track) && track.codec == VoiceCodec::Aac &&
               track.sample_rate == 44100 && track.channels == 1 && track.config == kAsc,
           "Safari's AAC in a whole MP4: codec, rate, channels and the AudioSpecificConfig");
    same_frames(track, aac, "the AAC frames come back exactly, from the sample table");
    expect(track.duration_ms == 69, "the duration comes from the media header");

    const Bytes fragmented = fragmented_mp4(audio_entry("Opus", 48000, dops()), 48000);
    expect(aura::voice_demux(fragmented.data(), fragmented.size(), track) && track.codec == VoiceCodec::Opus &&
               track.sample_rate == 48000 && track.channels == 1 && track.pre_skip == 312,
           "Chrome's Opus in a fragmented MP4, with its pre-skip");
    same_frames(track, fragmented, "the Opus frames come back from every fragment");
    expect(track.duration_ms == 64, "the duration is the fragments' sample durations");

    const Bytes fragmented_aac = fragmented_mp4(audio_entry("mp4a", 44100, esds()), 44100);
    expect(aura::voice_demux(fragmented_aac.data(), fragmented_aac.size(), track) && track.codec == VoiceCodec::Aac,
           "AAC in a fragmented MP4 too");
    same_frames(track, fragmented_aac, "its frames come back exactly");

    for (bool unknown : {false, true}) {
        const Bytes firefox = webm(unknown);
        expect(aura::voice_demux(firefox.data(), firefox.size(), track) && track.codec == VoiceCodec::Opus &&
                   track.sample_rate == 48000 && track.channels == 1 && track.config == kOpusHead &&
                   track.pre_skip == 312,
               unknown ? "Firefox's WebM with unknown sizes" : "a WebM with known sizes");
        same_frames(track, firefox, "the WebM Opus blocks come back exactly");
    }

    std::vector<std::int16_t> samples(1600);
    for (std::size_t index = 0; index < samples.size(); ++index)
        samples[index] = static_cast<std::int16_t>((index % 40) * 800 - 16000);
    Bytes wav;
    expect(aura::voice_wav(samples.data(), samples.size(), 16000, wav) && wav.size() == 44 + 3200,
           "AURA's own voice message is a 16 kHz mono 16-bit WAV");
    expect(aura::voice_demux(wav.data(), wav.size(), track) && track.codec == VoiceCodec::Pcm16 &&
               track.sample_rate == 16000 && track.channels == 1 && track.duration_ms == 100 &&
               track.frames.size() == 1 && track.frames[0].size == 3200,
           "and it reads back as one block of PCM");

    Bytes adts;
    expect(aura::voice_adts(kAsc, kFrames[0].data(), kFrames[0].size(), adts) && adts.size() == 7 + kFrames[0].size() &&
               adts[0] == 0xFF && (adts[1] & 0xF6) == 0xF0 && ((adts[2] >> 2) & 0x0F) == 4 &&
               ((((adts[3] & 3) << 11) | (adts[4] << 3) | (adts[5] >> 5)) == 10),
           "a raw AAC frame gets an ADTS header for the decoders (44.1 kHz index 4, length 10)");

    Bytes broken = aac;
    broken.resize(broken.size() - 2);
    expect(!aura::voice_demux(broken.data(), broken.size(), track), "a truncated file is refused");
    const Bytes text = {'h', 'e', 'l', 'l', 'o'};
    expect(!aura::voice_demux(text.data(), text.size(), track) && track.codec == VoiceCodec::None,
           "anything else is refused");

    // Decoding: PCM needs no platform decoder; AAC and Opus go to the
    // platform's, frame by frame, with the track's configuration.
    {
        aura::PcmClip clip;
        std::string error;
        expect(aura::voice_decode(wav.data(), wav.size(), nullptr, clip, error) && clip.format.sample_rate == 16000 &&
                   clip.format.channels == 1 && clip.samples == samples,
               "AURA's WAV plays without a platform decoder");
        expect(!aura::voice_decode(aac.data(), aac.size(), nullptr, clip, error) && error == "VOICE_UNSUPPORTED" &&
                   clip.samples.empty(),
               "AAC without a decoder on this platform says so");
        struct Recorder final : aura::IVoiceDecoder {
            std::vector<Bytes> frames;
            aura::VoiceCodec codec{aura::VoiceCodec::None};
            bool decode(const std::uint8_t* data, const aura::VoiceTrack& track, aura::PcmClip& out) override {
                codec = track.codec;
                for (const auto& frame : track.frames)
                    frames.emplace_back(data + frame.offset, data + frame.offset + frame.size);
                out.format.sample_rate = track.sample_rate;
                out.format.channels = 1;
                out.samples.assign(441, 7);
                return true;
            }
        } recorder;
        expect(aura::voice_decode(aac.data(), aac.size(), &recorder, clip, error) &&
                   recorder.codec == aura::VoiceCodec::Aac && recorder.frames == kFrames &&
                   clip.format.sample_rate == 16000 && clip.samples.size() == 160,
               "AAC goes to the platform's decoder, every frame exactly, and comes back at 16 kHz");
        expect(!aura::voice_decode(text.data(), text.size(), &recorder, clip, error) && error == "VOICE_DAMAGED",
               "an unreadable file says so");
        std::vector<std::int16_t> long_samples(16000 * 6, 1);
        Bytes long_wav;
        expect(aura::voice_wav(long_samples.data(), long_samples.size(), 16000, long_wav) &&
                   !aura::voice_decode(long_wav.data(), long_wav.size(), nullptr, clip, error, 5) &&
                   error == "VOICE_TOO_LONG",
               "a message longer than the limit is refused");
    }

    // Voice stays 16 kHz mono: a minute of 48 kHz stereo would not fit the
    // device's PSRAM. The resampler works frame by frame, as decoders produce.
    {
        constexpr double kPi = 3.14159265358979;
        std::vector<std::int16_t> stereo(48000 * 2);
        for (std::size_t index = 0; index < 48000; ++index) {
            const auto value = static_cast<std::int16_t>(8000 * std::sin(2.0 * kPi * 440.0 * index / 48000.0));
            stereo[2 * index] = value;
            stereo[2 * index + 1] = value;
        }
        aura::VoiceResampler resampler(48000, 2);
        resampler.push(stereo.data(), 24000);
        resampler.push(stereo.data() + 48000, 24000);
        const auto& out = resampler.output();
        expect(out.size() >= 15999 && out.size() <= 16001, "a second of 48 kHz stereo becomes 16,000 mono samples");
        std::size_t crossings = 0;
        for (std::size_t index = 1; index < out.size(); ++index)
            crossings += (out[index - 1] < 0) != (out[index] < 0) ? 1U : 0U;
        expect(crossings > 860 && crossings < 900, "the 440 Hz tone survives");
        int loudest = 0;
        for (const auto sample : out) loudest = std::max(loudest, std::abs(static_cast<int>(sample)));
        expect(loudest > 7000 && loudest <= 8000, "and so does its level");
        aura::VoiceResampler cd(44100, 1);
        std::vector<std::int16_t> mono(44100, 300);
        cd.push(mono.data(), mono.size());
        expect(cd.output().size() >= 15999 && cd.output().size() <= 16001 && cd.output()[100] == 300,
               "44.1 kHz too");
        std::vector<std::int16_t> high(48000, 250);
        Bytes high_wav;
        aura::PcmClip clip;
        std::string error;
        expect(aura::voice_wav(high.data(), high.size(), 48000, high_wav) &&
                   aura::voice_decode(high_wav.data(), high_wav.size(), nullptr, clip, error) &&
                   clip.format.sample_rate == 16000 && clip.samples.size() >= 15999 && clip.samples.size() <= 16001,
               "a decoded message is kept at 16 kHz");
    }

    std::cout << "voice container tests passed\n";
    return 0;
}
