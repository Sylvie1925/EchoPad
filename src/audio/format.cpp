#include "format.h"

namespace echopad {
namespace {

// KSDATAFORMAT_SUBTYPE_* live in ksmedia.h, whose GUIDs MinGW-w64 does not
// export from an import library. Defining the two we need locally keeps this
// translation unit free of that dependency.
const GUID kSubtypePcm = {
    0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
const GUID kSubtypeFloat = {
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

inline float ClampUnit(float value) {
    if (value > 1.0f) return 1.0f;
    if (value < -1.0f) return -1.0f;
    return value;
}

inline int32_t FloatToInt(float value, float scale, float upper, float lower) {
    const float scaled = value * scale;
    if (scaled >= upper) return static_cast<int32_t>(upper);
    if (scaled <= lower) return static_cast<int32_t>(lower);
    return static_cast<int32_t>(scaled);
}

}  // namespace

void WaveFormatHolder::CopyFrom(const WAVEFORMATEX* format) {
    if (!format) {
        bytes_.clear();
        return;
    }
    const size_t size = sizeof(WAVEFORMATEX) + format->cbSize;
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(format);
    bytes_.assign(raw, raw + size);
}

uint16_t FormatChannels(const WAVEFORMATEX* format) {
    return format ? format->nChannels : 0;
}

uint32_t FormatSampleRate(const WAVEFORMATEX* format) {
    return format ? format->nSamplesPerSec : 0;
}

SampleType ClassifySampleType(const WAVEFORMATEX* format) {
    if (!format) {
        return SampleType::Unknown;
    }
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return format->wBitsPerSample == 32 ? SampleType::Float32 : SampleType::Unknown;
    }
    if (format->wFormatTag == WAVE_FORMAT_PCM) {
        switch (format->wBitsPerSample) {
            case 16: return SampleType::Pcm16;
            case 24: return SampleType::Pcm24;
            case 32: return SampleType::Pcm32;
            default: return SampleType::Unknown;
        }
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->cbSize >= (sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))) {
        const auto* extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        if (IsEqualGUID(extended->SubFormat, kSubtypeFloat)) {
            return format->wBitsPerSample == 32 ? SampleType::Float32 : SampleType::Unknown;
        }
        if (IsEqualGUID(extended->SubFormat, kSubtypePcm)) {
            switch (format->wBitsPerSample) {
                case 16: return SampleType::Pcm16;
                case 24: return SampleType::Pcm24;
                case 32: return SampleType::Pcm32;
                default: return SampleType::Unknown;
            }
        }
    }
    return SampleType::Unknown;
}

const wchar_t* SampleTypeName(SampleType type) {
    switch (type) {
        case SampleType::Float32: return L"float32";
        case SampleType::Pcm16: return L"pcm16";
        case SampleType::Pcm24: return L"pcm24";
        case SampleType::Pcm32: return L"pcm32";
        default: return L"unknown";
    }
}

void BuildFloatFormat(uint32_t sampleRate, uint16_t channels, WAVEFORMATEXTENSIBLE& out) {
    std::memset(&out, 0, sizeof(out));
    out.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    out.Format.nChannels = channels;
    out.Format.nSamplesPerSec = sampleRate;
    out.Format.wBitsPerSample = 32;
    out.Format.nBlockAlign = static_cast<uint16_t>(channels * sizeof(Sample));
    out.Format.nAvgBytesPerSec = sampleRate * out.Format.nBlockAlign;
    out.Format.cbSize = static_cast<uint16_t>(sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
    out.Samples.wValidBitsPerSample = 32;
    out.dwChannelMask = channels == 1 ? SPEAKER_FRONT_CENTER
                                      : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    out.SubFormat = kSubtypeFloat;
}

void BuildEngineFormat(WAVEFORMATEXTENSIBLE& out) {
    BuildFloatFormat(kEngineSampleRate, kEngineChannels, out);
}

bool IsEngineFormat(const WAVEFORMATEX* format) {
    return format && FormatSampleRate(format) == kEngineSampleRate &&
           FormatChannels(format) == kEngineChannels &&
           ClassifySampleType(format) == SampleType::Float32;
}

void EncodeToDevice(const Sample* source, uint32_t frames, void* target,
                    const WAVEFORMATEX* targetFormat) {
    if (!source || !target || !targetFormat || frames == 0) {
        return;
    }
    const uint32_t count = frames * FormatChannels(targetFormat);
    switch (ClassifySampleType(targetFormat)) {
        case SampleType::Float32:
            std::memcpy(target, source, static_cast<size_t>(count) * sizeof(Sample));
            break;
        case SampleType::Pcm16: {
            auto* out = static_cast<int16_t*>(target);
            for (uint32_t i = 0; i < count; ++i) {
                out[i] = static_cast<int16_t>(FloatToInt(source[i], 32768.0f, 32767.0f, -32768.0f));
            }
            break;
        }
        case SampleType::Pcm24: {
            auto* out = static_cast<uint8_t*>(target);
            for (uint32_t i = 0; i < count; ++i) {
                const int32_t value =
                    FloatToInt(source[i], 8388608.0f, 8388607.0f, -8388608.0f);
                out[i * 3 + 0] = static_cast<uint8_t>(value & 0xFF);
                out[i * 3 + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
                out[i * 3 + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
            }
            break;
        }
        case SampleType::Pcm32: {
            auto* out = static_cast<int32_t*>(target);
            for (uint32_t i = 0; i < count; ++i) {
                out[i] = FloatToInt(source[i], 2147483648.0f, 2147483520.0f, -2147483648.0f);
            }
            break;
        }
        default:
            std::memset(target, 0, static_cast<size_t>(count) * (targetFormat->wBitsPerSample / 8));
            break;
    }
}

void DecodeFromDevice(const void* source, uint32_t frames, Sample* target,
                      const WAVEFORMATEX* sourceFormat) {
    if (!source || !target || !sourceFormat || frames == 0) {
        return;
    }
    const uint32_t count = frames * FormatChannels(sourceFormat);
    switch (ClassifySampleType(sourceFormat)) {
        case SampleType::Float32:
            std::memcpy(target, source, static_cast<size_t>(count) * sizeof(Sample));
            break;
        case SampleType::Pcm16: {
            const auto* in = static_cast<const int16_t*>(source);
            for (uint32_t i = 0; i < count; ++i) {
                target[i] = static_cast<float>(in[i]) * (1.0f / 32768.0f);
            }
            break;
        }
        case SampleType::Pcm24: {
            const auto* in = static_cast<const uint8_t*>(source);
            for (uint32_t i = 0; i < count; ++i) {
                int32_t value = static_cast<int32_t>(in[i * 3 + 0]) |
                                (static_cast<int32_t>(in[i * 3 + 1]) << 8) |
                                (static_cast<int32_t>(in[i * 3 + 2]) << 16);
                if (value & 0x00800000) {
                    value |= static_cast<int32_t>(0xFF000000);  // sign extend
                }
                target[i] = static_cast<float>(value) * (1.0f / 8388608.0f);
            }
            break;
        }
        case SampleType::Pcm32: {
            const auto* in = static_cast<const int32_t*>(source);
            for (uint32_t i = 0; i < count; ++i) {
                target[i] = static_cast<float>(static_cast<double>(in[i]) / 2147483648.0);
            }
            break;
        }
        default:
            std::memset(target, 0, static_cast<size_t>(count) * sizeof(Sample));
            break;
    }
}

}  // namespace echopad
