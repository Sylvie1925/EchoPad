# EchoPad 音效助手

一个用 **C++20 / 纯 Win32** 手写的轻量级音效板（类 Soundpad）。把自定义音频文件通过虚拟声卡
（VB-CABLE / Voicemeeter）送进麦克风通道，让游戏和语音软件里的队友听到你的音效。

没有任何第三方运行时依赖：**单个 exe，455 KB，不装 DLL，不用 .NET，不用 Qt**。

---

## 特性

| | |
|---|---|
| 支持格式 | WAV / MP3 / M4A / AAC / WMA / FLAC / MP4 / AIFF（走系统 Media Foundation 解码器） |
| 播放方式 | WASAPI 共享模式 + 事件驱动，不占用独占音频通道 |
| 麦克风直通 | 真麦克风与音效实时混音后一起送进虚拟声卡，**不需要**「侦听此设备」这种土办法 |
| 全局热键 | 游戏全屏、窗口失焦时照样触发；另有一个独立的**一键停止**全局键（默认 `Ctrl+Alt+S`） |
| 多音效混音 | 最多 24 路同时播放，同一个音效重放会重新开始而不是叠一层 |
| 托盘常驻 | 关窗口不退出，继续后台待命 |
| 预解码 | 音频在后台线程一次性解码成 32 位浮点常驻内存，播放时零解码、零分配 |
| 自动休眠 | 关掉麦克风直通后，停止播放 3 秒自动关闭音频客户端，此时后台占用为 **0** |

## 实测性能（本机 VB-CABLE，48 kHz / 2 声道 / 22 ms 缓冲）

| 工况 | CPU 占用 |
|---|---|
| 启动 + 后台解码 4 个音效（WAV/MP3/M4A） | 合计 0.094 秒 |
| 空闲，麦克风直通开启 | **低于 0.08%（20 秒内计时器无增长）** |
| 麦克风直通关，引擎休眠 | **0%** |
| 峰值内存 | 约 37 MB（含 4 个已解码音效） |

对比一下：这类程序常见的做法是每个音效开一个解码线程、或者用 BASS/FMOD 这类库，空闲时也要
1~3% CPU。EchoPad 的做法是**一个渲染线程 + 一个采集线程，事件驱动阻塞等待**，没有任何轮询。

---

## 快速开始

### 1. 编译

**方式 A：直接用附带的工具链（推荐，已在本机验证）**

```bat
cd EchoPad
build.bat
```

脚本会自动找到 `..\tools\w64devkit\w64devkit\bin\g++.exe`（本机已装好），
也可以把 w64devkit 放到 `EchoPad\tools\` 下，或者把 `g++` 加进 PATH。
产物：`build\EchoPad.exe`

**方式 B：CMake（Visual Studio / CLion）**

```bat
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --config Release
```

**方式 C：MSYS2 / 其它 MinGW**

```bash
g++ -std=c++20 -O2 -mwindows -Isrc -Ires src/**/*.cpp -o EchoPad.exe \
    -lole32 -loleaut32 -luuid -lmmdevapi -lavrt \
    -lmfplat -lmfreadwrite -lmfuuid -lmf -lshlwapi \
    -lcomctl32 -lcomdlg32 -lshell32 -luser32 -lgdi32 -ladvapi32
