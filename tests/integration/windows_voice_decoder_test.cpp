// The simulator's voice decoder on this PC's Media Foundation: a tone encoded
// with Windows' own AAC encoder (as raw frames and an AudioSpecificConfig, the
// way the shared demuxer hands them over) decodes back to the same tone.
// Opus decodes only where an Opus decoder is installed; the test reports which.
#include "aura/voice_decoder.hpp"
#include "aura/windows_voice_decoder.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
void expect(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

template <typename T>
struct Com {
    T* value{nullptr};
    ~Com() {
        if (value) value->Release();
    }
    T** out() { return &value; }
    T* operator->() const { return value; }
};

constexpr UINT32 kRate = 44100;

// Windows' AAC encoder: one raw AAC frame per output sample.
bool encode_tone(std::vector<std::uint8_t>& stream, std::vector<aura::VoiceFrame>& frames,
                 std::vector<std::uint8_t>& asc) {
    MFT_REGISTER_TYPE_INFO input{MFMediaType_Audio, MFAudioFormat_PCM};
    MFT_REGISTER_TYPE_INFO output{MFMediaType_Audio, MFAudioFormat_AAC};
    IMFActivate** found = nullptr;
    UINT32 count = 0;
    if (FAILED(MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER, &input,
                         &output, &found, &count)) ||
        count == 0) {
        std::cerr << "no AAC encoder\n";
        return false;
    }
    Com<IMFTransform> encoder;
    const bool activated = SUCCEEDED(found[0]->ActivateObject(IID_PPV_ARGS(encoder.out())));
    for (UINT32 index = 0; index < count; ++index) found[index]->Release();
    CoTaskMemFree(found);
    if (!activated) {
        std::cerr << "encoder not activated\n";
        return false;
    }
    Com<IMFMediaType> out_type;
    Com<IMFMediaType> in_type;
    if (FAILED(MFCreateMediaType(out_type.out())) || FAILED(out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(out_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC)) ||
        FAILED(out_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) ||
        FAILED(out_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate)) ||
        FAILED(out_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1)) ||
        FAILED(out_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 12000)) ||
        FAILED(encoder->SetOutputType(0, out_type.value, 0))) {
        std::cerr << "output type refused\n";
        return false;
    }
    if (FAILED(MFCreateMediaType(in_type.out())) || FAILED(in_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(in_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
        FAILED(in_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) ||
        FAILED(in_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate)) ||
        FAILED(in_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1)) ||
        FAILED(encoder->SetInputType(0, in_type.value, 0))) {
        std::cerr << "input type refused\n";
        return false;
    }
    Com<IMFMediaType> current;
    UINT8* blob = nullptr;
    UINT32 blob_size = 0;
    if (FAILED(encoder->GetOutputCurrentType(0, current.out())) ||
        FAILED(current->GetAllocatedBlob(MF_MT_USER_DATA, &blob, &blob_size)) || blob_size <= 12) {
        std::cerr << "no AudioSpecificConfig\n";
        return false;
    }
    asc.assign(blob + 12, blob + blob_size);
    CoTaskMemFree(blob);
    std::vector<std::int16_t> tone(kRate);  // one second of 440 Hz
    for (std::size_t index = 0; index < tone.size(); ++index)
        tone[index] = static_cast<std::int16_t>(8000 * std::sin(2.0 * 3.14159265358979 * 440.0 * index / kRate));
    const auto pull = [&]() {
        MFT_OUTPUT_STREAM_INFO info{};
        encoder->GetOutputStreamInfo(0, &info);
        for (;;) {
            Com<IMFSample> sample;
            Com<IMFMediaBuffer> buffer;
            MFCreateSample(sample.out());
            MFCreateMemoryBuffer(std::max<DWORD>(info.cbSize, 8192), buffer.out());
            sample->AddBuffer(buffer.value);
            MFT_OUTPUT_DATA_BUFFER data{};
            data.pSample = sample.value;
            DWORD status = 0;
            if (encoder->ProcessOutput(0, 1, &data, &status) != S_OK) return;
            BYTE* bytes = nullptr;
            DWORD length = 0;
            buffer->Lock(&bytes, nullptr, &length);
            frames.push_back({stream.size(), length});
            stream.insert(stream.end(), bytes, bytes + length);
            buffer->Unlock();
        }
    };
    encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    for (std::size_t at = 0; at < tone.size(); at += 4410) {
        Com<IMFMediaBuffer> buffer;
        Com<IMFSample> sample;
        BYTE* target = nullptr;
        const DWORD bytes = static_cast<DWORD>(std::min<std::size_t>(4410, tone.size() - at) * 2);
        MFCreateMemoryBuffer(bytes, buffer.out());
        buffer->Lock(&target, nullptr, nullptr);
        std::memcpy(target, tone.data() + at, bytes);
        buffer->Unlock();
        buffer->SetCurrentLength(bytes);
        MFCreateSample(sample.out());
        sample->AddBuffer(buffer.value);
        sample->SetSampleTime(static_cast<LONGLONG>(at) * 10000000LL / kRate);
        sample->SetSampleDuration(static_cast<LONGLONG>(bytes / 2) * 10000000LL / kRate);
        const HRESULT taken = encoder->ProcessInput(0, sample.value, 0);
        if (FAILED(taken)) {
            std::cerr << std::hex << static_cast<unsigned long>(taken) << std::dec << ' ';
            std::cerr << "input not taken\n";
            return false;
        }
        pull();
    }
    encoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    pull();
    return !frames.empty();
}

