// EchoPad - notification area icon.
#pragma once

// common.h first: it pins WINVER/_WIN32_IE, which shellapi.h needs in order to
// expose the full NOTIFYICONDATA structure.
#include "common.h"

#include <shellapi.h>

namespace echopad {

class TrayIcon {
public:
    ~TrayIcon();

    bool Create(HWND owner, HICON icon, const std::wstring& tooltip);
    void Destroy();
    void SetTooltip(const std::wstring& text);
    void SetIcon(HICON icon);
    void ShowBalloon(const std::wstring& title, const std::wstring& text);

    // The single message the icon uses to talk to its owner window.
    static constexpr UINT kCallbackMessage = WM_APP + 1;

private:
    NOTIFYICONDATAW data_{};
    bool created_ = false;
};

}  // namespace echopad
