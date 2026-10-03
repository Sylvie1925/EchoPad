#include "engine.h"

#include <avrt.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <algorithm>

#include "audio/devices.h"
#include "core/util.h"

namespace echopad {
namespace {

// Ring big enough to absorb scheduling jitter on the capture side.
constexpr size_t kMicRingFrames = 16384;  // ~340 ms at 48 kHz

// How much microphone backlog we tolerate before resyncing. Keeping this small
// is what stops the voice from drifting behind the game audio over time.
constexpr size_t kMaxMicBacklogFrames = 4800;  // 100 ms

// Silent for this long and the audio client can be shut down entirely.
constexpr uint64_t kIdleThresholdMs = 3000;

uint64_t NowMs() {
    return static_cast<uint64_t>(GetTickCount64());
}

bool SameRateAndChannels(const WAVEFORMATEX* format, const AudioFormat& wanted) {
    return format && FormatSampleRate(format) == wanted.sampleRate &&
           FormatChannels(format) == wanted.channels;
}

}  // namespace

AudioEngine::~AudioEngine() {
    Stop();
}

// --------------------------------------------------------------------- start

bool AudioEngine::Start(const std::wstring& renderDeviceId, const std::wstring& captureDeviceId,
                        bool micPassthrough, const std::wstring& renderName,
                        const std::wstring& captureName) {
    Stop();

    {
        std::lock_guard<std::mutex> guard(statusMutex_);
        renderName_ = renderName;
        captureName_ = captureName;
        lastError_.clear();
        micPassthroughActive_ = false;
    }
    captureDeviceId_ = captureDeviceId;

    if (!OpenRender(renderDeviceId)) {
        ReleaseDevices();
        return false;
    }

    const size_t mixSamples = static_cast<size_t>(bufferFrames_) * engineFormat_.channels;
    mixBuffer_.assign(mixSamples, 0.0f);
    micScratch_.assign(mixSamples, 0.0f);

    for (Voice& voice : voices_) {
        voice = Voice{};
    }

    if (!micRing_ || micRing_->channels() != engineFormat_.channels) {
        micRing_ = std::make_unique<SampleRing>(kMicRingFrames, engineFormat_.channels);
    } else {
        micRing_->Clear();
    }

    stopRequested_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    lastAudibleMs_.store(NowMs(), std::memory_order_relaxed);
    underruns_.store(0, std::memory_order_relaxed);
    peak_.store(0.0f, std::memory_order_relaxed);

    renderThread_ = std::thread([this] { RenderLoop(); });

    if (micPassthrough) {
        SetMicPassthrough(true);
    }

    LogLine(L"engine started: %s %u Hz %u ch, buffer %u frames (%.1f ms), passthrough=%s",
            SampleTypeName(ClassifySampleType(renderBufferFormat_.Get())), engineFormat_.sampleRate,
            engineFormat_.channels, bufferFrames_,
            engineFormat_.sampleRate
                ? 1000.0 * bufferFrames_ / static_cast<double>(engineFormat_.sampleRate)
                : 0.0,
            micPassthrough ? L"on" : L"off");
    return true;
}

bool AudioEngine::OpenRender(const std::wstring& deviceId) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                  IID_IMMDeviceEnumerator, enumerator.PutVoid());
    if (FAILED(hr)) {
        SetEngineError(L"无法创建音频设备枚举器: " + HrText(hr));
        return false;
    }

    ComPtr<IMMDevice> device;
    hr = deviceId.empty()
             ? enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.Put())
             : enumerator->GetDevice(deviceId.c_str(), device.Put());
    if (FAILED(hr)) {
        SetEngineError(L"找不到输出设备: " + HrText(hr));
        return false;
    }

    hr = device->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, renderClient_.PutVoid());
    if (FAILED(hr)) {
        SetEngineError(L"无法激活输出设备: " + HrText(hr));
        return false;
    }

    WaveFormatHolder mixFormat;
    WAVEFORMATEX* rawMix = nullptr;
    if (SUCCEEDED(renderClient_->GetMixFormat(&rawMix)) && rawMix) {
        mixFormat.CopyFrom(rawMix);
        CoTaskMemFree(rawMix);
    }

    const DWORD baseFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    const DWORD convertFlags = baseFlags | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                               AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    WAVEFORMATEXTENSIBLE preferred{};
    BuildEngineFormat(preferred);

    bool opened = false;

    // Preferred path: ask for 48 kHz stereo float and let the audio engine do
    // any conversion. This keeps every decoded sound valid across device
    // changes, and means our own callback writes float samples straight into
    // the endpoint buffer.
    hr = renderClient_->Initialize(AUDCLNT_SHAREMODE_SHARED, convertFlags, 0, 0,
                                   reinterpret_cast<WAVEFORMATEX*>(&preferred), nullptr);
    if (SUCCEEDED(hr)) {
        engineFormat_ = AudioFormat{kEngineSampleRate, kEngineChannels};
        renderBufferFormat_.CopyFrom(reinterpret_cast<WAVEFORMATEX*>(&preferred));
        opened = true;
    } else {
        LogLine(L"render: AUTOCONVERTPCM not accepted (%s), falling back to device mix format",
                HrText(hr).c_str());
        if (mixFormat) {
            hr = renderClient_->Initialize(AUDCLNT_SHAREMODE_SHARED, baseFlags, 0, 0,
                                           mixFormat.Get(), nullptr);
            if (SUCCEEDED(hr)) {
                engineFormat_ = AudioFormat{FormatSampleRate(mixFormat.Get()),
                                            FormatChannels(mixFormat.Get())};
                renderBufferFormat_.CopyFrom(mixFormat.Get());
                opened = true;
            }
        }
    }

    if (!opened) {
        SetEngineError(L"无法初始化输出流: " + HrText(hr));
        return false;
    }
    if (ClassifySampleType(renderBufferFormat_.Get()) == SampleType::Unknown) {
        SetEngineError(L"输出设备使用了不支持的采样格式（位深或编码）");
        return false;
    }
    if (engineFormat_.channels == 0 || engineFormat_.sampleRate == 0) {
        SetEngineError(L"输出设备的格式无效");
        return false;
    }

    renderEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!renderEvent_) {
        SetEngineError(L"无法创建音频事件");
        return false;
    }
    hr = renderClient_->SetEventHandle(renderEvent_);
    if (FAILED(hr)) {
        SetEngineError(L"SetEventHandle 失败: " + HrText(hr));
        return false;
    }
    hr = renderClient_->GetBufferSize(&bufferFrames_);
    if (FAILED(hr) || bufferFrames_ == 0) {
        SetEngineError(L"无法获取输出缓冲区大小: " + HrText(hr));
        return false;
    }
    hr = renderClient_->GetService(IID_IAudioRenderClient, renderService_.PutVoid());
    if (FAILED(hr)) {
        SetEngineError(L"无法获取渲染服务: " + HrText(hr));
        return false;
    }
    hr = renderClient_->Start();
    if (FAILED(hr)) {
        SetEngineError(L"无法启动输出流: " + HrText(hr));
        return false;
    }
    return true;
}

