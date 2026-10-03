// EchoPad - global hotkeys.
//
// RegisterHotKey delivers WM_HOTKEY to our window even while a game has focus,
// which is the whole point of a soundboard. The sound's stable id doubles as the
// hotkey id.
#pragma once

#include "common.h"
#include "media/sound.h"

namespace echopad {

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

class HotkeyManager {
public:
    void Initialize(HWND window) { window_ = window; }
    void Shutdown();

    // Returns false when Windows (or another program) already owns the
    // combination; the caller should tell the user rather than stay silent.
    bool Bind(uint32_t id, const HotkeyBinding& binding);
    void Unbind(uint32_t id);
    void UnbindAll();

    bool isBound(uint32_t id) const;

private:
    HWND window_ = nullptr;
    std::vector<uint32_t> registered_;
};

}  // namespace echopad