bool opus_decoder_installed() {
    const GUID opus = {0x0000704F, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
    MFT_REGISTER_TYPE_INFO input{MFMediaType_Audio, opus};
    IMFActivate** found = nullptr;
    UINT32 count = 0;
    if (FAILED(MFTEnumEx(MFT_CATEGORY_AUDIO_DECODER, MFT_ENUM_FLAG_ALL, &input, nullptr, &found, &count))) return false;
    for (UINT32 index = 0; index < count; ++index) found[index]->Release();
    CoTaskMemFree(found);
    return count > 0;
}
}  // namespace

int main() {
    expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) && SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE)),
           "Media Foundation starts");
    std::vector<std::uint8_t> stream;
    aura::VoiceTrack track;
    track.codec = aura::VoiceCodec::Aac;
    track.sample_rate = kRate;
    track.channels = 1;
    expect(encode_tone(stream, track.frames, track.config), "Windows' AAC encoder makes raw frames and their config");
    const bool opus = opus_decoder_installed();
    MFShutdown();

    aura::simulator::WindowsVoiceDecoder decoder;
    aura::PcmClip clip;
    expect(decoder.decode(stream.data(), track, clip), "the AAC frames decode");
    expect(clip.format.channels == 1 && clip.format.sample_rate == kRate, "mono at the track's rate");
    const double seconds = static_cast<double>(clip.samples.size()) / kRate;
    expect(seconds > 0.9 && seconds < 1.2, "about the second that was encoded");
    std::size_t crossings = 0;
    for (std::size_t index = kRate / 10 + 1; index < clip.samples.size() - kRate / 10; ++index)
        crossings += (clip.samples[index - 1] < 0) != (clip.samples[index] < 0) ? 1U : 0U;
    const double hertz = crossings / 2.0 / (static_cast<double>(clip.samples.size() - kRate / 5) / kRate);
    expect(hertz > 420 && hertz < 460, "the 440 Hz tone survives");

    if (opus) {
        // Ten 20 ms Opus packets of a single TOC byte (CELT, mono, an empty
        // frame), which a decoder turns into concealed silence, with OpusHead.
        const std::vector<std::uint8_t> packets(10, 0xF8);
        aura::VoiceTrack silence;
        silence.codec = aura::VoiceCodec::Opus;
        silence.sample_rate = 48000;
        silence.channels = 1;
        silence.config = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, 1, 0, 0, 0x80, 0xBB, 0, 0, 0, 0, 0};
        for (std::size_t index = 0; index < packets.size(); ++index) silence.frames.push_back({index, 1});
        aura::PcmClip quiet;
        expect(decoder.decode(packets.data(), silence, quiet), "Opus packets decode where the decoder is installed");
        const double length = static_cast<double>(quiet.samples.size()) / quiet.format.sample_rate;
        expect(length > 0.1 && length < 0.3, "about 200 ms of Opus");
        int loudest = 0;
        for (const auto sample : quiet.samples) loudest = std::max(loudest, std::abs(static_cast<int>(sample)));
        expect(loudest < 64, "and it is silence");
    }

    aura::VoiceTrack unknown = track;
    unknown.codec = aura::VoiceCodec::Pcm16;
    expect(!decoder.decode(stream.data(), unknown, clip), "only AAC and Opus go through Media Foundation");
    std::cout << "windows voice decoder tests passed (Opus decoder " << (opus ? "installed" : "not installed")
              << ")\n";
    return 0;
}