bool AudioEngine::OpenCapture(const std::wstring& deviceId) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                  IID_IMMDeviceEnumerator, enumerator.PutVoid());
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IMMDevice> device;
    hr = deviceId.empty() ? enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, device.Put())
                          : enumerator->GetDevice(deviceId.c_str(), device.Put());
    if (FAILED(hr)) {
        LogLine(L"capture: device not available: %s", HrText(hr).c_str());
        return false;
    }

    hr = device->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, captureClient_.PutVoid());
    if (FAILED(hr)) {
        LogLine(L"capture: Activate failed: %s", HrText(hr).c_str());
        return false;
    }

    const DWORD convertFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                               AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                               AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    WAVEFORMATEXTENSIBLE wanted{};
    BuildFloatFormat(engineFormat_.sampleRate, engineFormat_.channels, wanted);

    bool opened = false;
    hr = captureClient_->Initialize(AUDCLNT_SHAREMODE_SHARED, convertFlags, 0, 0,
                                    reinterpret_cast<WAVEFORMATEX*>(&wanted), nullptr);
    if (SUCCEEDED(hr)) {
        captureBufferFormat_.CopyFrom(reinterpret_cast<WAVEFORMATEX*>(&wanted));
        opened = true;
    } else {
        WAVEFORMATEX* rawMix = nullptr;
        if (SUCCEEDED(captureClient_->GetMixFormat(&rawMix)) && rawMix) {
            if (SameRateAndChannels(rawMix, engineFormat_)) {
                hr = captureClient_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                                AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, rawMix,
                                                nullptr);
                if (SUCCEEDED(hr)) {
                    captureBufferFormat_.CopyFrom(rawMix);
                    opened = true;
                }
            } else {
                LogLine(L"capture: device format %u Hz %u ch does not match engine %u Hz %u ch",
                        FormatSampleRate(rawMix), FormatChannels(rawMix), engineFormat_.sampleRate,
                        engineFormat_.channels);
            }
            CoTaskMemFree(rawMix);
        }
    }

    if (!opened) {
        LogLine(L"capture: Initialize failed, passthrough disabled: %s", HrText(hr).c_str());
        captureClient_.Reset();
        return false;
    }
    if (ClassifySampleType(captureBufferFormat_.Get()) == SampleType::Unknown) {
        LogLine(L"capture: unsupported sample encoding, passthrough disabled");
        captureClient_.Reset();
        return false;
    }

    captureEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!captureEvent_) {
        captureClient_.Reset();
        return false;
    }

    UINT32 captureFrames = 0;
    hr = captureClient_->GetBufferSize(&captureFrames);
    if (FAILED(hr)) {
        LogLine(L"capture: GetBufferSize failed: %s", HrText(hr).c_str());
        captureClient_.Reset();
        return false;
    }
    captureScratch_.assign(
        static_cast<size_t>(std::max<UINT32>(captureFrames, bufferFrames_)) *
            engineFormat_.channels,
        0.0f);

    hr = captureClient_->SetEventHandle(captureEvent_);
    if (FAILED(hr)) {
        LogLine(L"capture: SetEventHandle failed: %s", HrText(hr).c_str());
        captureClient_.Reset();
        return false;
    }
    hr = captureClient_->GetService(IID_IAudioCaptureClient, captureService_.PutVoid());
    if (FAILED(hr)) {
        LogLine(L"capture: GetService failed: %s", HrText(hr).c_str());
        captureClient_.Reset();
        return false;
    }
    hr = captureClient_->Start();
    if (FAILED(hr)) {
        LogLine(L"capture: Start failed: %s", HrText(hr).c_str());
        captureService_.Reset();
        captureClient_.Reset();
        return false;
    }

    LogLine(L"capture: opened %s %u Hz %u ch, buffer %u frames",
            SampleTypeName(ClassifySampleType(captureBufferFormat_.Get())),
            FormatSampleRate(captureBufferFormat_.Get()), FormatChannels(captureBufferFormat_.Get()),
            captureFrames);
    return true;
}

