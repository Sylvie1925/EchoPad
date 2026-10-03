// EchoPad - every user-visible string in one place.
//
// Labels are prefixed "kLabel" so they never collide with the command IDs of
// the same name declared in app.h.
#pragma once

#include "common.h"

namespace echopad {

inline constexpr wchar_t kAppName[] = L"EchoPad 音效助手";
inline constexpr wchar_t kAppVersion[] = L"1.0.0";

inline constexpr wchar_t kTextCaptureTitle[] = L"设置快捷键";
inline constexpr wchar_t kTextCaptureStopTitle[] = L"设置“停止全部”热键";
inline constexpr wchar_t kTextCaptureHint[] =
    L"请按下要绑定的按键\nEsc 取消，Backspace 或 Delete 清除绑定";

// Controls
inline constexpr wchar_t kTextAdd[] = L"添加音频";
inline constexpr wchar_t kTextRemove[] = L"移除";
inline constexpr wchar_t kTextStopAll[] = L"停止全部";
inline constexpr wchar_t kTextOutputDevice[] = L"输出设备:";
inline constexpr wchar_t kTextMicPassthrough[] = L"麦克风直通";
inline constexpr wchar_t kTextLoop[] = L"循环";
inline constexpr wchar_t kTextMasterVolume[] = L"主音量";
inline constexpr wchar_t kTextMicVolume[] = L"麦克风";

// List columns
inline constexpr wchar_t kColumnName[] = L"名称";
inline constexpr wchar_t kColumnHotkey[] = L"快捷键";
inline constexpr wchar_t kColumnDuration[] = L"时长";
inline constexpr wchar_t kColumnVolume[] = L"音量";
inline constexpr wchar_t kColumnLoop[] = L"循环";

// Menu labels
inline constexpr wchar_t kLabelMenuFile[] = L"文件(&F)";
inline constexpr wchar_t kLabelMenuAddFiles[] = L"添加音频文件…\tCtrl+O";
inline constexpr wchar_t kLabelMenuRescan[] = L"重新加载全部音效";
inline constexpr wchar_t kLabelMenuOpenConfig[] = L"打开配置目录";
inline constexpr wchar_t kLabelMenuExit[] = L"退出(&X)";
inline constexpr wchar_t kLabelMenuPlay[] = L"播放(&P)";
inline constexpr wchar_t kLabelMenuPlaySelected[] = L"播放选中项\tEnter";
// The key shown after this label is filled in from the user's actual binding.
inline constexpr wchar_t kLabelMenuStopAll[] = L"停止全部";
inline constexpr wchar_t kLabelMenuAssignStopHotkey[] = L"设置“停止全部”热键…";
inline constexpr wchar_t kLabelMenuHelp[] = L"帮助(&H)";
inline constexpr wchar_t kLabelMenuUsage[] = L"使用说明与虚拟声卡配置";
inline constexpr wchar_t kLabelMenuLog[] = L"打开日志文件";
inline constexpr wchar_t kLabelMenuAbout[] = L"关于 EchoPad";

// Tray menu labels
inline constexpr wchar_t kLabelTrayShow[] = L"显示主窗口";
inline constexpr wchar_t kLabelTrayStopAll[] = L"停止全部";
inline constexpr wchar_t kLabelTrayPassthrough[] = L"麦克风直通";
inline constexpr wchar_t kLabelTrayExit[] = L"退出";

// Context menu labels
inline constexpr wchar_t kLabelContextPlay[] = L"播放";
inline constexpr wchar_t kLabelContextSetHotkey[] = L"设置快捷键…";
inline constexpr wchar_t kLabelContextClearHotkey[] = L"清除快捷键";
inline constexpr wchar_t kLabelContextLoopOn[] = L"设为循环";
inline constexpr wchar_t kLabelContextLoopOff[] = L"取消循环";
inline constexpr wchar_t kLabelContextVolumeUp[] = L"音量 +10%";
inline constexpr wchar_t kLabelContextVolumeDown[] = L"音量 -10%";
inline constexpr wchar_t kLabelContextReveal[] = L"在资源管理器中显示";
inline constexpr wchar_t kLabelContextRemove[] = L"从列表移除";

// Status
inline constexpr wchar_t kStatusIdle[] = L"就绪";
inline constexpr wchar_t kStatusEngineStopped[] = L"音频引擎已休眠（无播放时零占用）";

// Dialogs
inline constexpr wchar_t kDialogAddFilter[] = L"音频文件";
inline constexpr wchar_t kDialogAllFiles[] = L"所有文件";
inline constexpr wchar_t kDialogAddTitle[] = L"添加音频文件";

inline constexpr wchar_t kErrorNoDevice[] =
    L"没有找到可用的输出设备。请确认已安装 VB-CABLE 虚拟声卡并至少有一个播放设备。";
inline constexpr wchar_t kErrorHotkeyTaken[] = L"该快捷键已被其他程序占用，请换一个组合。";
inline constexpr wchar_t kErrorOpenFolder[] =
    L"无法打开资源管理器。\n\n路径：";
inline constexpr wchar_t kHintNoVirtualCable[] =
    L"提示：当前输出设备看起来不是虚拟声卡。若要让游戏/语音软件听到音效，"
    L"请把输出设备设为 “CABLE Input”，并在游戏里把麦克风设为 “CABLE Output”。";

}  // namespace echopad
