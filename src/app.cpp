#include "app.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <strsafe.h>

#include "core/util.h"
#include "media/decoder.h"
#include "resource.h"
#include "ui/hotkey_capture.h"
#include "ui/strings.h"

namespace echopad {
namespace {

constexpr wchar_t kWindowClassName[] = L"EchoPadMainWindow";

// Config keys
constexpr wchar_t kKeyRenderDevice[] = L"renderDevice";
constexpr wchar_t kKeyCaptureDevice[] = L"captureDevice";
constexpr wchar_t kKeyMicPassthrough[] = L"micPassthrough";
constexpr wchar_t kKeyMasterGain[] = L"masterGain";
constexpr wchar_t kKeyMicGain[] = L"micGain";
constexpr wchar_t kKeyLastDirectory[] = L"lastDirectory";
constexpr wchar_t kKeyStopHotkeyModifiers[] = L"stopHotkeyModifiers";
constexpr wchar_t kKeyStopHotkeyKey[] = L"stopHotkeyKey";

// Ctrl+Alt+S out of the box: rarely taken by games, and easy to change.
constexpr uint32_t kDefaultStopModifiers = MOD_CONTROL | MOD_ALT;
constexpr uint32_t kDefaultStopKey = 'S';

constexpr int kMargin = 10;
constexpr int kRowHeight = 26;

std::wstring FormatDuration(uint32_t milliseconds) {
    wchar_t buffer[32];
    const uint32_t seconds = milliseconds / 1000;
    if (milliseconds >= 3600000u) {
        StringCchPrintfW(buffer, std::size(buffer), L"%u:%02u:%02u", seconds / 3600,
                         (seconds / 60) % 60, seconds % 60);
    } else {
        StringCchPrintfW(buffer, std::size(buffer), L"%u:%02u", seconds / 60, seconds % 60);
    }
    return buffer;
}

std::wstring FormatVolume(float gain) {
    wchar_t buffer[32];
    StringCchPrintfW(buffer, std::size(buffer), L"%d%%", static_cast<int>(gain * 100.0f + 0.5f));
    return buffer;
}

void ListViewSetText(HWND list, int row, int column, const std::wstring& text) {
    ListView_SetItemText(list, row, column, const_cast<wchar_t*>(text.c_str()));
}

int InsertListRow(HWND list, int row, const std::wstring& text, LPARAM data) {
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = row;
    item.pszText = const_cast<wchar_t*>(text.c_str());
    item.lParam = data;
    return ListView_InsertItem(list, &item);
}

std::wstring ExecutableFileName() {
    return FileNameOf(ExePath());
}

}  // namespace

App::~App() {
    if (font_ && ownsFont_) {
        DeleteObject(font_);
    }
}

// ------------------------------------------------------------------ lifecycle

bool App::Initialize(HINSTANCE instance, int showCommand) {
    instance_ = instance;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES |
                     ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&controls);

    MediaFoundationStartup();

    LogLine(L"=== %s %s starting (%s) ===", kAppName, kAppVersion, ExecutableFileName().c_str());
    LogLine(L"config directory: %s", ConfigDirectory().c_str());

    LoadSettings();

    if (!CreateMainWindow(showCommand)) {
        return false;
    }
    CreateMenuBar();
    CreateControls();
    ApplyFont();
    PopulateDeviceCombo();
    LayoutControls();

    icon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON),
                                          GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    HICON bigIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APPICON),
                                                  IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                                  GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    if (!icon_) {
        icon_ = LoadIconW(nullptr, IDI_APPLICATION);
    }
    if (!bigIcon) {
        bigIcon = icon_;
    }
    SendMessageW(window_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon_));
    SendMessageW(window_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));

    tray_.Create(window_, icon_, kAppName);
    hotkeys_.Initialize(window_);

    // The bank decodes for the format the engine prefers. If the render device
    // turns out to need something else, SetTargetFormat re-decodes everything.
    bank_.StartLoader(AudioFormat{kEngineSampleRate, kEngineChannels}, window_, kMsgDecodeDone);
    bank_.LoadList(SoundListPath());
    bank_.DecodeAll();
    RebuildList();

    SendMessageW(masterSlider_, TBM_SETPOS, TRUE, static_cast<LPARAM>(masterGain_ * 100.0f));
    SendMessageW(micSlider_, TBM_SETPOS, TRUE, static_cast<LPARAM>(micGain_ * 100.0f));
    SendMessageW(micCheck_, BM_SETCHECK,
                 micPassthrough_ ? BST_CHECKED : BST_UNCHECKED, 0);

    ApplyHotkeys();

    ShowWindow(window_, showCommand);
    UpdateWindow(window_);

    if (micPassthrough_) {
        EnsureEngine();
    }
    UpdateStatusText();
    return true;
}

int App::RunMessageLoop() {
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN &&
            GetFocus() == list_) {
            OnPlaySelected();
            continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == 'O' &&
            (GetKeyState(VK_CONTROL) & 0x8000) && IsWindowVisible(window_)) {
            OnAddFiles();
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool App::CreateMainWindow(int showCommand) {
    (void)showCommand;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProcThunk;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWindowClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(0, kWindowClassName, kAppName, WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 960, 580, nullptr, nullptr, instance_,
                              this);
    return window_ != nullptr;
}

void App::CreateMenuBar() {
    menu_ = CreateMenu();

    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, kMenuAddFiles, kLabelMenuAddFiles);
    AppendMenuW(fileMenu, MF_STRING, kMenuRescan, kLabelMenuRescan);
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, kMenuOpenConfig, kLabelMenuOpenConfig);
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, kMenuExit, kLabelMenuExit);

    playMenu_ = CreatePopupMenu();
    RefreshPlayMenu();

    HMENU helpMenu = CreatePopupMenu();
    AppendMenuW(helpMenu, MF_STRING, kMenuUsage, kLabelMenuUsage);
    AppendMenuW(helpMenu, MF_STRING, kMenuLog, kLabelMenuLog);
    AppendMenuW(helpMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(helpMenu, MF_STRING, kMenuAbout, kLabelMenuAbout);

    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), kLabelMenuFile);
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(playMenu_), kLabelMenuPlay);
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), kLabelMenuHelp);
    SetMenu(window_, menu_);
}