void AudioEngine::ReleaseDevices() {
    renderService_.Reset();
    renderClient_.Reset();
    captureService_.Reset();
    captureClient_.Reset();
    if (renderEvent_) {
        CloseHandle(renderEvent_);
        renderEvent_ = nullptr;
    }
    if (captureEvent_) {
        CloseHandle(captureEvent_);
        captureEvent_ = nullptr;
    }
    renderBufferFormat_.Clear();
    captureBufferFormat_.Clear();
    mixBuffer_.clear();
    micScratch_.clear();
    captureScratch_.clear();
    bufferFrames_ = 0;
}

void AudioEngine::Stop() {
    const bool wasRunning = running_.exchange(false, std::memory_order_acq_rel);
    stopRequested_.store(true, std::memory_order_release);

    if (renderEvent_) {
        SetEvent(renderEvent_);
    }
    if (renderThread_.joinable()) {
        renderThread_.join();
    }

    StopCaptureThread();

    if (renderClient_) {
        renderClient_->Stop();
    }

    // Anything the audio thread retired is released here, on a control thread.
    DrainRetired();

    {
        std::lock_guard<std::mutex> guard(statusMutex_);
        micPassthroughActive_ = false;
    }
    micPassthrough_.store(false, std::memory_order_release);

    ReleaseDevices();
    micRing_.reset();

    for (Voice& voice : voices_) {
        voice = Voice{};
    }
    activeVoiceCount_.store(0, std::memory_order_relaxed);

    if (wasRunning) {
        LogLine(L"engine stopped");
    }
}

// ------------------------------------------------------------------ threading

void AudioEngine::StartCaptureThread() {
    if (captureThread_.joinable()) {
        return;
    }
    if (!captureRunning_.exchange(true, std::memory_order_acq_rel)) {
        captureThread_ = std::thread([this] { CaptureLoop(); });
    }
}

