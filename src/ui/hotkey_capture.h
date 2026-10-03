// EchoPad - modal "press a key" capture popup.
#pragma once

#include "common.h"
#include "media/sound.h"

namespace echopad {

enum class HotkeyCaptureResult {
    Captured,   // out is filled in
    Cleared,    // user asked for no hotkey
    Cancelled,  // nothing changed
};

// Blocks until the user presses a combination, presses Esc, or clicks away.
// `title` lets the caller say what is being bound.
HotkeyCaptureResult CaptureHotkey(HWND owner, HotkeyBinding& out, const wchar_t* title);

}  // namespace echopad