std::wstring App::StopHotkeyLabel() const {
    if (!stopHotkey_.bound()) {
        return kLabelMenuStopAll;
    }
    return std::wstring(kLabelMenuStopAll) + L"\t" + FormatHotkey(stopHotkey_);
}

void App::RefreshPlayMenu() {
    if (!playMenu_) {
        return;
    }
    while (GetMenuItemCount(playMenu_) > 0) {
        DeleteMenu(playMenu_, 0, MF_BYPOSITION);
    }
    // The stop entry shows the key that is actually registered, so the menu can
    // never advertise a shortcut that does nothing.
    AppendMenuW(playMenu_, MF_STRING, kMenuPlaySelected, kLabelMenuPlaySelected);
    const std::wstring stopLabel = StopHotkeyLabel();
    AppendMenuW(playMenu_, MF_STRING, kMenuStopAll, stopLabel.c_str());
    AppendMenuW(playMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(playMenu_, MF_STRING, kMenuAssignStopHotkey, kLabelMenuAssignStopHotkey);

    if (menu_) {
        DrawMenuBar(window_);
    }
}

void App::CreateControls() {
    list_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
                                LVS_SHOWSELALWAYS,
                            0, 0, 10, 10, window_, reinterpret_cast<HMENU>(kIdSoundList), instance_,
                            nullptr);
    ListView_SetExtendedListViewStyle(
        list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);

    struct Column {
        const wchar_t* text;
        int width;
    };
    const Column columns[] = {
        {kColumnName, 330}, {kColumnHotkey, 130}, {kColumnDuration, 80}, {kColumnVolume, 70},
        {kColumnLoop, 60},
    };
    for (int index = 0; index < static_cast<int>(std::size(columns)); ++index) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(columns[index].text);
        column.cx = columns[index].width;
        column.iSubItem = index;
        ListView_InsertColumn(list_, index, &column);
    }

    addButton_ = CreateWindowExW(0, WC_BUTTONW, kTextAdd, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                 0, 0, 10, 10, window_, reinterpret_cast<HMENU>(kIdAddButton),
                                 instance_, nullptr);
    removeButton_ = CreateWindowExW(0, WC_BUTTONW, kTextRemove, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                    0, 0, 10, 10, window_, reinterpret_cast<HMENU>(kIdRemoveButton),
                                    instance_, nullptr);
    stopButton_ = CreateWindowExW(0, WC_BUTTONW, kTextStopAll, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  0, 0, 10, 10, window_, reinterpret_cast<HMENU>(kIdStopButton),
                                  instance_, nullptr);

    deviceLabel_ = CreateWindowExW(0, WC_STATICW, kTextOutputDevice,
                                   WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 10, 10, window_, nullptr,
                                   instance_, nullptr);
    deviceCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                       CBS_DROPDOWNLIST,
                                   0, 0, 10, 200, window_, reinterpret_cast<HMENU>(kIdDeviceCombo),
                                   instance_, nullptr);

    masterLabel_ = CreateWindowExW(0, WC_STATICW, kTextMasterVolume, WS_CHILD | WS_VISIBLE,
                                   0, 0, 10, 10, window_, nullptr, instance_, nullptr);
    masterSlider_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
                                    0, 0, 10, 10, window_, reinterpret_cast<HMENU>(kIdMasterSlider),
                                    instance_, nullptr);
    SendMessageW(masterSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 200));
    SendMessageW(masterSlider_, TBM_SETPOS, TRUE, 100);

    micLabel_ = CreateWindowExW(0, WC_STATICW, kTextMicVolume, WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                                window_, nullptr, instance_, nullptr);
    micSlider_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, 0, 0,
                                 10, 10, window_, reinterpret_cast<HMENU>(kIdMicSlider), instance_,
                                 nullptr);
    SendMessageW(micSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 200));
    SendMessageW(micSlider_, TBM_SETPOS, TRUE, 100);

    micCheck_ = CreateWindowExW(0, WC_BUTTONW, kTextMicPassthrough,
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 10, 10,
                                window_, reinterpret_cast<HMENU>(kIdMicCheck), instance_, nullptr);
    loopCheck_ = CreateWindowExW(0, WC_BUTTONW, kTextLoop,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 10, 10,
                                 window_, reinterpret_cast<HMENU>(kIdLoopCheck), instance_, nullptr);

    statusText_ = CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                                  0, 0, 10, 10, window_, reinterpret_cast<HMENU>(kIdStatusText),
                                  instance_, nullptr);
}

void App::ApplyFont() {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        font_ = CreateFontIndirectW(&metrics.lfMessageFont);
        ownsFont_ = font_ != nullptr;
    }
    if (!font_) {
        font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        ownsFont_ = false;
    }

    HWND children[] = {list_,        addButton_,   removeButton_, stopButton_, deviceLabel_,
                       deviceCombo_, micCheck_,    loopCheck_,    masterLabel_, masterSlider_,
                       micLabel_,    micSlider_,   statusText_};
    for (HWND child : children) {
        if (child) {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
    }
}

