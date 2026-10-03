// EchoPad - audio file decoding through Media Foundation.
//
// Media Foundation is used rather than a bundled decoder because it is already
// part of Windows: no third-party code, no extra DLLs, and hardware/OS codecs
// for MP3, AAC, WMA, FLAC and WAV come for free. The source reader is asked for
// exactly the engine's format, so what comes back is ready to mix.
#pragma once

#include "audio/engine.h"
#include "common.h"

namespace echopad {

struct DecodeResult {
    SoundDataPtr sound;
    std::wstring error;

    bool ok() const { return sound != nullptr; }
};

// Starts Media Foundation once per process. Safe to call repeatedly.
bool MediaFoundationStartup();
void MediaFoundationShutdown();

// Decodes any file Media Foundation can read into interleaved float32 at
// `target`. Handles files longer than kMaxSoundSeconds by truncating.
DecodeResult DecodeAudioFile(const std::wstring& path, const AudioFormat& target);

// Lowercase extension check, e.g. L".mp3".
bool IsSupportedAudioFile(const std::wstring& path);

// "*.wav;*.mp3;..." for the open dialog.
const wchar_t* AudioFileDialogPattern();

// Human readable list for the About/help text.
const wchar_t* AudioFileExtensionList();

}  // namespace echopad