void AudioEngine::StopCaptureThread() {
    if (!captureThread_.joinable()) {
        captureRunning_.store(false, std::memory_order_release);
        if (captureClient_) {
            captureClient_->Stop();
        }
        captureService_.Reset();
        captureClient_.Reset();
        if (captureEvent_) {
            CloseHandle(captureEvent_);
            captureEvent_ = nullptr;
        }
        return;
    }
    captureRunning_.store(false, std::memory_order_release);
    if (captureEvent_) {
        SetEvent(captureEvent_);
    }
    captureThread_.join();
    if (captureClient_) {
        captureClient_->Stop();
    }
    captureService_.Reset();
    captureClient_.Reset();
    if (captureEvent_) {
        CloseHandle(captureEvent_);
        captureEvent_ = nullptr;
    }
}

// ---------------------------------------------------------------- audio loops

void AudioEngine::RenderLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    DWORD taskIndex = 0;
    HANDLE mmTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    const double msPerTick = 1000.0 / static_cast<double>(frequency.QuadPart);
    const double bufferMs =
        engineFormat_.sampleRate
            ? 1000.0 * static_cast<double>(bufferFrames_) / engineFormat_.sampleRate
            : 10.0;

    const uint16_t channels = engineFormat_.channels;
    double smoothedLoad = 0.0;

    while (!stopRequested_.load(std::memory_order_acquire)) {
        const DWORD wait = WaitForSingleObject(renderEvent_, 100);
        if (stopRequested_.load(std::memory_order_acquire)) {
            break;
        }
        if (wait != WAIT_OBJECT_0) {
            continue;
        }

        LARGE_INTEGER begin{};
        QueryPerformanceCounter(&begin);

        DrainCommands();

        UINT32 padding = 0;
        if (FAILED(renderClient_->GetCurrentPadding(&padding))) {
            underruns_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        const UINT32 available = bufferFrames_ - padding;
        if (available == 0) {
            continue;
        }

        BYTE* buffer = nullptr;
        if (FAILED(renderService_->GetBuffer(available, &buffer)) || !buffer) {
            underruns_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        MixInto(available);
        EncodeToDevice(mixBuffer_.data(), available, buffer, renderBufferFormat_.Get());
        renderService_->ReleaseBuffer(available, 0);

        // Nothing audible and no microphone to relay: let the control thread
        // shut the client down so the process goes fully idle.
        if (activeVoiceCount_.load(std::memory_order_relaxed) > 0 ||
            micPassthrough_.load(std::memory_order_relaxed)) {
            lastAudibleMs_.store(NowMs(), std::memory_order_relaxed);
        }

        LARGE_INTEGER end{};
        QueryPerformanceCounter(&end);
        const double elapsedMs =
            static_cast<double>(end.QuadPart - begin.QuadPart) * msPerTick;
        const double load = bufferMs > 0.0 ? elapsedMs / bufferMs : 0.0;
        smoothedLoad = smoothedLoad * 0.92 + load * 0.08;
        renderLoad_.store(smoothedLoad * 100.0, std::memory_order_relaxed);

        (void)channels;
    }

    if (mmTask) {
        AvRevertMmThreadCharacteristics(mmTask);
    }
    CoUninitialize();
}

void AudioEngine::CaptureLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    DWORD taskIndex = 0;
    HANDLE mmTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    const uint16_t channels = engineFormat_.channels;

    while (captureRunning_.load(std::memory_order_acquire) &&
           !stopRequested_.load(std::memory_order_acquire)) {
        const DWORD wait = WaitForSingleObject(captureEvent_, 100);
        if (!captureRunning_.load(std::memory_order_acquire) ||
            stopRequested_.load(std::memory_order_acquire)) {
            break;
        }
        if (wait != WAIT_OBJECT_0) {
            continue;
        }

        UINT32 packetFrames = 0;
        while (SUCCEEDED(captureService_->GetNextPacketSize(&packetFrames)) && packetFrames > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(captureService_->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }

            if (frames > 0 && micRing_) {
                const size_t samples = static_cast<size_t>(frames) * channels;
                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data) {
                    std::fill_n(captureScratch_.data(), samples, 0.0f);
                } else {
                    DecodeFromDevice(data, frames, captureScratch_.data(),
                                     captureBufferFormat_.Get());
                }
                micRing_->Write(captureScratch_.data(), frames);
            }
            captureService_->ReleaseBuffer(frames);
        }
    }

    if (mmTask) {
        AvRevertMmThreadCharacteristics(mmTask);
    }
    CoUninitialize();
}

// ------------------------------------------------------------------- mixing

void AudioEngine::RetireVoice(Voice& voice) {
    voice.active = false;
    voice.position = 0;
    if (voice.sound) {
        // Hand the buffer to the control thread instead of releasing it here.
        // If the retire queue is momentarily full we fall back to an inline
        // release, which is rare and only costs a free().
        retired_.Push(voice.sound);
        voice.sound.reset();
    }
    voice.catalogId = 0;
    voice.loop = false;
}

