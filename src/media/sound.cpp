#include "sound.h"

#include <strsafe.h>

#include <condition_variable>

#include "core/util.h"
#include "media/decoder.h"

namespace echopad {
namespace {

std::wstring KeyName(uint32_t virtualKey) {
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        return std::wstring(1, static_cast<wchar_t>(virtualKey));
    }
    if (virtualKey >= '0' && virtualKey <= '9') {
        return std::wstring(1, static_cast<wchar_t>(virtualKey));
    }
    if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        wchar_t buffer[8];
        StringCchPrintfW(buffer, std::size(buffer), L"F%u", virtualKey - VK_F1 + 1);
        return buffer;
    }
    if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9) {
        wchar_t buffer[16];
        StringCchPrintfW(buffer, std::size(buffer), L"小键盘%u", virtualKey - VK_NUMPAD0);
        return buffer;
    }
    switch (virtualKey) {
        case VK_SPACE: return L"空格";
        case VK_RETURN: return L"回车";
        case VK_TAB: return L"Tab";
        case VK_BACK: return L"退格";
        case VK_ESCAPE: return L"Esc";
        case VK_DELETE: return L"Delete";
        case VK_INSERT: return L"Insert";
        case VK_HOME: return L"Home";
        case VK_END: return L"End";
        case VK_PRIOR: return L"PageUp";
        case VK_NEXT: return L"PageDown";
        case VK_UP: return L"↑";
        case VK_DOWN: return L"↓";
        case VK_LEFT: return L"←";
        case VK_RIGHT: return L"→";
        case VK_OEM_1: return L";";
        case VK_OEM_2: return L"/";
        case VK_OEM_3: return L"`";
        case VK_OEM_4: return L"[";
        case VK_OEM_5: return L"\\";
        case VK_OEM_6: return L"]";
        case VK_OEM_7: return L"'";
        case VK_OEM_PLUS: return L"=";
        case VK_OEM_MINUS: return L"-";
        case VK_OEM_COMMA: return L",";
        case VK_OEM_PERIOD: return L".";
        default: break;
    }

    const UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
    if (scanCode) {
        wchar_t buffer[64] = L"";
        if (GetKeyNameTextW(static_cast<LONG>(scanCode) << 16, buffer,
                            static_cast<int>(std::size(buffer))) > 0) {
            return buffer;
        }
    }
    wchar_t fallback[32];
    StringCchPrintfW(fallback, std::size(fallback), L"键码 0x%02X", virtualKey);
    return fallback;
}

std::wstring Sanitize(std::wstring text) {
    for (wchar_t& ch : text) {
        if (ch == L'\t' || ch == L'\r' || ch == L'\n') {
            ch = L' ';
        }
    }
    return text;
}

}  // namespace

std::wstring FormatHotkey(const HotkeyBinding& binding) {
    if (!binding.bound()) {
        return L"";
    }
    std::wstring text;
    if (binding.modifiers & MOD_CONTROL) text += L"Ctrl+";
    if (binding.modifiers & MOD_ALT) text += L"Alt+";
    if (binding.modifiers & MOD_SHIFT) text += L"Shift+";
    if (binding.modifiers & MOD_WIN) text += L"Win+";
    text += KeyName(binding.virtualKey);
    return text;
}

SoundBank::~SoundBank() {
    StopLoader();
}

int SoundBank::IndexOfId(uint32_t id) const {
    for (size_t index = 0; index < sounds_.size(); ++index) {
        if (sounds_[index].id == id) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

size_t SoundBank::AddPending(const std::wstring& path, const std::wstring& name) {
    Sound sound;
    sound.id = nextId_++;
    sound.path = path;
    sound.name = name.empty() ? FileStemOf(path) : name;
    sound.loading = true;
    sounds_.push_back(std::move(sound));
    return sounds_.size() - 1;
}

void SoundBank::Remove(size_t index) {
    if (index < sounds_.size()) {
        sounds_.erase(sounds_.begin() + static_cast<ptrdiff_t>(index));
    }
}

void SoundBank::MoveUp(size_t index) {
    if (index > 0 && index < sounds_.size()) {
        std::swap(sounds_[index], sounds_[index - 1]);
    }
}

void SoundBank::MoveDown(size_t index) {
    if (index + 1 < sounds_.size()) {
        std::swap(sounds_[index], sounds_[index + 1]);
    }
}

void SoundBank::Clear() {
    sounds_.clear();
    nextId_ = 1;
}

// ---------------------------------------------------------------- persistence

bool SoundBank::LoadList(const std::wstring& path) {
    std::wstring text;
    if (!ReadTextFile(path, text)) {
        return false;
    }

    sounds_.clear();
    nextId_ = 1;

    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = text.size();
        }
        const std::wstring line = Trim(text.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line[0] == L'#') {
            continue;
        }

        const std::vector<std::wstring> fields = Split(line, L'\t');
        if (fields.size() < 2) {
            continue;
        }

        Sound sound;
        sound.id = nextId_++;
        sound.name = fields[0];
        sound.path = fields[1];
        if (fields.size() > 2) {
            sound.hotkey.modifiers = static_cast<uint32_t>(wcstoul(fields[2].c_str(), nullptr, 10));
        }
        if (fields.size() > 3) {
            sound.hotkey.virtualKey = static_cast<uint32_t>(wcstoul(fields[3].c_str(), nullptr, 10));
        }
        if (fields.size() > 4) {
            sound.gain = static_cast<float>(wcstod(fields[4].c_str(), nullptr));
            if (sound.gain <= 0.0f || sound.gain > 4.0f) {
                sound.gain = 1.0f;
            }
        }
        if (fields.size() > 5) {
            sound.loop = fields[5] == L"1";
        }
        sound.loading = true;

        if (sound.name.empty()) {
            sound.name = FileStemOf(sound.path);
        }
        sounds_.push_back(std::move(sound));
    }
    return true;
}

