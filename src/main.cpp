// EchoPad - program entry point.
#include "app.h"
#include "common.h"
#include "core/util.h"
#include "ui/strings.h"

#include <objbase.h>

namespace {

constexpr wchar_t kSingleInstanceMutex[] = L"Local\\EchoPad.SingleInstance.v1";
constexpr wchar_t kMainWindowClass[] = L"EchoPadMainWindow";

// Brought to the front when a second copy is launched.
void ActivateExistingInstance() {
    HWND existing = FindWindowW(kMainWindowClass, nullptr);
    if (!existing) {
        return;
    }
    if (IsIconic(existing)) {
        ShowWindow(existing, SW_RESTORE);
    } else {
        ShowWindow(existing, SW_SHOW);
    }
    SetForegroundWindow(existing);
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    HANDLE singleInstance = CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
    if (singleInstance && GetLastError() == ERROR_ALREADY_EXISTS) {
        ActivateExistingInstance();
        CloseHandle(singleInstance);
        return 0;
    }

    int exitCode = 0;
    {
        echopad::App app;
        if (!app.Initialize(instance, showCommand)) {
            MessageBoxW(nullptr, L"EchoPad 初始化失败。请查看日志文件了解详情。",
                        echopad::kAppName, MB_ICONERROR | MB_OK);
            exitCode = 1;
        } else {
            exitCode = app.RunMessageLoop();
        }
    }

    CoUninitialize();
    if (singleInstance) {
        ReleaseMutex(singleInstance);
        CloseHandle(singleInstance);
    }
    return exitCode;
}