void AudioEngine::DrainRetired() {
    SoundDataPtr scratch;
    while (retired_.Pop(scratch)) {
        scratch.reset();
    }
}

void AudioEngine::DrainCommands() {
    EngineCommand command;
    while (commands_.Pop(command)) {
        switch (command.type) {
            case EngineCommandType::Play: {
                if (!command.sound) {
                    break;
                }
                // One-shot semantics: replaying a sound restarts it rather than
                // stacking a second copy of itself.
                for (Voice& voice : voices_) {
                    if (voice.active && voice.catalogId == command.catalogId) {
                        RetireVoice(voice);
                    }
                }
                Voice* slot = nullptr;
                for (Voice& voice : voices_) {
                    if (!voice.active) {
                        slot = &voice;
                        break;
                    }
                }
                if (!slot) {
                    slot = &voices_[0];  // steal the oldest slot
                    RetireVoice(*slot);
                }
                slot->sound = command.sound;
                slot->position = 0;
                slot->gain = command.gain;
                slot->loop = command.loop;
                slot->active = true;
                slot->catalogId = command.catalogId;
                break;
            }
            case EngineCommandType::StopSound:
                for (Voice& voice : voices_) {
                    if (voice.active && voice.catalogId == command.catalogId) {
                        RetireVoice(voice);
                    }
                }
                break;
            case EngineCommandType::StopAll:
                for (Voice& voice : voices_) {
                    if (voice.active) {
                        RetireVoice(voice);
                    }
                }
                break;
            case EngineCommandType::SetMasterGain:
                masterGain_.store(command.gain, std::memory_order_relaxed);
                break;
            case EngineCommandType::SetMicGain:
                micGain_.store(command.gain, std::memory_order_relaxed);
                break;
        }
    }
}

void AudioEngine::MixInto(uint32_t frames) {
    const uint16_t channels = engineFormat_.channels;
    Sample* out = mixBuffer_.data();
    const size_t totalSamples = static_cast<size_t>(frames) * channels;
    std::memset(out, 0, totalSamples * sizeof(Sample));

    uint32_t voicesActive = 0;
    for (Voice& voice : voices_) {
        if (!voice.active) {
            continue;
        }
        const SoundData* sound = voice.sound.get();
        if (!sound || sound->frameCount == 0 || sound->format.channels != channels) {
            RetireVoice(voice);
            continue;
        }

        const float gain = voice.gain;
        uint64_t remaining = frames;
        uint32_t offset = 0;
        while (remaining > 0) {
            if (voice.position >= sound->frameCount) {
                if (voice.loop) {
                    voice.position = 0;
                } else {
                    break;  // finished; retired below
                }
            }
            const uint64_t chunk =
                std::min<uint64_t>(remaining, sound->frameCount - voice.position);
            const Sample* src = sound->samples.data() + voice.position * channels;
            Sample* dst = out + static_cast<size_t>(offset) * channels;
            const size_t count = static_cast<size_t>(chunk) * channels;
            for (size_t i = 0; i < count; ++i) {
                dst[i] += src[i] * gain;
            }
            voice.position += chunk;
            offset += static_cast<uint32_t>(chunk);
            remaining -= chunk;
        }

        if (voice.position >= sound->frameCount && !voice.loop) {
            RetireVoice(voice);
        } else {
            ++voicesActive;
        }
    }

    const bool relayMicrophone = micPassthrough_.load(std::memory_order_relaxed);
    if (relayMicrophone && micRing_) {
        const size_t backlog = micRing_->FramesAvailable();
        if (backlog > kMaxMicBacklogFrames) {
            micRing_->DiscardOldest(backlog - kMaxMicBacklogFrames);
        }
        micRing_->ReadPadded(micScratch_.data(), frames);
        const float gain = micGain_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < totalSamples; ++i) {
            out[i] += micScratch_[i] * gain;
        }
    }

    const float master = masterGain_.load(std::memory_order_relaxed);
    float peak = 0.0f;
    for (size_t i = 0; i < totalSamples; ++i) {
        float value = out[i] * master;
        if (value > 1.0f) {
            value = 1.0f;
        } else if (value < -1.0f) {
            value = -1.0f;
        }
        out[i] = value;
        const float magnitude = value < 0.0f ? -value : value;
        if (magnitude > peak) {
            peak = magnitude;
        }
    }
    peak_.store(peak, std::memory_order_relaxed);
    activeVoiceCount_.store(voicesActive, std::memory_order_relaxed);
}