```

### 2. 运行

双击 `build\EchoPad.exe`。首次启动会自动把输出设备选成系统默认设备；
如果检测到名字里带 “cable / vb-audio / voicemeeter” 的设备，会优先选它。

---

## 配置虚拟声卡（如果你还没装）

EchoPad 本身不创建虚拟声卡，它把音频**播放到**虚拟声卡里。推荐 VB-CABLE（免费）：

1. 到 <https://vb-audio.com/Cable/> 下载 VBCABLE_Driver_Pack，右键
   `VBCABLE_Setup_x64.exe` → **以管理员身份运行** → Install Driver
2. **重启电脑**（必须，否则设备不会出现）
3. 重启后系统里会多出两个设备：
   - `CABLE Input (VB-Audio Virtual Cable)` —— 播放设备
   - `CABLE Output (VB-Audio Virtual Cable)` —— 录音设备

### EchoPad 这边怎么设

1. 上方「输出设备」选 **CABLE Input (VB-Audio Virtual Cable)**
2. 勾上「麦克风直通」—— 真麦克风的输入会和音效混在一起送出去
3. 「添加音频」把音效加进来，双击列表项试听
4. 列表里右键 → 「设置快捷键…」→ 按下想绑的键（比如 `Ctrl+1`）

### 游戏 / 语音软件那边怎么设

在游戏、Discord、YY、QQ 语音里，把 **麦克风 / 输入设备** 选成
`CABLE Output (VB-Audio Virtual Cable)`。

此时：
- 队友能听到你的声音 + 音效（因为 EchoPad 把两者混好了一起送进去）
- 只有你自己听不到音效 —— 如果你也想听，在 Windows「声音设置」里找到
  `CABLE Output` → 属性 → 「侦听」→ 勾选「侦听此设备」→ 指向你的耳机

> 注意：不要把「侦听此设备」指向 `CABLE Input`，那会形成回环啸叫。

---

## 使用

| 操作 | 说明 |
|---|---|
| 双击列表项 / 选中后按 `Enter` | 播放 |
| **`Ctrl+Alt+S`** | **一键停止全部播放**（全局键，游戏全屏时也有效） |
| `Ctrl+O` | 添加音频文件（可多选） |
| `Delete` | 从列表移除选中项 |
| 空格 | 播放选中项 |
| 右键列表项 | 播放 / 设置快捷键 / 清除快捷键 / 设置循环 / 音量 ±10% / 在资源管理器中显示 / 移除 |
| 「播放」菜单 → 设置“停止全部”热键 | 把一键停止换成你习惯的键；当前生效的键会显示在菜单和状态栏上 |
| 双击托盘图标 | 重新打开主窗口 |
| 右键托盘图标 | 显示主窗口 / 停止全部 / 麦克风直通开关 / 退出 |
| 关窗口 | 只是收进托盘，热键继续有效；要真正退出请用托盘菜单 |

音量有两档：**主音量**是总输出增益，**麦克风**只影响你说话的音量。
单个音效的音量用右键菜单调。三者都超过 100% 也是允许的（最高 200% / 单个音效 400%）。

---

## 文件位置

EchoPad 是绿色的：配置文件优先放在 **exe 所在目录**；如果那目录不可写
（比如装在 `Program Files` 下），才退回到 `%APPDATA%\EchoPad\`。

```
EchoPad.ini    设置（输出设备、麦克风直通、音量、上次打开的目录）
sounds.tsv     音效列表（名称 / 路径 / 快捷键 / 音量 / 循环）
echopad.log    运行日志，出问题时先看它
```

三个都是纯 UTF-8 文本，可以直接用记事本改。`sounds.tsv` 每行一个音效，
用 Tab 分隔，前两列是名称和路径，可以手工批量编辑。

---

## 项目结构

```
EchoPad/
├── build.bat                 MinGW 一键构建
├── CMakeLists.txt            MSVC / CLion 构建
├── res/
│   ├── echopad.rc            图标 + manifest
│   ├── echopad.ico           应用图标（make_icon.py 生成）
│   ├── echopad.manifest      启用 comctl32 v6 与 PerMonitorV2 DPI
│   └── resource.h
└── src/
    ├── common.h              公共定义、ComPtr、内部音频格式
    ├── main.cpp              WinMain、单实例
    ├── app.h / app.cpp       主窗口、控件、托盘、配置、命令分发
    ├── core/
    │   ├── util.h/.cpp       路径、UTF-8 转换、日志、INI
    │   ├── ring_buffer.h/.cpp 无锁 SPSC 队列与采样环
    │   └── guids.cpp         全程序 GUID 的唯一实例化点（MinGW 必需）
    ├── audio/
    │   ├── format.h/.cpp     WAVEFORMATEX 识别与采样编码转换
    │   ├── devices.h/.cpp    端点枚举、虚拟声卡识别
    │   └── engine.h/.cpp     WASAPI 双工引擎（核心）
    ├── media/
    │   ├── decoder.h/.cpp    Media Foundation 解码
    │   └── sound.h/.cpp      音效目录、持久化、后台解码线程
    ├── hotkey/hotkeys.h/.cpp 全局热键
    └── ui/
        ├── strings.h         全部界面文案
        ├── tray.h/.cpp       托盘图标
        └── hotkey_capture.h/.cpp  按键捕获弹窗
