#include "aura/windows_voice_decoder.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace aura::simulator {
namespace {

// MFAudioFormat_Opus: the WAVE_FORMAT_OPUS (0x704F) media subtype.
const GUID kOpusSubtype = {0x0000704F, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

template <typename T>
struct Com {
    T* value{nullptr};
    ~Com() {
        if (value) value->Release();
    }
    T** out() { return &value; }
    T* operator->() const { return value; }
};

struct Session {
    bool ok{false};
    bool com{false};
    Session() {
        com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        ok = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    }
    ~Session() {
        if (ok) MFShutdown();
        if (com) CoUninitialize();
    }
};

bool find_decoder(const GUID& subtype, Com<IMFTransform>& transform) {
    MFT_REGISTER_TYPE_INFO input{MFMediaType_Audio, subtype};
    IMFActivate** found = nullptr;
    UINT32 count = 0;
    if (FAILED(MFTEnumEx(MFT_CATEGORY_AUDIO_DECODER,
                         MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER, &input, nullptr,
                         &found, &count)))
        return false;
    bool ok = false;
    for (UINT32 index = 0; index < count; ++index) {
        if (!ok && SUCCEEDED(found[index]->ActivateObject(IID_PPV_ARGS(transform.out())))) ok = true;
        found[index]->Release();
    }
    CoTaskMemFree(found);
    return ok;
}

bool set_types(IMFTransform* transform, const GUID& subtype, const VoiceTrack& track,
               const std::vector<std::uint8_t>& user_data, std::uint32_t& rate, std::uint32_t& channels,
               bool& floating) {
    Com<IMFMediaType> input;
    if (FAILED(MFCreateMediaType(input.out())) || FAILED(input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(input->SetGUID(MF_MT_SUBTYPE, subtype)) ||
        FAILED(input->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, track.sample_rate)) ||
        FAILED(input->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, track.channels)))
        return false;
    if (subtype == MFAudioFormat_AAC) input->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
    if (!user_data.empty() &&
        FAILED(input->SetBlob(MF_MT_USER_DATA, user_data.data(), static_cast<UINT32>(user_data.size()))))
        return false;
    if (FAILED(transform->SetInputType(0, input.value, 0))) return false;
    // 16-bit PCM where the decoder offers it, otherwise 32-bit float.
    for (int pass = 0; pass < 2; ++pass) for (DWORD index = 0;; ++index) {
        Com<IMFMediaType> output;
        if (FAILED(transform->GetOutputAvailableType(0, index, output.out()))) break;
        GUID format{};
        UINT32 bits = 0;
        const GUID wanted = pass == 0 ? MFAudioFormat_PCM : MFAudioFormat_Float;
        if (SUCCEEDED(output->GetGUID(MF_MT_SUBTYPE, &format)) && format == wanted &&
            SUCCEEDED(output->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits)) && bits == (pass == 0 ? 16U : 32U) &&
            SUCCEEDED(transform->SetOutputType(0, output.value, 0))) {
            floating = pass == 1;
            UINT32 value = 0;
            rate = SUCCEEDED(output->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &value)) ? value : track.sample_rate;
            channels = SUCCEEDED(output->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &value)) ? value : track.channels;
            return rate > 0 && channels > 0;
        }
    }
    return false;
}

