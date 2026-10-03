#include "tray.h"

#include <shellapi.h>

namespace echopad {

TrayIcon::~TrayIcon() {
    Destroy();
}

bool TrayIcon::Create(HWND owner, HICON icon, const std::wstring& tooltip) {
    Destroy();

    data_ = NOTIFYICONDATAW{};
    data_.cbSize = sizeof(NOTIFYICONDATAW);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data_.uCallbackMessage = kCallbackMessage;
    data_.hIcon = icon;
    wcsncpy_s(data_.szTip, tooltip.c_str(), _TRUNCATE);

    created_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    if (created_) {
        data_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data_);
    }
    return created_;
}

void TrayIcon::Destroy() {
    if (created_) {
        Shell_NotifyIconW(NIM_DELETE, &data_);
        created_ = false;
    }
}

void TrayIcon::SetTooltip(const std::wstring& text) {
    if (!created_) {
        return;
    }
    wcsncpy_s(data_.szTip, text.c_str(), _TRUNCATE);
    data_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::SetIcon(HICON icon) {
    if (!created_) {
        return;
    }
    data_.hIcon = icon;
    data_.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::ShowBalloon(const std::wstring& title, const std::wstring& text) {
    if (!created_) {
        return;
    }
    data_.uFlags = NIF_INFO;
    data_.dwInfoFlags = NIIF_NONE;
    wcsncpy_s(data_.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(data_.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

}  // namespace echopad