```

---

## 实现要点

**音频路径**

```
音频文件 ──MF解码──▶ float32 / 48kHz / 2ch ──┐
                                            ├─▶ 混音 ─▶ 主音量 ─▶ 削波 ─▶ CABLE Input
真实麦克风 ──WASAPI采集──▶ float32 ──────────┘
```

- 渲染设备用 `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM` 请求 48 kHz / 立体声 / float32，
  由 Windows 音频引擎负责和硬件格式之间的转换。这样**解码出来的缓冲区与设备无关**，
  换输出设备不需要重新解码，我们的回调里也不需要任何重采样代码。
- 万一某台设备不接受 `AUTOCONVERTPCM`，会自动退回设备原生混合格式，
  此时解码目标格式跟着变，引擎会通知音效库后台重新解码一次。
- 采集侧同样用 `AUTOCONVERTPCM`，所以麦克风是 44.1 kHz 还是单声道都能自动对上。

**实时性**

- 渲染线程和采集线程都用 `WaitForSingleObject` 等 WASAPI 事件，**没有轮询、没有 `timeBeginPeriod`**。
- 两种线程都调用 `AvSetMmThreadCharacteristics("Pro Audio")` 申请 MMCSS 优先级。
- 播放中的音效缓冲用 `std::shared_ptr` 引用；音频线程结束一路音效时把引用丢进一个
  SPSC 队列，由界面线程负责释放，因此回调里**不会有释放大块内存的抖动**。
- 麦克风采样经过一个无锁环形缓冲交给渲染线程；当采集与渲染时钟长期漂移导致积压超过
  100 ms 时，会丢弃最旧的数据重新对齐，避免延迟越拖越大。

**关于削波**

麦克风和音效相加后若超过 ±1.0 会硬削波。这是有意为之：行为可预期，且没有额外开销。
如果发现爆音，把主音量或麦克风音量调低一点即可。

---

## 常见问题

**Q：游戏里听不到音效**
1. EchoPad 的「输出设备」是不是 `CABLE Input`？
2. 游戏/语音软件里的麦克风是不是 `CABLE Output`？
3. 「麦克风直通」勾上了吗？（不勾的话队友只能听到音效，听不到你说话）
4. 看 `echopad.log` 有没有 `engine started` 和 `decoded ...`。

**Q：热键没反应**
热键被别的程序独占了。右键那个音效 → 重新设一个别的组合。日志里会记
`RegisterHotKey failed`。

**Q：音效有爆音 / 断断续续**
把主音量降到 80% 以内试试；另外确认 `CABLE Input` 的采样率设置别太低。
日志里的 `underruns` 计数如果一直涨，说明系统音频负载过高。

**Q：能不能不用 VB-CABLE？**
可以，选任何你想要的输出设备。但要让游戏听到，那个设备必须是游戏能当麦克风读到的
虚拟设备，所以实际上还是需要虚拟声卡。

**Q：麦克风直通关掉会怎样？**
队友就只能听到音效，听不到你本人说话。好处是：停止播放 3 秒后音频引擎会完全关闭，
后台占用变成 0。

---

## 已知限制

- 仅支持 Windows 7 及以上（用到了 WASAPI 的 `AUTOCONVERTPCM`）。
- 能解码的格式取决于系统装了哪些 Media Foundation 解码器。Windows 10/11 自带
  WAV/MP3/AAC/M4A/WMA/FLAC；OGG/Opus 需要额外装解码器，暂不支持。
- 单个音效上限 10 分钟（避免误点一个电影文件吃掉几 GB 内存）。
- 目前只能给已有音效绑定热键，还不支持把多个音效编成「播放列表」连播。