// Pulls every decoded block the transform has ready; false on a real failure.
bool drain(IMFTransform* transform, std::vector<std::int16_t>& pcm, std::size_t limit, bool floating) {
    MFT_OUTPUT_STREAM_INFO info{};
    if (FAILED(transform->GetOutputStreamInfo(0, &info))) return false;
    const bool provides = (info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
    for (;;) {
        Com<IMFSample> sample;
        Com<IMFMediaBuffer> buffer;
        if (!provides) {
            if (FAILED(MFCreateSample(sample.out())) ||
                FAILED(MFCreateMemoryBuffer(std::max<DWORD>(info.cbSize, 65536), buffer.out())) ||
                FAILED(sample->AddBuffer(buffer.value)))
                return false;
        }
        MFT_OUTPUT_DATA_BUFFER output{};
        output.pSample = sample.value;
        DWORD status = 0;
        const HRESULT result = transform->ProcessOutput(0, 1, &output, &status);
        if (output.pEvents) output.pEvents->Release();
        if (result == MF_E_TRANSFORM_NEED_MORE_INPUT) return true;
        if (FAILED(result)) return false;
        IMFSample* produced = output.pSample;
        Com<IMFMediaBuffer> contiguous;
        if (!produced || FAILED(produced->ConvertToContiguousBuffer(contiguous.out()))) {
            if (provides && produced) produced->Release();
            return false;
        }
        BYTE* bytes = nullptr;
        DWORD length = 0;
        if (SUCCEEDED(contiguous->Lock(&bytes, nullptr, &length))) {
            const std::size_t count = length / (floating ? 4 : 2);
            if (pcm.size() + count > limit) {
                contiguous->Unlock();
                if (provides) produced->Release();
                return false;
            }
            const std::size_t at = pcm.size();
            pcm.resize(at + count);
            if (floating) {
                for (std::size_t index = 0; index < count; ++index) {
                    float value = 0;
                    std::memcpy(&value, bytes + index * 4, 4);
                    pcm[at + index] = static_cast<std::int16_t>(std::max(-1.0F, std::min(1.0F, value)) * 32767.0F);
                }
            } else {
                std::memcpy(pcm.data() + at, bytes, count * 2);
            }
            contiguous->Unlock();
        }
        if (provides) produced->Release();
    }
}

}  // namespace

bool WindowsVoiceDecoder::decode(const std::uint8_t* data, const VoiceTrack& track, PcmClip& clip) {
    clip = {};
    if (!data || track.frames.empty() || track.channels == 0 || track.sample_rate == 0) return false;
    if (track.codec != VoiceCodec::Aac && track.codec != VoiceCodec::Opus) return false;
    Session session;
    if (!session.ok) return false;
    const GUID subtype = track.codec == VoiceCodec::Aac ? MFAudioFormat_AAC : kOpusSubtype;
    Com<IMFTransform> transform;
    if (!find_decoder(subtype, transform)) return false;
    // AAC: HEAACWAVEINFO's tail (raw payload, profile 0xFE) and the AudioSpecificConfig.
    std::vector<std::uint8_t> user_data;
    if (track.codec == VoiceCodec::Aac) {
        user_data = {0, 0, 0xFE, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        user_data.insert(user_data.end(), track.config.begin(), track.config.end());
    } else {
        user_data = track.config;  // OpusHead, when the container carried one
    }
    std::uint32_t rate = 0;
    std::uint32_t channels = 0;
    bool floating = false;
    if (!set_types(transform.value, subtype, track, user_data, rate, channels, floating)) return false;
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    std::vector<std::int16_t> pcm;
    const std::size_t limit = std::size_t{rate} * channels * 600;
    for (const auto& frame : track.frames) {
        Com<IMFMediaBuffer> buffer;
        Com<IMFSample> sample;
        BYTE* target = nullptr;
        if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(frame.size), buffer.out())) ||
            FAILED(buffer->Lock(&target, nullptr, nullptr)))
            return false;
        std::memcpy(target, data + frame.offset, frame.size);
        buffer->Unlock();
        buffer->SetCurrentLength(static_cast<DWORD>(frame.size));
        if (FAILED(MFCreateSample(sample.out())) || FAILED(sample->AddBuffer(buffer.value))) return false;
        HRESULT result = transform->ProcessInput(0, sample.value, 0);
        if (result == MF_E_NOTACCEPTING) {
            if (!drain(transform.value, pcm, limit, floating)) return false;
            result = transform->ProcessInput(0, sample.value, 0);
        }
        if (FAILED(result) || !drain(transform.value, pcm, limit, floating)) return false;
    }
    transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    if (!drain(transform.value, pcm, limit, floating) || pcm.empty()) return false;
    // Mono, as AURA plays voice; Opus's pre-skip is dropped at 48 kHz.
    PcmClip decoded;
    decoded.format.sample_rate = rate;
    decoded.format.channels = 1;
    const std::size_t skip = track.codec == VoiceCodec::Opus ? std::size_t{track.pre_skip} * rate / 48000 : 0;
    for (std::size_t index = skip * channels; index + channels <= pcm.size(); index += channels) {
        std::int32_t sum = 0;
        for (std::size_t channel = 0; channel < channels; ++channel) sum += pcm[index + channel];
        decoded.samples.push_back(static_cast<std::int16_t>(sum / static_cast<std::int32_t>(channels)));
    }
    clip = std::move(decoded);
    return !clip.samples.empty();
}

}  // namespace aura::simulator
