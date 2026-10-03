#include "hotkeys.h"

#include <algorithm>

#include "core/util.h"

namespace echopad {

void HotkeyManager::Shutdown() {
    UnbindAll();
    window_ = nullptr;
}

bool HotkeyManager::isBound(uint32_t id) const {
    return std::find(registered_.begin(), registered_.end(), id) != registered_.end();
}

bool HotkeyManager::Bind(uint32_t id, const HotkeyBinding& binding) {
    Unbind(id);
    if (!window_ || !binding.bound()) {
        return true;  // nothing to do is not a failure
    }

    const UINT modifiers = binding.modifiers | MOD_NOREPEAT;
    if (!RegisterHotKey(window_, static_cast<int>(id), modifiers, binding.virtualKey)) {
        LogLine(L"RegisterHotKey failed for id=%u (%s), error=%lu", id,
                FormatHotkey(binding).c_str(), GetLastError());
        return false;
    }
    registered_.push_back(id);
    return true;
}

void HotkeyManager::Unbind(uint32_t id) {
    const auto it = std::find(registered_.begin(), registered_.end(), id);
    if (it == registered_.end()) {
        return;
    }
    if (window_) {
        UnregisterHotKey(window_, static_cast<int>(id));
    }
    registered_.erase(it);
}

void HotkeyManager::UnbindAll() {
    if (window_) {
        for (uint32_t id : registered_) {
            UnregisterHotKey(window_, static_cast<int>(id));
        }
    }
    registered_.clear();
}

}  // namespace echopad
