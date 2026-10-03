// EchoPad - WAVEFORMATEX inspection and sample-encoding conversion.
//
// The mixer always works in 32-bit float. Devices disagree about how they store
// samples, so the output stage encodes into whatever the endpoint was opened
// with, and the capture stage decodes back. Sample rate and channel count are
// always identical on both sides by construction, so only the encoding changes.
#pragma once

#include "common.h"

namespace echopad {

// The format we ask WASAPI for. AUTOCONVERTPCM lets the audio engine hand us
// exactly this, so the common path needs no conversion at all - which is the
// whole point: near-zero CPU inside our process.
constexpr uint32_t kEngineSampleRate = 48000;
constexpr uint16_t kEngineChannels = 2;

enum class SampleType { Unknown, Float32, Pcm16, Pcm24, Pcm32 };

// Owns a WAVEFORMATEX (plus its cbSize extension bytes) so it can be kept
// around after GetMixFormat's CoTaskMemFree.
class WaveFormatHolder {
public:
    WaveFormatHolder() = default;
    WaveFormatHolder(const WaveFormatHolder&) = delete;
    WaveFormatHolder& operator=(const WaveFormatHolder&) = delete;

    void CopyFrom(const WAVEFORMATEX* format);
    void Clear() { bytes_.clear(); }

    const WAVEFORMATEX* Get() const {
        return bytes_.empty() ? nullptr : reinterpret_cast<const WAVEFORMATEX*>(bytes_.data());
    }
    explicit operator bool() const { return !bytes_.empty(); }

    uint16_t Channels() const { return Get() ? Get()->nChannels : 0; }
    uint32_t SampleRate() const { return Get() ? Get()->nSamplesPerSec : 0; }

private:
    std::vector<uint8_t> bytes_;
};

uint16_t FormatChannels(const WAVEFORMATEX* format);
uint32_t FormatSampleRate(const WAVEFORMATEX* format);
SampleType ClassifySampleType(const WAVEFORMATEX* format);
const wchar_t* SampleTypeName(SampleType type);

// WAVE_FORMAT_EXTENSIBLE describing 32-bit float at the given rate/channels.
void BuildFloatFormat(uint32_t sampleRate, uint16_t channels, WAVEFORMATEXTENSIBLE& out);

// The preferred engine format: 48 kHz, stereo, 32-bit float.
void BuildEngineFormat(WAVEFORMATEXTENSIBLE& out);

// True when `format` is 48 kHz stereo float32.
bool IsEngineFormat(const WAVEFORMATEX* format);

// Encodes `frames` interleaved float frames into a device buffer. `target`
// must have the same channel count as the mixer.
void EncodeToDevice(const Sample* source, uint32_t frames, void* target,
                    const WAVEFORMATEX* targetFormat);

// Decodes `frames` interleaved frames from a device buffer into float32.
void DecodeFromDevice(const void* source, uint32_t frames, Sample* target,
                      const WAVEFORMATEX* sourceFormat);

}  // namespace echopad
