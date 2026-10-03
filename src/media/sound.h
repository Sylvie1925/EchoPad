// EchoPad - the sound catalog and its on-disk form.
#pragma once

#include "audio/engine.h"
#include "common.h"

#include <deque>
#include <mutex>
#include <thread>

namespace echopad {

struct HotkeyBinding {
    uint32_t modifiers = 0;   // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
    uint32_t virtualKey = 0;  // 0 means unbound

    bool bound() const { return virtualKey != 0; }
    bool operator==(const HotkeyBinding& other) const {
        return modifiers == other.modifiers && virtualKey == other.virtualKey;
    }
};

// "Ctrl+Alt+F5", "Num 1", "Q"
std::wstring FormatHotkey(const HotkeyBinding& binding);

struct Sound {
    uint32_t id = 0;  // stable across removals; also used as the hotkey ID
    std::wstring path;
    std::wstring name;
    SoundDataPtr data;
    HotkeyBinding hotkey;
    float gain = 1.0f;
    bool loop = false;
    bool loading = false;
    bool failed = false;
    uint32_t durationMs = 0;
    std::wstring error;

    bool ready() const { return data != nullptr; }
};

// Owns the sound list, its persistence, and a background decode thread so
// adding a long file never blocks the window.
class SoundBank {
public:
    SoundBank() = default;
    ~SoundBank();

    SoundBank(const SoundBank&) = delete;
    SoundBank& operator=(const SoundBank&) = delete;

    // ---- list access (UI thread)
    size_t size() const { return sounds_.size(); }
    Sound& at(size_t index) { return sounds_[index]; }
    const Sound& at(size_t index) const { return sounds_[index]; }
    int IndexOfId(uint32_t id) const;

    // ---- mutation (UI thread)
    size_t AddPending(const std::wstring& path, const std::wstring& name);
    void Remove(size_t index);
    void MoveUp(size_t index);
    void MoveDown(size_t index);
    void Clear();

    // ---- persistence
    bool LoadList(const std::wstring& path);
    bool SaveList(const std::wstring& path) const;

    // ---- background decoding
    // `notifyWindow` receives `notifyMessage` whenever a decode finishes.
    void StartLoader(const AudioFormat& format, HWND notifyWindow, UINT notifyMessage);
    void StopLoader();
    void SetTargetFormat(const AudioFormat& format);
    AudioFormat targetFormat() const;
    void RequestDecode(size_t index);
    void DecodeAll();
    // Applies finished decodes. Call from the UI thread when notified.
    void ApplyCompleted();

private:
    struct LoadRequest {
        uint32_t id = 0;
        std::wstring path;
    };
    struct LoadResult {
        uint32_t id = 0;
        SoundDataPtr data;
        std::wstring error;
    };

    void LoaderLoop();
    void RetargetAll();

    std::vector<Sound> sounds_;
    uint32_t nextId_ = 1;

    // Loader thread state
    std::thread loader_;
    std::atomic<bool> loaderStop_{false};
    HWND notifyWindow_ = nullptr;
    UINT notifyMessage_ = 0;
    mutable std::mutex requestMutex_;
    std::deque<LoadRequest> requests_;
    std::mutex resultMutex_;
    std::vector<LoadResult> results_;
    mutable std::mutex formatMutex_;
    AudioFormat targetFormat_;
};

}  // namespace echopad