bool SoundBank::SaveList(const std::wstring& path) const {
    std::wstring text =
        L"# EchoPad 音效列表\r\n"
        L"# 格式: 名称<TAB>文件路径<TAB>修饰键<TAB>虚拟键码<TAB>音量<TAB>循环\r\n";
    for (const Sound& sound : sounds_) {
        wchar_t numbers[128];
        StringCchPrintfW(numbers, std::size(numbers), L"\t%u\t%u\t%.3f\t%d", sound.hotkey.modifiers,
                         sound.hotkey.virtualKey, static_cast<double>(sound.gain),
                         sound.loop ? 1 : 0);
        if (!sound.error.empty()) {
            text += L"# 无法加载: ";
            text += Sanitize(sound.error);
            text += L"\r\n";
        }
        text += Sanitize(sound.name);
        text += L'\t';
        text += Sanitize(sound.path);
        text += numbers;
        text += L"\r\n";
    }
    return WriteTextFile(path, text);
}

// -------------------------------------------------------------- background IO

void SoundBank::StartLoader(const AudioFormat& format, HWND notifyWindow, UINT notifyMessage) {
    StopLoader();
    {
        std::lock_guard<std::mutex> guard(formatMutex_);
        targetFormat_ = format;
    }
    notifyWindow_ = notifyWindow;
    notifyMessage_ = notifyMessage;
    loaderStop_.store(false, std::memory_order_release);
    loader_ = std::thread([this] { LoaderLoop(); });
}

void SoundBank::StopLoader() {
    if (!loader_.joinable()) {
        return;
    }
    loaderStop_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> guard(requestMutex_);
        requests_.clear();
    }
    // The loop polls with a short wait, so no explicit wake-up is needed.
    loader_.join();
    notifyWindow_ = nullptr;
}

AudioFormat SoundBank::targetFormat() const {
    std::lock_guard<std::mutex> guard(formatMutex_);
    return targetFormat_;
}

void SoundBank::SetTargetFormat(const AudioFormat& format) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> guard(formatMutex_);
        if (targetFormat_ != format) {
            targetFormat_ = format;
            changed = true;
        }
    }
    if (changed) {
        RetargetAll();
    }
}

void SoundBank::RetargetAll() {
    // The render device changed its native format, so every cached buffer is at
    // the wrong sample rate. Drop them and decode again in the background.
    for (Sound& sound : sounds_) {
        sound.data.reset();
        sound.durationMs = 0;
        sound.error.clear();
        sound.failed = false;
        sound.loading = true;
    }
    DecodeAll();
}

void SoundBank::RequestDecode(size_t index) {
    if (index >= sounds_.size()) {
        return;
    }
    Sound& sound = sounds_[index];
    sound.loading = true;
    sound.failed = false;
    sound.error.clear();

    const LoadRequest request{sound.id, sound.path};
    {
        std::lock_guard<std::mutex> guard(requestMutex_);
        requests_.push_back(request);
    }
}

void SoundBank::DecodeAll() {
    for (size_t index = 0; index < sounds_.size(); ++index) {
        if (!sounds_[index].data) {
            RequestDecode(index);
        }
    }
}

void SoundBank::LoaderLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    for (;;) {
        if (loaderStop_.load(std::memory_order_acquire)) {
            break;
        }

        LoadRequest request;
        bool haveRequest = false;
        {
            std::lock_guard<std::mutex> guard(requestMutex_);
            if (!requests_.empty()) {
                request = requests_.front();
                requests_.pop_front();
                haveRequest = true;
            }
        }

        if (!haveRequest) {
            Sleep(40);
            continue;
        }

        AudioFormat format;
        {
            std::lock_guard<std::mutex> guard(formatMutex_);
            format = targetFormat_;
        }

        const DecodeResult decoded = DecodeAudioFile(request.path, format);

        LoadResult result;
        result.id = request.id;
        if (decoded.ok()) {
            result.data = decoded.sound;
        } else {
            result.error = decoded.error;
            LogLine(L"decode failed for %s: %s", request.path.c_str(), decoded.error.c_str());
        }

        {
            std::lock_guard<std::mutex> guard(resultMutex_);
            results_.push_back(std::move(result));
        }
        if (notifyWindow_) {
            PostMessageW(notifyWindow_, notifyMessage_, 0, 0);
        }
    }

    CoUninitialize();
}

void SoundBank::ApplyCompleted() {
    std::vector<LoadResult> completed;
    {
        std::lock_guard<std::mutex> guard(resultMutex_);
        completed.swap(results_);
    }

    for (LoadResult& result : completed) {
        const int index = IndexOfId(result.id);
        if (index < 0) {
            continue;  // removed while it was decoding
        }
        Sound& sound = sounds_[static_cast<size_t>(index)];
        sound.loading = false;
        if (result.data) {
            sound.data = std::move(result.data);
            sound.durationMs = sound.data->durationMs;
            sound.failed = false;
            sound.error.clear();
            LogLine(L"decoded %s: %llu frames, %u ms", sound.path.c_str(),
                    static_cast<unsigned long long>(sound.data->frameCount), sound.durationMs);
        } else {
            sound.failed = true;
            sound.error = result.error;
        }
    }
}

}  // namespace echopad
