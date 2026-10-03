#include "decoder.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <strsafe.h>

#include <algorithm>
#include <iterator>

#include "core/util.h"

namespace echopad {
namespace {

int g_mfRefCount = 0;

// Streams longer than this are truncated so a mis-clicked movie file cannot
// swallow gigabytes of RAM.
constexpr uint32_t kMaxDecodedFramesCeiling = 60u * 60u * 48000u;  // one hour at 48 kHz

bool ConfigureOutputType(IMFMediaType* type, const AudioFormat& target) {
    if (FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio))) return false;
    if (FAILED(type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float))) return false;
    if (FAILED(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, target.channels))) return false;
    if (FAILED(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, target.sampleRate))) return false;
    const uint32_t blockAlign = target.channels * sizeof(float);
    if (FAILED(type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign))) return false;
    if (FAILED(type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                               target.sampleRate * blockAlign))) return false;
    if (FAILED(type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE))) return false;
    return true;
}

}  // namespace

bool MediaFoundationStartup() {
    if (g_mfRefCount++ > 0) {
        return true;
    }
    const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) {
        --g_mfRefCount;
        LogError(L"MFStartup", hr);
        return false;
    }
    return true;
}

void MediaFoundationShutdown() {
    if (g_mfRefCount > 0 && --g_mfRefCount == 0) {
        MFShutdown();
    }
}

DecodeResult DecodeAudioFile(const std::wstring& path, const AudioFormat& target) {
    DecodeResult result;

    if (!MediaFoundationStartup()) {
        result.error = L"无法初始化 Media Foundation（系统缺少媒体组件）";
        return result;
    }

    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, reader.Put());
    if (FAILED(hr)) {
        result.error = L"无法打开文件：" + HrText(hr);
        MediaFoundationShutdown();
        return result;
    }

    ComPtr<IMFMediaType> wanted;
    hr = MFCreateMediaType(wanted.Put());
    if (FAILED(hr) || !ConfigureOutputType(wanted.Get(), target)) {
        result.error = L"无法构造解码输出格式";
        MediaFoundationShutdown();
        return result;
    }

    hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, wanted.Get());
    if (FAILED(hr)) {
        // Retry once without pinning the sample rate; some containers only
        // accept their native rate and rely on the resampler being inserted
        // later. Setting the type without a rate lets MF pick.
        ComPtr<IMFMediaType> relaxed;
        if (SUCCEEDED(MFCreateMediaType(relaxed.Put()))) {
            relaxed->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            relaxed->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
            relaxed->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, target.channels);
            relaxed->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, target.sampleRate);
            hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr,
                                             relaxed.Get());
        }
    }
    if (FAILED(hr)) {
        result.error = L"系统没有能解码这个文件的编解码器：" + HrText(hr);
        MediaFoundationShutdown();
        return result;
    }

    // Confirm what the pipeline actually produces. In practice it is exactly
    // what we asked for; this is a safety net so a mismatch fails loudly rather
    // than playing at the wrong pitch.
    ComPtr<IMFMediaType> actual;
    UINT32 channels = 0;
    UINT32 sampleRate = 0;
    GUID subtype = GUID_NULL;
    if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, actual.Put())) &&
        actual) {
        actual->GetGUID(MF_MT_SUBTYPE, &subtype);
        actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
        actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
    }
    if (channels != target.channels || sampleRate != target.sampleRate ||
        !IsEqualGUID(subtype, MFAudioFormat_Float)) {
        wchar_t detail[256];
        StringCchPrintfW(detail, std::size(detail),
                         L"解码器输出格式不受支持（%u Hz / %u 声道）", sampleRate, channels);
        result.error = detail;
        MediaFoundationShutdown();
        return result;
    }

    auto sound = std::make_shared<SoundData>();
    sound->format = target;

    const uint64_t frameCeiling =
        std::min<uint64_t>(static_cast<uint64_t>(target.sampleRate) * kMaxSoundSeconds,
                           kMaxDecodedFramesCeiling);
    bool truncated = false;

    for (;;) {
        DWORD streamFlags = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &streamFlags,
                                nullptr, sample.Put());
        if (FAILED(hr)) {
            result.error = L"读取音频数据失败：" + HrText(hr);
            MediaFoundationShutdown();
            return result;
        }
        if (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
            break;
        }
        if (streamFlags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            // Re-check the negotiated format after a mid-stream change.
            ComPtr<IMFMediaType> changed;
            UINT32 changedRate = 0;
            UINT32 changedChannels = 0;
            if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                                      changed.Put())) &&
                changed) {
                changed->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &changedChannels);
                changed->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &changedRate);
                if (changedChannels != target.channels || changedRate != target.sampleRate) {
                    result.error = L"文件在播放过程中改变了格式";
                    MediaFoundationShutdown();
                    return result;
                }
            }
        }
        if (!sample) {
            continue;
        }

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.Put()))) {
            continue;
        }
        BYTE* data = nullptr;
        DWORD length = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &length)) || !data) {
            continue;
        }
        const size_t sampleCount = length / sizeof(float);
        sound->samples.insert(sound->samples.end(), reinterpret_cast<const float*>(data),
                              reinterpret_cast<const float*>(data) + sampleCount);
        buffer->Unlock();

        if (sound->samples.size() / target.channels >= frameCeiling) {
            truncated = true;
            break;
        }
    }

    reader.Reset();
    MediaFoundationShutdown();

    sound->frameCount = sound->samples.size() / target.channels;
    sound->durationMs = target.sampleRate
                            ? static_cast<uint32_t>(sound->frameCount * 1000 / target.sampleRate)
                            : 0;

    if (sound->frameCount == 0) {
        result.error = L"文件不包含可用的音频数据";
        return result;
    }
    if (truncated) {
        LogLine(L"decode: %s truncated to %u s", path.c_str(), kMaxSoundSeconds);
    }

    // Trim odd trailing bytes so the mixer never reads past the end.
    sound->samples.resize(static_cast<size_t>(sound->frameCount) * target.channels);

    result.sound = std::move(sound);
    return result;
}

bool IsSupportedAudioFile(const std::wstring& path) {
    static const wchar_t* kExtensions[] = {
        L".wav", L".mp3",  L".m4a", L".aac", L".wma", L".flac",
        L".mp4", L".aif",  L".aiff", L".alac", L".adts",
    };
    const std::wstring extension = ExtensionOf(path);
    if (extension.empty()) {
        return false;
    }
    for (const wchar_t* known : kExtensions) {
        if (extension == known) {
            return true;
        }
    }
    return false;
}

const wchar_t* AudioFileDialogPattern() {
    return L"*.wav;*.mp3;*.m4a;*.aac;*.wma;*.flac;*.mp4;*.aif;*.aiff";
}

const wchar_t* AudioFileExtensionList() {
    return L"WAV / MP3 / M4A / AAC / WMA / FLAC / MP4 / AIFF";
}

}  // namespace echopad