void App::LayoutControls() {
    if (!window_) {
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    const int buttonWidth = 90;
    int x = kMargin;
    int y = kMargin;

    MoveWindow(addButton_, x, y, buttonWidth, kRowHeight, TRUE);
    x += buttonWidth + 6;
    MoveWindow(removeButton_, x, y, buttonWidth, kRowHeight, TRUE);
    x += buttonWidth + 6;
    MoveWindow(stopButton_, x, y, buttonWidth, kRowHeight, TRUE);

    const int comboWidth = 320;
    const int labelWidth = 70;
    MoveWindow(deviceLabel_, width - kMargin - comboWidth - labelWidth - 6, y + 4, labelWidth, 20,
               TRUE);
    MoveWindow(deviceCombo_, width - kMargin - comboWidth, y, comboWidth, 200, TRUE);

    y += kRowHeight + 8;
    const int listHeight = height - y - 96;
    MoveWindow(list_, kMargin, y, width - kMargin * 2, listHeight > 60 ? listHeight : 60, TRUE);

    const int bottomY = y + (listHeight > 60 ? listHeight : 60) + 8;
    int sliderY = bottomY;
    MoveWindow(masterLabel_, kMargin, sliderY + 4, 56, 20, TRUE);
    MoveWindow(masterSlider_, kMargin + 60, sliderY, 170, 24, TRUE);
    MoveWindow(micLabel_, kMargin + 244, sliderY + 4, 56, 20, TRUE);
    MoveWindow(micSlider_, kMargin + 302, sliderY, 170, 24, TRUE);

    MoveWindow(micCheck_, kMargin, bottomY + 30, 120, 22, TRUE);
    MoveWindow(loopCheck_, kMargin + 130, bottomY + 30, 90, 22, TRUE);

    MoveWindow(statusText_, kMargin, bottomY + 56, width - kMargin * 2, 20, TRUE);
}

// -------------------------------------------------------------------- settings

std::wstring App::SoundListPath() const {
    return JoinPath(ConfigDirectory(), L"sounds.tsv");
}

void App::LoadSettings() {
    KeyValueFile config;
    config.Load(ConfigFilePath());
    renderDeviceId_ = config.Get(kKeyRenderDevice);
    captureDeviceId_ = config.Get(kKeyCaptureDevice);
    micPassthrough_ = config.GetBool(kKeyMicPassthrough, true);
    masterGain_ = config.GetFloat(kKeyMasterGain, 1.0f);
    micGain_ = config.GetFloat(kKeyMicGain, 1.0f);
    lastDirectory_ = config.Get(kKeyLastDirectory);

    stopHotkey_.modifiers =
        static_cast<uint32_t>(config.GetInt(kKeyStopHotkeyModifiers, kDefaultStopModifiers));
    stopHotkey_.virtualKey =
        static_cast<uint32_t>(config.GetInt(kKeyStopHotkeyKey, kDefaultStopKey));
}

void App::SaveSettings() {
    KeyValueFile config;
    config.Set(kKeyRenderDevice, renderDeviceId_);
    config.Set(kKeyCaptureDevice, captureDeviceId_);
    config.SetBool(kKeyMicPassthrough, micPassthrough_);
    config.SetFloat(kKeyMasterGain, masterGain_);
    config.SetFloat(kKeyMicGain, micGain_);
    config.Set(kKeyLastDirectory, lastDirectory_);
    config.SetInt(kKeyStopHotkeyModifiers, static_cast<int>(stopHotkey_.modifiers));
    config.SetInt(kKeyStopHotkeyKey, static_cast<int>(stopHotkey_.virtualKey));
    if (!config.Save(ConfigFilePath())) {
        LogLine(L"failed to save settings to %s", ConfigFilePath().c_str());
    }
    bank_.SaveList(SoundListPath());
}

void App::PopulateDeviceCombo() {
    renderDevices_ = EnumerateDevices(DeviceFlow::Render);

    SendMessageW(deviceCombo_, CB_RESETCONTENT, 0, 0);
    for (const DeviceInfo& device : renderDevices_) {
        SendMessageW(deviceCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(device.name.c_str()));
    }

    if (renderDevices_.empty()) {
        SendMessageW(deviceCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(没有可用设备)"));
        SendMessageW(deviceCombo_, CB_SETCURSEL, 0, 0);
        return;
    }

    // Prefer the saved device, then a virtual cable, then the system default.
    int selected = -1;
    if (!renderDeviceId_.empty()) {
        for (size_t index = 0; index < renderDevices_.size(); ++index) {
            if (renderDevices_[index].id == renderDeviceId_) {
                selected = static_cast<int>(index);
                break;
            }
        }
    }
    if (selected < 0) {
        for (size_t index = 0; index < renderDevices_.size(); ++index) {
            if (LooksLikeVirtualCable(renderDevices_[index].name)) {
                selected = static_cast<int>(index);
                break;
            }
        }
    }
    if (selected < 0) {
        for (size_t index = 0; index < renderDevices_.size(); ++index) {
            if (renderDevices_[index].isDefault) {
                selected = static_cast<int>(index);
                break;
            }
        }
    }
    if (selected < 0) {
        selected = 0;
    }

    SendMessageW(deviceCombo_, CB_SETCURSEL, selected, 0);
    renderDeviceId_ = renderDevices_[static_cast<size_t>(selected)].id;
}

bool App::IsVirtualCableSelected() const {
    const int selection = static_cast<int>(SendMessageW(deviceCombo_, CB_GETCURSEL, 0, 0));
    if (selection < 0 || selection >= static_cast<int>(renderDevices_.size())) {
        return false;
    }
    return LooksLikeVirtualCable(renderDevices_[static_cast<size_t>(selection)].name);
}

// ------------------------------------------------------------------------ list

void App::RebuildList() {
    if (!list_) {
        return;
    }
    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list_);

    for (size_t index = 0; index < bank_.size(); ++index) {
        InsertListRow(list_, static_cast<int>(index), bank_.at(index).name,
                      static_cast<LPARAM>(index));
        RefreshRow(index);
    }

    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list_, nullptr, TRUE);
}

