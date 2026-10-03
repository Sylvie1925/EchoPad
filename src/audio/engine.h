// EchoPad - duplex WASAPI engine.
//
// One render thread and (optionally) one capture thread. Both are event-driven:
// they sleep until the audio engine signals a buffer, so an idle EchoPad costs
// nothing. All audio is mixed as 32-bit float, and the render device is always
// opened in shared mode so EchoPad never fights another process for exclusive
// access.
//
// The capture side exists so the user still sounds like themselves while effect
// sounds play: the microphone is mixed into the same stream that goes out to the
// virtual cable, which is why no "listen to this device" loopback hack is
// needed.
#pragma once

#include "audio/format.h"
#include "common.h"
#include "core/ring_buffer.h"

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <memory>
#include <mutex>
#include <thread>

namespace echopad {

// A fully decoded sound, always in the engine's format so playback never touches
// a decoder or a resampler.
struct SoundData {
    AudioFormat format;
    std::vector<Sample> samples;  // interleaved
    uint64_t frameCount = 0;
    uint32_t durationMs = 0;
};
using SoundDataPtr = std::shared_ptr<const SoundData>;

enum class EngineCommandType : uint8_t {
    Play,
    StopSound,
    StopAll,
    SetMasterGain,
    SetMicGain,
};

struct EngineCommand {
    EngineCommandType type = EngineCommandType::StopAll;
    SoundDataPtr sound;      // Play
    uint32_t catalogId = 0;  // Play / StopSound
    float gain = 1.0f;       // Play / SetMasterGain / SetMicGain
    bool loop = false;       // Play
};

struct EngineStatus {
    bool running = false;
    bool micPassthroughActive = false;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    uint32_t bufferFrames = 0;
    double bufferMs = 0.0;
    float peak = 0.0f;
    uint32_t activeVoices = 0;
    uint32_t underruns = 0;
    double renderLoadPercent = 0.0;
    std::wstring renderDeviceName;
    std::wstring captureDeviceName;
    std::wstring lastError;
};

class AudioEngine {
public:
    static constexpr uint32_t kMaxVoices = 24;

    AudioEngine() = default;
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Opens the render endpoint and, when `micPassthrough` is set, the capture
    // endpoint too. Returns false and records the reason on failure.
    bool Start(const std::wstring& renderDeviceId, const std::wstring& captureDeviceId,
               bool micPassthrough, const std::wstring& renderName,
               const std::wstring& captureName);
    void Stop();
    bool running() const { return running_.load(std::memory_order_acquire); }

    // Format every decoded sound must be in. Constant for the lifetime of a
    // Start()/Stop() pair.
    AudioFormat format() const { return engineFormat_; }

    // Control-thread entry points. None of them block for long.
    bool Post(const EngineCommand& command);
    void PlaySound(SoundDataPtr sound, uint32_t catalogId, float gain, bool loop);
    void StopSound(uint32_t catalogId);
    void StopAll();
    void SetMasterGain(float gain);
    void SetMicGain(float gain);
    void SetMicPassthrough(bool enabled);

    // True once nothing has been audible for `seconds`. Used to shut the audio
    // client down so an idle EchoPad uses no CPU at all.
    bool IdleForSeconds(double seconds) const;

    EngineStatus Snapshot() const;

private:
    struct Voice {
        SoundDataPtr sound;
        uint64_t position = 0;  // frame index
        float gain = 1.0f;
        bool loop = false;
        bool active = false;
        uint32_t catalogId = 0;
    };

    bool OpenRender(const std::wstring& deviceId);
    bool OpenCapture(const std::wstring& deviceId);
    void StartCaptureThread();
    void StopCaptureThread();
    void ReleaseDevices();
    // Named SetEngineError because SetLastError is a Win32 API in this namespace.
    void SetEngineError(const std::wstring& text);

    void RenderLoop();
    void CaptureLoop();

    void DrainCommands();
    void MixInto(uint32_t frames);
    // Releases a voice's buffer. On the audio thread the reference is handed to
    // the control thread instead of being dropped, so a large decoded buffer is
    // never freed inside the callback.
    void RetireVoice(Voice& voice);
    void DrainRetired();

    // ---- render side
    ComPtr<IAudioClient> renderClient_;
    ComPtr<IAudioRenderClient> renderService_;
    HANDLE renderEvent_ = nullptr;
    WaveFormatHolder renderBufferFormat_;
    uint32_t bufferFrames_ = 0;
    std::vector<Sample> mixBuffer_;
    std::vector<Sample> micScratch_;
    std::thread renderThread_;

    // ---- capture side
    ComPtr<IAudioClient> captureClient_;
    ComPtr<IAudioCaptureClient> captureService_;
    HANDLE captureEvent_ = nullptr;
    WaveFormatHolder captureBufferFormat_;
    std::vector<Sample> captureScratch_;
    std::unique_ptr<SampleRing> micRing_;
    std::thread captureThread_;
    std::atomic<bool> captureRunning_{false};
    std::wstring captureDeviceId_;

    // ---- shared state
    AudioFormat engineFormat_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> micPassthrough_{false};
    std::atomic<float> masterGain_{1.0f};
    std::atomic<float> micGain_{1.0f};
    std::atomic<float> peak_{0.0f};
    std::atomic<uint32_t> underruns_{0};
    std::atomic<uint32_t> activeVoiceCount_{0};
    std::atomic<double> renderLoad_{0.0};
    std::atomic<uint64_t> lastAudibleMs_{0};

    // ---- audio-thread only
    Voice voices_[kMaxVoices];

    static constexpr size_t kCommandCapacity = 256;
    SpscQueue<EngineCommand, kCommandCapacity> commands_;

    // Buffers that finished playing on the audio thread, freed on the control
    // thread instead. Tiny, and it keeps the callback free of deallocations.
    static constexpr size_t kRetireCapacity = 16;
    SpscQueue<SoundDataPtr, kRetireCapacity> retired_;

    mutable std::mutex statusMutex_;
    std::wstring renderName_;
    std::wstring captureName_;
    std::wstring lastError_;
    bool micPassthroughActive_ = false;
};

}  // namespace echopad
