#include "hotkey_capture.h"

#include <strsafe.h>

#include "ui/strings.h"

namespace echopad {
namespace {

constexpr wchar_t kCaptureClassName[] = L"EchoPadHotkeyCapture";

struct CaptureState {
    HWND window = nullptr;
    HWND owner = nullptr;
    HotkeyCaptureResult result = HotkeyCaptureResult::Cancelled;
    HotkeyBinding binding;
    bool done = false;
};

bool IsModifierKey(uint32_t key) {
    switch (key) {
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
        case VK_LWIN:
        case VK_RWIN:
            return true;
        default:
            return false;
    }
}

uint32_t CurrentModifiers() {
    uint32_t modifiers = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= MOD_ALT;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= MOD_SHIFT;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) modifiers |= MOD_WIN;
    return modifiers;
}

LRESULT CALLBACK CaptureProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<CaptureState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
        case WM_NCCREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }
        case WM_ERASEBKGND: {
            RECT rect{};
            GetClientRect(window, &rect);
            FillRect(reinterpret_cast<HDC>(wparam), &rect,
                     reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            return 1;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (!state) {
                break;
            }
            const uint32_t key = static_cast<uint32_t>(wparam);
            if (key == VK_ESCAPE) {
                state->result = HotkeyCaptureResult::Cancelled;
                state->done = true;
                return 0;
            }
            if (key == VK_BACK || key == VK_DELETE) {
                state->result = HotkeyCaptureResult::Cleared;
                state->binding = HotkeyBinding{};
                state->done = true;
                return 0;
            }
            if (IsModifierKey(key)) {
                return 0;  // wait for the actual key
            }
            state->binding.modifiers = CurrentModifiers();
            state->binding.virtualKey = key;
            state->result = HotkeyCaptureResult::Captured;
            state->done = true;
            return 0;
        }
        case WM_KILLFOCUS: {
            if (state && !state->done) {
                state->result = HotkeyCaptureResult::Cancelled;
                state->done = true;
            }
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wparam & 0xFFF0) == SC_CLOSE) {
                if (state) {
                    state->result = HotkeyCaptureResult::Cancelled;
                    state->done = true;
                }
                return 0;
            }
            break;
        case WM_CLOSE: {
            if (state) {
                state->result = HotkeyCaptureResult::Cancelled;
                state->done = true;
            }
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void EnsureClassRegistered(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = CaptureProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kCaptureClassName;
    RegisterClassExW(&wc);
    registered = true;
}

}  // namespace

HotkeyCaptureResult CaptureHotkey(HWND owner, HotkeyBinding& out, const wchar_t* title) {
    const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    EnsureClassRegistered(instance);

    CaptureState state;
    state.owner = owner;

    constexpr int kWidth = 380;
    constexpr int kHeight = 110;

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - kWidth) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - kHeight) / 2;

    state.window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kCaptureClassName,
                                   title ? title : kTextCaptureTitle, WS_POPUP | WS_BORDER, x, y,
                                   kWidth, kHeight, owner, nullptr, instance, &state);
    if (!state.window) {
        return HotkeyCaptureResult::Cancelled;
    }

    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HWND hint = CreateWindowExW(0, L"STATIC", kTextCaptureHint,
                                WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 12, 12,
                                kWidth - 24, kHeight - 36, state.window, nullptr, instance, nullptr);
    SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    EnableWindow(owner, FALSE);
    ShowWindow(state.window, SW_SHOW);
    SetForegroundWindow(state.window);
    SetFocus(state.window);

    MSG message;
    while (!state.done && GetMessageW(&message, nullptr, 0, 0)) {
        if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
            // Deliver key presses to the capture window regardless of focus.
            if (message.hwnd != state.window) {
                PostMessageW(state.window, message.message, message.wParam, message.lParam);
                continue;
            }
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    EnableWindow(owner, TRUE);
    DestroyWindow(state.window);
    SetForegroundWindow(owner);

    if (state.result == HotkeyCaptureResult::Captured) {
        out = state.binding;
    }
    return state.result;
}

}  // namespace echopad