void App::RefreshRow(size_t index) {
    if (!list_ || index >= bank_.size()) {
        return;
    }
    const Sound& sound = bank_.at(index);
    const int row = static_cast<int>(index);
    ListViewSetText(list_, row, 0, sound.name);
    ListViewSetText(list_, row, 1, sound.hotkey.bound() ? FormatHotkey(sound.hotkey) : L"—");
    if (sound.loading) {
        ListViewSetText(list_, row, 2, L"载入中…");
    } else if (sound.failed) {
        ListViewSetText(list_, row, 2, L"失败");
    } else {
        ListViewSetText(list_, row, 2, FormatDuration(sound.durationMs));
    }
    ListViewSetText(list_, row, 3, FormatVolume(sound.gain));
    ListViewSetText(list_, row, 4, sound.loop ? L"是" : L"");
}

void App::RefreshAllRows() {
    for (size_t index = 0; index < bank_.size(); ++index) {
        RefreshRow(index);
    }
}

int App::SelectedIndex() const {
    return ListView_GetNextItem(list_, -1, LVNI_SELECTED);
}

void App::SelectIndex(int index) {
    ListView_SetItemState(list_, index, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list_, index, FALSE);
}

// -------------------------------------------------------------------- commands

void App::OnAddFiles() {
    std::wstring filter;
    filter += kDialogAddFilter;
    filter.push_back(L'\0');
    filter += AudioFileDialogPattern();
    filter.push_back(L'\0');
    filter += kDialogAllFiles;
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    filter.push_back(L'\0');

    std::vector<wchar_t> buffer(64 * 1024, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle = kDialogAddTitle;
    dialog.lpstrInitialDir = lastDirectory_.empty() ? nullptr : lastDirectory_.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY |
                   OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) {
        return;
    }

    std::vector<std::wstring> paths;
    const wchar_t* cursor = buffer.data();
    const std::wstring first = cursor;
    cursor += first.size() + 1;
    if (*cursor == L'\0') {
        paths.push_back(first);
    } else {
        while (*cursor) {
            paths.push_back(JoinPath(first, cursor));
            cursor += wcslen(cursor) + 1;
        }
    }
    if (paths.empty()) {
        return;
    }

    lastDirectory_ = ExeDirectory();
    {
        const size_t slash = paths.front().find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            lastDirectory_ = paths.front().substr(0, slash);
        }
    }

    size_t added = 0;
    for (const std::wstring& path : paths) {
        if (!FileExists(path)) {
            continue;
        }
        bool duplicate = false;
        for (size_t index = 0; index < bank_.size(); ++index) {
            if (EqualsNoCase(bank_.at(index).path, path)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        if (!IsSupportedAudioFile(path)) {
            LogLine(L"skipping unsupported file: %s", path.c_str());
            continue;
        }
        const size_t index = bank_.AddPending(path, FileStemOf(path));
        bank_.RequestDecode(index);
        ++added;
    }

    if (added > 0) {
        RebuildList();
        SaveSettings();
        SetWindowTextW(statusText_, L"正在解码新增的音频…");
    }
}

void App::OnRemoveSelected() {
    const int selected = SelectedIndex();
    if (selected < 0) {
        return;
    }
    const uint32_t id = bank_.at(static_cast<size_t>(selected)).id;
    hotkeys_.Unbind(id);
    engine_.StopSound(id);
    bank_.Remove(static_cast<size_t>(selected));
    RebuildList();
    SaveSettings();
}

void App::OnPlaySelected() {
    const int selected = SelectedIndex();
    if (selected >= 0) {
        OnPlayIndex(static_cast<size_t>(selected));
    }
}

void App::OnPlayIndex(size_t index) {
    if (index >= bank_.size()) {
        return;
    }
    const Sound& sound = bank_.at(index);
    if (!sound.ready()) {
        if (sound.failed) {
            MessageBoxW(window_, sound.error.c_str(), kAppName, MB_ICONWARNING);
        }
        return;
    }
    if (!EnsureEngine()) {
        return;
    }
    engine_.PlaySound(sound.data, sound.id, sound.gain, sound.loop);
    StartStatusTimer();
    UpdateStatusText();
}

void App::OnStopAll() {
    engine_.StopAll();
    UpdateStatusText();
}

void App::OnAssignHotkey() {
    const int selected = SelectedIndex();
    if (selected < 0) {
        return;
    }
    Sound& sound = bank_.at(static_cast<size_t>(selected));

    HotkeyBinding binding = sound.hotkey;
    const HotkeyCaptureResult result = CaptureHotkey(window_, binding, kTextCaptureTitle);
    if (result == HotkeyCaptureResult::Cancelled) {
        return;
    }

    hotkeys_.Unbind(sound.id);
    if (result == HotkeyCaptureResult::Cleared) {
        sound.hotkey = HotkeyBinding{};
    } else {
        if (!hotkeys_.Bind(sound.id, binding)) {
            MessageBoxW(window_, kErrorHotkeyTaken, kAppName, MB_ICONWARNING);
            hotkeys_.Bind(sound.id, sound.hotkey);
            return;
        }
        sound.hotkey = binding;
    }
    RefreshRow(static_cast<size_t>(selected));
    SaveSettings();
}

void App::OnClearHotkey() {
    const int selected = SelectedIndex();
    if (selected < 0) {
        return;
    }
    Sound& sound = bank_.at(static_cast<size_t>(selected));
    hotkeys_.Unbind(sound.id);
    sound.hotkey = HotkeyBinding{};
    RefreshRow(static_cast<size_t>(selected));
    SaveSettings();
}

void App::OnToggleLoop() {
    const int selected = SelectedIndex();
    if (selected < 0) {
        // Nothing selected: leave the checkbox where it was rather than letting
        // it show a state that belongs to no sound.
        SendMessageW(loopCheck_, BM_SETCHECK, BST_UNCHECKED, 0);
        return;
    }
    Sound& sound = bank_.at(static_cast<size_t>(selected));
    sound.loop = !sound.loop;
    SendMessageW(loopCheck_, BM_SETCHECK, sound.loop ? BST_CHECKED : BST_UNCHECKED, 0);
    RefreshRow(static_cast<size_t>(selected));
    SaveSettings();
}

void App::OnAdjustVolume(int deltaPercent) {
    const int selected = SelectedIndex();
    if (selected < 0) {
        return;
    }
    Sound& sound = bank_.at(static_cast<size_t>(selected));
    sound.gain = static_cast<float>(sound.gain * 100.0f + deltaPercent) / 100.0f;
    if (sound.gain < 0.05f) sound.gain = 0.05f;
    if (sound.gain > 4.0f) sound.gain = 4.0f;
    RefreshRow(static_cast<size_t>(selected));
    SaveSettings();
}

void App::OnRevealInExplorer() {
    const int selected = SelectedIndex();
    if (selected < 0) {
        return;
    }
    const std::wstring& path = bank_.at(static_cast<size_t>(selected)).path;
    RunExplorer(L"/select,\"" + path + L"\"", path);
}

void App::OnOutputDeviceChanged() {
    const int selection = static_cast<int>(SendMessageW(deviceCombo_, CB_GETCURSEL, 0, 0));
    if (selection < 0 || selection >= static_cast<int>(renderDevices_.size())) {
        return;
    }
    renderDeviceId_ = renderDevices_[static_cast<size_t>(selection)].id;
    SaveSettings();
    RestartEngine();
    UpdateStatusText();

    if (!IsVirtualCableSelected() && !statusWarningShown_) {
        statusWarningShown_ = true;
        tray_.ShowBalloon(kAppName, kHintNoVirtualCable);
    }
}

void App::OnMicPassthroughToggled() {
    micPassthrough_ =
        SendMessageW(micCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    SaveSettings();

    if (micPassthrough_) {
        if (!EnsureEngine()) {
            micPassthrough_ = false;
            SendMessageW(micCheck_, BM_SETCHECK, BST_UNCHECKED, 0);
            return;
        }
        engine_.SetMicPassthrough(true);
        StartStatusTimer();
    } else if (engine_.running()) {
        engine_.SetMicPassthrough(false);
    }
    UpdateStatusText();
}

void App::OnShowContextMenu(POINT screenPoint) {
    const int selected = SelectedIndex();
    if (selected < 0) {
        return;
    }
    const Sound& sound = bank_.at(static_cast<size_t>(selected));

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kContextPlay, kLabelContextPlay);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (sound.hotkey.bound() ? MF_ENABLED : MF_GRAYED),
                kContextClearHotkey, kLabelContextClearHotkey);
    AppendMenuW(menu, MF_STRING, kContextSetHotkey, kLabelContextSetHotkey);
    AppendMenuW(menu, MF_STRING, kContextToggleLoop,
                sound.loop ? kLabelContextLoopOff : kLabelContextLoopOn);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kContextVolumeUp, kLabelContextVolumeUp);
    AppendMenuW(menu, MF_STRING, kContextVolumeDown, kLabelContextVolumeDown);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kContextReveal, kLabelContextReveal);
    AppendMenuW(menu, MF_STRING, kContextRemove, kLabelContextRemove);

    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x,
                                       screenPoint.y, 0, window_, nullptr);
    DestroyMenu(menu);

    switch (command) {
        case kContextPlay: OnPlaySelected(); break;
        case kContextSetHotkey: OnAssignHotkey(); break;
        case kContextClearHotkey: OnClearHotkey(); break;
        case kContextToggleLoop: OnToggleLoop(); break;
        case kContextVolumeUp: OnAdjustVolume(10); break;
        case kContextVolumeDown: OnAdjustVolume(-10); break;
        case kContextReveal: OnRevealInExplorer(); break;
        case kContextRemove: OnRemoveSelected(); break;
        default: break;
    }
}