// ------------------------------------------------------- control-thread API

bool AudioEngine::Post(const EngineCommand& command) {
    if (!commands_.Push(command)) {
        LogLine(L"engine command queue full, dropping command %d",
                static_cast<int>(command.type));
        return false;
    }
    return true;
}

void AudioEngine::PlaySound(SoundDataPtr sound, uint32_t catalogId, float gain, bool loop) {
    EngineCommand command;
    command.type = EngineCommandType::Play;
    command.sound = std::move(sound);
    command.catalogId = catalogId;
    command.gain = gain;
    command.loop = loop;
    Post(command);
}

void AudioEngine::StopSound(uint32_t catalogId) {
    EngineCommand command;
    command.type = EngineCommandType::StopSound;
    command.catalogId = catalogId;
    Post(command);
}

void AudioEngine::StopAll() {
    EngineCommand command;
    command.type = EngineCommandType::StopAll;
    Post(command);
}

void AudioEngine::SetMasterGain(float gain) {
    masterGain_.store(gain, std::memory_order_relaxed);
    EngineCommand command;
    command.type = EngineCommandType::SetMasterGain;
    command.gain = gain;
    Post(command);
}

void AudioEngine::SetMicGain(float gain) {
    micGain_.store(gain, std::memory_order_relaxed);
    EngineCommand command;
    command.type = EngineCommandType::SetMicGain;
    command.gain = gain;
    Post(command);
}

void AudioEngine::SetMicPassthrough(bool enabled) {
    if (!running_.load(std::memory_order_acquire)) {
        micPassthrough_.store(enabled, std::memory_order_release);
        return;
    }

    if (enabled) {
        micPassthrough_.store(true, std::memory_order_release);
        if (!captureService_) {
            if (!OpenCapture(captureDeviceId_)) {
                micPassthrough_.store(false, std::memory_order_release);
                std::lock_guard<std::mutex> guard(statusMutex_);
                micPassthroughActive_ = false;
                return;
            }
        }
        if (micRing_) {
            micRing_->Clear();
        }
        StartCaptureThread();
        std::lock_guard<std::mutex> guard(statusMutex_);
        micPassthroughActive_ = true;
    } else {
        micPassthrough_.store(false, std::memory_order_release);
        StopCaptureThread();
        if (micRing_) {
            micRing_->Clear();
        }
        std::lock_guard<std::mutex> guard(statusMutex_);
        micPassthroughActive_ = false;
    }
}

bool AudioEngine::IdleForSeconds(double seconds) const {
    if (!running_.load(std::memory_order_acquire)) {
        return true;
    }
    if (micPassthrough_.load(std::memory_order_relaxed)) {
        return false;  // the microphone must keep flowing
    }
    const uint64_t last = lastAudibleMs_.load(std::memory_order_relaxed);
    const uint64_t now = NowMs();
    if (now < last) {
        return false;
    }
    return static_cast<double>(now - last) >= seconds * 1000.0;
}

EngineStatus AudioEngine::Snapshot() const {
    // Releasing retired buffers needs a non-const call; the state it touches is
    // only ever written by the audio thread, so this is safe.
    const_cast<AudioEngine*>(this)->DrainRetired();

    EngineStatus status;
    status.running = running_.load(std::memory_order_acquire);
    status.sampleRate = engineFormat_.sampleRate;
    status.channels = engineFormat_.channels;
    status.bufferFrames = bufferFrames_;
    status.bufferMs = engineFormat_.sampleRate
                          ? 1000.0 * bufferFrames_ / static_cast<double>(engineFormat_.sampleRate)
                          : 0.0;
    status.peak = peak_.load(std::memory_order_relaxed);
    status.activeVoices = activeVoiceCount_.load(std::memory_order_relaxed);
    status.underruns = underruns_.load(std::memory_order_relaxed);
    status.renderLoadPercent = renderLoad_.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> guard(statusMutex_);
    status.renderDeviceName = renderName_;
    status.captureDeviceName = captureName_;
    status.lastError = lastError_;
    status.micPassthroughActive = micPassthroughActive_;
    return status;
}

void AudioEngine::SetEngineError(const std::wstring& text) {
    {
        std::lock_guard<std::mutex> guard(statusMutex_);
        lastError_ = text;
    }
    LogLine(L"%s", text.c_str());
}

}  // namespace echopad
