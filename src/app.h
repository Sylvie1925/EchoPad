// EchoPad - application object: window, controls, tray, config and glue.
#pragma once

#include "audio/devices.h"
#include "audio/engine.h"
#include "common.h"
#include "core/util.h"
#include "hotkey/hotkeys.h"
#include "media/sound.h"
#include "ui/tray.h"

namespace echopad {

enum : int {
    kIdAddButton = 1001,
    kIdRemoveButton,
    kIdStopButton,
    kIdDeviceCombo,
    kIdMicCheck,
    kIdLoopCheck,
    kIdMasterSlider,
    kIdMicSlider,
    kIdSoundList,
    kIdStatusText,
    kIdStatusTimer = 1100,
};

enum : int {
    kMenuAddFiles = 2001,
    kMenuRescan,
    kMenuOpenConfig,
    kMenuExit,
    kMenuPlaySelected,
    kMenuStopAll,
    kMenuAssignStopHotkey,
    kMenuUsage,
    kMenuLog,
    kMenuAbout,
};

enum : int {
    kTrayShow = 3001,
    kTrayStopAll,
    kTrayPassthrough,
    kTrayExit,
};

enum : int {
    kContextPlay = 4001,
    kContextSetHotkey,
    kContextClearHotkey,
    kContextToggleLoop,
    kContextVolumeUp,
    kContextVolumeDown,
    kContextReveal,
    kContextRemove,
};

// Posted by the background decoder.
inline constexpr UINT kMsgDecodeDone = WM_APP + 10;

// WM_HOTKEY id reserved for the global "stop everything" key. Sound ids start at
// 1 and grow slowly, so this never collides with one of them.
inline constexpr uint32_t kStopAllHotkeyId = 0x40000000;

class App {
public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool Initialize(HINSTANCE instance, int showCommand);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WindowProcThunk(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    // setup
    bool CreateMainWindow(int showCommand);
    void CreateControls();
    void LayoutControls();
    void CreateMenuBar();
    void RefreshPlayMenu();
    void ApplyFont();
    void LoadSettings();
    void SaveSettings();
    void PopulateDeviceCombo();

    // list
    void RebuildList();
    void RefreshRow(size_t index);
    void RefreshAllRows();
    int SelectedIndex() const;
    void SelectIndex(int index);

    // commands
    void OnAddFiles();
    void OnRemoveSelected();
    void OnPlaySelected();
    void OnPlayIndex(size_t index);
    void OnStopAll();
    void OnAssignHotkey();
    void OnAssignStopHotkey();
    void OnClearHotkey();
    void OnToggleLoop();
    void OnAdjustVolume(int deltaPercent);
    void OnRevealInExplorer();
    void OnOutputDeviceChanged();
    void OnMicPassthroughToggled();
    void OnShowContextMenu(POINT screenPoint);
    void OnTrayMessage(LPARAM lparam);
    void OnTrayCommand(int id);
    void OnTimer();
    void OnDecodeFinished();
    void OnShowUsage();
    void OnAbout();
    void OnOpenConfigDirectory();
    void OnOpenLog();

    // engine
    bool EnsureEngine();
    void RestartEngine();
    void SleepEngineIfIdle();
    void StartStatusTimer();
    void StopStatusTimer();
    void UpdateStatusText();
    void ApplyHotkeys();
    void ShowEngineError(const std::wstring& fallback);

    // helpers
    std::wstring SoundListPath() const;
    bool IsVirtualCableSelected() const;
    // Opens a folder in Explorer without going through the shell's "open" verb,
    // which is unreliable on machines with broken folder associations. The
    // second argument is only what gets named in the error message.
    bool RunExplorer(const std::wstring& parameters, const std::wstring& reportedPath);
    std::wstring StopHotkeyLabel() const;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND list_ = nullptr;
    HWND addButton_ = nullptr;
    HWND removeButton_ = nullptr;
    HWND stopButton_ = nullptr;
    HWND deviceCombo_ = nullptr;
    HWND deviceLabel_ = nullptr;
    HWND micCheck_ = nullptr;
    HWND loopCheck_ = nullptr;
    HWND masterLabel_ = nullptr;
    HWND masterSlider_ = nullptr;
    HWND micLabel_ = nullptr;
    HWND micSlider_ = nullptr;
    HWND statusText_ = nullptr;
    HFONT font_ = nullptr;
    bool ownsFont_ = false;
    HICON icon_ = nullptr;
    HMENU menu_ = nullptr;
    HMENU playMenu_ = nullptr;

    AudioEngine engine_;
    SoundBank bank_;
    HotkeyManager hotkeys_;
    TrayIcon tray_;

    std::vector<DeviceInfo> renderDevices_;

    // settings
    std::wstring renderDeviceId_;
    std::wstring captureDeviceId_;
    bool micPassthrough_ = true;
    float masterGain_ = 1.0f;
    float micGain_ = 1.0f;
    std::wstring lastDirectory_;
    // Global "stop everything" key. Defaults to Ctrl+Alt+S.
    HotkeyBinding stopHotkey_;

    bool engineEverStarted_ = false;
    bool statusTimerRunning_ = false;
    bool statusWarningShown_ = false;
    bool shuttingDown_ = false;

    // Cached so the status timer does not repaint or talk to the shell when
    // nothing has actually changed.
    std::wstring lastStatusText_;
    std::wstring lastTrayTip_;
    bool lastTrayRunning_ = false;
};

}  // namespace echopad