// ----------------------------------------------------------------------- tray

void App::OnTrayMessage(LPARAM lparam) {
    switch (LOWORD(lparam)) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK: {
            ShowWindow(window_, SW_SHOW);
            ShowWindow(window_, SW_RESTORE);
            SetForegroundWindow(window_);
            break;
        }
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            POINT cursor{};
            GetCursorPos(&cursor);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, kTrayShow, kLabelTrayShow);
            const std::wstring trayStopLabel = StopHotkeyLabel();
            AppendMenuW(menu, MF_STRING, kTrayStopAll, trayStopLabel.c_str());
            AppendMenuW(menu, MF_STRING | (micPassthrough_ ? MF_CHECKED : MF_UNCHECKED),
                        kTrayPassthrough, kLabelTrayPassthrough);
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kTrayExit, kLabelTrayExit);

            SetForegroundWindow(window_);
            const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x,
                                               cursor.y, 0, window_, nullptr);
            DestroyMenu(menu);
            if (command) {
                OnTrayCommand(command);
            }
            break;
        }
        default:
            break;
    }
}

void App::OnTrayCommand(int id) {
    switch (id) {
        case kTrayShow:
            ShowWindow(window_, SW_SHOW);
            ShowWindow(window_, SW_RESTORE);
            SetForegroundWindow(window_);
            break;
        case kTrayStopAll:
            OnStopAll();
            break;
        case kTrayPassthrough:
            SendMessageW(micCheck_, BM_SETCHECK,
                         micPassthrough_ ? BST_UNCHECKED : BST_CHECKED, 0);
            OnMicPassthroughToggled();
            break;
        case kTrayExit:
            shuttingDown_ = true;
            DestroyWindow(window_);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------- engine

bool App::EnsureEngine() {
    if (engine_.running()) {
        return true;
    }

    const std::wstring renderId = ResolveDeviceId(DeviceFlow::Render, renderDeviceId_);
    if (renderId.empty()) {
        MessageBoxW(window_, kErrorNoDevice, kAppName, MB_ICONERROR);
        return false;
    }

    std::wstring renderName = L"(未知设备)";
    for (const DeviceInfo& device : renderDevices_) {
        if (device.id == renderId) {
            renderName = device.name;
            break;
        }
    }

    const std::wstring captureId = ResolveDeviceId(DeviceFlow::Capture, captureDeviceId_);
    std::wstring captureName = L"(未使用)";
    if (!captureId.empty()) {
        for (const DeviceInfo& device : EnumerateDevices(DeviceFlow::Capture)) {
            if (device.id == captureId) {
                captureName = device.name;
                break;
            }
        }
    }

    if (!engine_.Start(renderId, captureId, micPassthrough_, renderName, captureName)) {
        ShowEngineError(L"无法启动音频引擎");
        return false;
    }

    renderDeviceId_ = renderId;
    captureDeviceId_ = captureId;

    engine_.SetMasterGain(masterGain_);
    engine_.SetMicGain(micGain_);

    // Decoded buffers must match the format the engine actually settled on.
    bank_.SetTargetFormat(engine_.format());

    engineEverStarted_ = true;
    StartStatusTimer();
    return true;
}

void App::RestartEngine() {
    if (!engine_.running()) {
        return;
    }
    engine_.Stop();
    StopStatusTimer();
    EnsureEngine();
}

void App::ShowEngineError(const std::wstring& fallback) {
    const EngineStatus status = engine_.Snapshot();
    const std::wstring text = status.lastError.empty() ? fallback : status.lastError;
    MessageBoxW(window_, text.c_str(), kAppName, MB_ICONERROR);
}

void App::StartStatusTimer() {
    if (statusTimerRunning_) {
        return;
    }
    // One tick per second is plenty: the audio thread keeps its own precise
    // timing, and refreshing the readout more often only burns CPU.
    SetTimer(window_, kIdStatusTimer, 1000, nullptr);
    statusTimerRunning_ = true;
}

void App::StopStatusTimer() {
    if (!statusTimerRunning_) {
        return;
    }
    KillTimer(window_, kIdStatusTimer);
    statusTimerRunning_ = false;
}

void App::OnTimer() {
    UpdateStatusText();

    // With no sound playing and no microphone to relay, drop the audio client so
    // an idle EchoPad costs literally nothing.
    if (engine_.running() && !micPassthrough_ && engine_.IdleForSeconds(3.0)) {
        engine_.Stop();
        StopStatusTimer();
        UpdateStatusText();
    }
}

void App::UpdateStatusText() {
    if (!statusText_) {
        return;
    }

    std::wstring text;
    const EngineStatus status = engine_.Snapshot();

    if (!engine_.running()) {
        text = engineEverStarted_ ? kStatusEngineStopped
                                  : L"尚未启动音频引擎（播放任意音效即会自动启动）";
    } else {
        wchar_t buffer[512];
        StringCchPrintfW(buffer, std::size(buffer),
                         L"输出: %s  |  %.1f kHz / %u 声道 / 缓冲 %.1f ms  |  CPU %.2f%%  |  "
                         L"播放中 %u  |  麦克风直通 %s  |  欠载 %u",
                         status.renderDeviceName.c_str(), status.sampleRate / 1000.0,
                         status.channels, status.bufferMs, status.renderLoadPercent,
                         status.activeVoices, status.micPassthroughActive ? L"开" : L"关",
                         status.underruns);
        text = buffer;
    }

    // Put the panic key where it can actually be discovered.
    if (stopHotkey_.bound()) {
        text = L"停止全部: " + FormatHotkey(stopHotkey_) + L"   |   " + text;
    }

    if (text != lastStatusText_) {
        lastStatusText_ = text;
        SetWindowTextW(statusText_, text.c_str());
    }

    const std::wstring tip =
        engine_.running() ? std::wstring(kAppName) + L"\n" + text
                          : std::wstring(kAppName) + L"\n" + kStatusEngineStopped;
    if (tip != lastTrayTip_) {
        lastTrayTip_ = tip;
        tray_.SetTooltip(tip);
    }
}

void App::ApplyHotkeys() {
    hotkeys_.UnbindAll();
    for (size_t index = 0; index < bank_.size(); ++index) {
        const Sound& sound = bank_.at(index);
        if (sound.hotkey.bound()) {
            hotkeys_.Bind(sound.id, sound.hotkey);
        }
    }
    // Registered last so a clash with a sound hotkey is reported on the sound,
    // which is the one the user just set.
    if (stopHotkey_.bound()) {
        hotkeys_.Bind(kStopAllHotkeyId, stopHotkey_);
    }
}

void App::OnAssignStopHotkey() {
    HotkeyBinding binding = stopHotkey_;
    const HotkeyCaptureResult result = CaptureHotkey(window_, binding, kTextCaptureStopTitle);
    if (result == HotkeyCaptureResult::Cancelled) {
        return;
    }

    hotkeys_.Unbind(kStopAllHotkeyId);
    if (result == HotkeyCaptureResult::Cleared) {
        stopHotkey_ = HotkeyBinding{};
    } else {
        if (!hotkeys_.Bind(kStopAllHotkeyId, binding)) {
            MessageBoxW(window_, kErrorHotkeyTaken, kAppName, MB_ICONWARNING);
            hotkeys_.Bind(kStopAllHotkeyId, stopHotkey_);  // restore
            return;
        }
        stopHotkey_ = binding;
    }

    RefreshPlayMenu();
    SaveSettings();
    UpdateStatusText();
}

void App::OnDecodeFinished() {
    bank_.ApplyCompleted();
    RefreshAllRows();
    UpdateStatusText();
}

// ----------------------------------------------------------------------- help

void App::OnShowUsage() {
    const std::wstring text =
        std::wstring(L"EchoPad 音效助手 ") + kAppVersion + L"\n\n"
        L"【一、安装虚拟声卡】\n"
        L"下载并安装 VB-CABLE（免费）：https://vb-audio.com/Cable/\n"
        L"安装后重启一次电脑，系统里会出现两个新设备：\n"
        L"  · CABLE Input  —— 播放设备，EchoPad 把音效送到这里\n"
        L"  · CABLE Output —— 录音设备，游戏/语音软件把这里当作麦克风\n\n"
        L"【二、EchoPad 设置】\n"
        L"1. 把上方“输出设备”选成 “CABLE Input (VB-Audio Virtual Cable)”\n"
        L"2. 勾选“麦克风直通”，这样你的真麦克风会和音效混在一起送出去\n"
        L"3. 点“添加音频”把音效文件加进来，双击任意一条即可试听\n"
        L"4. 在列表里右键 → “设置快捷键…”，按下想绑定的按键\n"
        L"   （热键是全局的，游戏全屏时也能触发）\n"
        L"5. “播放”菜单 → “设置‘停止全部’热键…”，给「一键停掉所有音效」\n"
        L"   绑一个全局键。默认是 Ctrl+Alt+S，播放长音频时特别有用。\n\n"
        L"【三、游戏 / 语音软件设置】\n"
        L"在游戏、Discord、YY、QQ 语音里，把「麦克风 / 输入设备」\n"
        L"选为 “CABLE Output (VB-Audio Virtual Cable)”。\n"
        L"如果还想自己听到声音，在 Windows 声音设置里把 CABLE Output\n"
        L"的“侦听此设备”打开，指向你自己的耳机即可。\n\n"
        L"【关于性能】\n"
        L"EchoPad 采用 WASAPI 事件驱动 + 预解码到内存的方式，播放时\n"
        L"CPU 占用通常在 0.1% 上下；关掉“麦克风直通”并停止播放 3 秒后，\n"
        L"音频引擎会自动关闭，此时后台占用为 0。\n\n"
        L"支持的格式：" + AudioFileExtensionList() + L"（由系统解码器提供）\n"
        L"配置文件目录：" + ConfigDirectory();
    MessageBoxW(window_, text.c_str(), L"使用说明", MB_ICONINFORMATION | MB_OK);
}

void App::OnAbout() {
    const EngineStatus status = engine_.Snapshot();
    const std::wstring text =
        std::wstring(kAppName) + L" " + kAppVersion + L"\n\n"
        L"一个用 C++ / Win32 手写的轻量音效助手。\n"
        L"无第三方运行时依赖，单文件 exe 即可运行。\n\n"
        L"音频后端：WASAPI 共享模式（事件驱动）\n"
        L"解码后端：Media Foundation\n\n"
        L"配置文件：" + ConfigFilePath() + L"\n"
        L"日志文件：" + LogFilePath() + L"\n\n"
        L"当前输出格式：";
    std::wstring suffix;
    if (status.sampleRate) {
        wchar_t buffer[128];
        StringCchPrintfW(buffer, std::size(buffer), L"%u Hz / %u 声道", status.sampleRate,
                         status.channels);
        suffix = buffer;
    } else {
        suffix = L"未启动";
    }
    MessageBoxW(window_, (text + suffix).c_str(), L"关于", MB_ICONINFORMATION | MB_OK);
}

void App::OnOpenConfigDirectory() {
    const std::wstring folder = ConfigDirectory();
    // Make sure the folder exists first: handing ShellExecute a path that is not
    // there produces a bare "file not found" box with nothing to act on.
    CreateDirectoryW(folder.c_str(), nullptr);
    RunExplorer(L"\"" + folder + L"\"", folder);
}

bool App::RunExplorer(const std::wstring& parameters, const std::wstring& reportedPath) {
    // explorer.exe is launched directly rather than asking the shell to resolve
    // the "open" verb for a folder. On machines where the folder association is
    // damaged that verb pops an "open with" dialog instead of the folder, and
    // the "explore" verb reports SE_ERR_NOASSOC outright.
    const HINSTANCE result =
        ShellExecuteW(window_, L"open", L"explorer.exe", parameters.c_str(), nullptr,
                      SW_SHOWNORMAL);
    const INT_PTR code = reinterpret_cast<INT_PTR>(result);
    if (code > 32) {
        return true;
    }
    LogLine(L"ShellExecute(explorer.exe %s) failed with code %lld", parameters.c_str(),
            static_cast<long long>(code));
    MessageBoxW(window_, (std::wstring(kErrorOpenFolder) + reportedPath).c_str(), kAppName,
                MB_ICONWARNING);
    return false;
}

void App::OnOpenLog() {
    const std::wstring path = LogFilePath();
    if (!FileExists(path)) {
        LogLine(L"log opened from the UI");
    }
    // Quoted: notepad takes the path as a command-line argument, so a path with
    // spaces would otherwise be split.
    ShellExecuteW(window_, L"open", L"notepad.exe", (L"\"" + path + L"\"").c_str(), nullptr,
                  SW_SHOWNORMAL);
}

// ---------------------------------------------------------------- window proc

LRESULT CALLBACK App::WindowProcThunk(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    App* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<App*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) {
            self->window_ = window;
        }
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    if (self) {
        return self->WindowProc(window, message, wparam, lparam);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT App::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = 720;
            info->ptMinTrackSize.y = 460;
            return 0;
        }
        case WM_SIZE:
            LayoutControls();
            return 0;

        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const int notification = HIWORD(wparam);
            switch (id) {
                case kIdAddButton:
                    if (notification == BN_CLICKED) OnAddFiles();
                    return 0;
                case kIdRemoveButton:
                    if (notification == BN_CLICKED) OnRemoveSelected();
                    return 0;
                case kIdStopButton:
                    if (notification == BN_CLICKED) OnStopAll();
                    return 0;
                case kIdMicCheck:
                    if (notification == BN_CLICKED) OnMicPassthroughToggled();
                    return 0;
                case kIdLoopCheck:
                    if (notification == BN_CLICKED) OnToggleLoop();
                    return 0;
                case kIdDeviceCombo:
                    if (notification == CBN_SELCHANGE) OnOutputDeviceChanged();
                    return 0;
                case kMenuAddFiles:
                    OnAddFiles();
                    return 0;
                case kMenuRescan:
                    bank_.DecodeAll();
                    RefreshAllRows();
                    return 0;
                case kMenuOpenConfig:
                    OnOpenConfigDirectory();
                    return 0;
                case kMenuExit:
                    shuttingDown_ = true;
                    DestroyWindow(window_);
                    return 0;
                case kMenuPlaySelected:
                    OnPlaySelected();
                    return 0;
                case kMenuStopAll:
                    OnStopAll();
                    return 0;
                case kMenuAssignStopHotkey:
                    OnAssignStopHotkey();
                    return 0;
                case kMenuUsage:
                    OnShowUsage();
                    return 0;
                case kMenuLog:
                    OnOpenLog();
                    return 0;
                case kMenuAbout:
                    OnAbout();
                    return 0;
                default:
                    break;
            }
            break;
        }

        case WM_HSCROLL: {
            if (reinterpret_cast<HWND>(lparam) == masterSlider_) {
                masterGain_ = static_cast<float>(SendMessageW(masterSlider_, TBM_GETPOS, 0, 0)) /
                              100.0f;
                engine_.SetMasterGain(masterGain_);
                SaveSettings();
                return 0;
            }
            if (reinterpret_cast<HWND>(lparam) == micSlider_) {
                micGain_ = static_cast<float>(SendMessageW(micSlider_, TBM_GETPOS, 0, 0)) / 100.0f;
                engine_.SetMicGain(micGain_);
                SaveSettings();
                return 0;
            }
            break;
        }

        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lparam);
            if (header->idFrom == kIdSoundList) {
                switch (header->code) {
                    case NM_DBLCLK: {
                        const int selected = SelectedIndex();
                        if (selected >= 0) {
                            OnPlayIndex(static_cast<size_t>(selected));
                        }
                        return 0;
                    }
                    case NM_RCLICK: {
                        POINT cursor{};
                        GetCursorPos(&cursor);
                        OnShowContextMenu(cursor);
                        return 0;
                    }
                    case LVN_ITEMCHANGED: {
                        auto* changed = reinterpret_cast<NMLISTVIEW*>(lparam);
                        if ((changed->uNewState & LVIS_SELECTED) &&
                            !(changed->uOldState & LVIS_SELECTED) && changed->iItem >= 0) {
                            const size_t index = static_cast<size_t>(changed->iItem);
                            if (index < bank_.size()) {
                                SendMessageW(loopCheck_, BM_SETCHECK,
                                             bank_.at(index).loop ? BST_CHECKED : BST_UNCHECKED, 0);
                            }
                        }
                        return 0;
                    }
                    case LVN_KEYDOWN: {
                        auto* key = reinterpret_cast<NMLVKEYDOWN*>(lparam);
                        if (key->wVKey == VK_DELETE) {
                            OnRemoveSelected();
                        } else if (key->wVKey == VK_SPACE) {
                            OnPlaySelected();
                        }
                        return 0;
                    }
                    default:
                        break;
                }
            }
            break;
        }

        case WM_HOTKEY: {
            if (wparam == kStopAllHotkeyId) {
                LogLine(L"hotkey: stop all");
                OnStopAll();
                return 0;
            }
            const int index = bank_.IndexOfId(static_cast<uint32_t>(wparam));
            if (index >= 0) {
                LogLine(L"hotkey %u -> play \"%s\"", static_cast<unsigned>(wparam),
                        bank_.at(static_cast<size_t>(index)).name.c_str());
                OnPlayIndex(static_cast<size_t>(index));
            }
            return 0;
        }

        case WM_TIMER:
            if (wparam == kIdStatusTimer) {
                OnTimer();
                return 0;
            }
            break;

        case kMsgDecodeDone:
            OnDecodeFinished();
            return 0;

        case TrayIcon::kCallbackMessage:
            OnTrayMessage(lparam);
            return 0;

        case WM_CLOSE:
            if (shuttingDown_) {
                DestroyWindow(window);
            } else {
                ShowWindow(window, SW_HIDE);
                tray_.ShowBalloon(kAppName, L"EchoPad 仍在后台运行，热键继续有效。\n双击托盘图标可以重新打开窗口。");
            }
            return 0;

        case WM_ENDSESSION:
            shuttingDown_ = true;
            DestroyWindow(window);
            return 0;

        case WM_DESTROY: {
            StopStatusTimer();
            hotkeys_.Shutdown();
            engine_.Stop();
            SaveSettings();
            bank_.StopLoader();
            tray_.Destroy();
            MediaFoundationShutdown();
            PostQuitMessage(0);
            return 0;
        }

        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace echopad
