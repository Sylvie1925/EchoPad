#include "util.h"

#include <strsafe.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <mutex>

namespace echopad {
namespace {

std::mutex g_logMutex;

bool IsDirectoryWritable(const std::wstring& directory) {
    const std::wstring probe = JoinPath(directory, L".echopad-write-probe.tmp");
    HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(file);
    return true;
}

std::vector<uint8_t> ReadAllBytes(const std::wstring& path) {
    std::vector<uint8_t> data;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return data;
    }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (64LL << 20)) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        DWORD total = 0;
        while (total < data.size() &&
               ReadFile(file, data.data() + total, static_cast<DWORD>(data.size() - total),
                        &read, nullptr)) {
            if (read == 0) {
                break;
            }
            total += read;
        }
        data.resize(total);
    }
    CloseHandle(file);
    return data;
}

bool WriteAllBytes(const std::wstring& path, const void* bytes, size_t count) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    const uint8_t* cursor = static_cast<const uint8_t*>(bytes);
    size_t written = 0;
    bool ok = true;
    while (written < count) {
        DWORD chunk = 0;
        if (!WriteFile(file, cursor + written, static_cast<DWORD>(count - written), &chunk,
                       nullptr)) {
            ok = false;
            break;
        }
        written += chunk;
    }
    CloseHandle(file);
    return ok;
}

}  // namespace

// ------------------------------------------------------------------- paths

std::wstring ExePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return L"";
        }
        if (written < buffer.size() - 1) {
            buffer.resize(written);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring ExeDirectory() {
    const std::wstring path = ExePath();
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(0, slash);
}

std::wstring JoinPath(const std::wstring& directory, const std::wstring& leaf) {
    if (directory.empty()) {
        return leaf;
    }
    std::wstring result = directory;
    const wchar_t last = result.back();
    if (last != L'\\' && last != L'/') {
        result.push_back(L'\\');
    }
    result.append(leaf);
    return result;
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring FileStemOf(const std::wstring& path) {
    std::wstring name = FileNameOf(path);
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        name.resize(dot);
    }
    return name;
}

std::wstring ExtensionOf(const std::wstring& path) {
    const std::wstring name = FileNameOf(path);
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return L"";
    }
    return ToLower(name.substr(dot));
}

bool DirectoryExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool ReadTextFile(const std::wstring& path, std::wstring& out) {
    const std::vector<uint8_t> raw = ReadAllBytes(path);
    if (raw.empty()) {
        out.clear();
        return false;
    }
    // Tolerate a UTF-8 BOM.
    size_t start = 0;
    if (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        start = 3;
    }
    out = Widen(std::string(raw.begin() + static_cast<ptrdiff_t>(start), raw.end()));
    return true;
}

bool WriteTextFile(const std::wstring& path, const std::wstring& text) {
    const std::string utf8 = Narrow(text);
    return WriteAllBytes(path, utf8.data(), utf8.size());
}

bool DeleteFileAt(const std::wstring& path) {
    return DeleteFileW(path.c_str()) != 0;
}

std::wstring ConfigDirectory() {
    static const std::wstring cached = [] {
        const std::wstring exeDir = ExeDirectory();
        if (!exeDir.empty() && IsDirectoryWritable(exeDir)) {
            return exeDir;
        }
        wchar_t appData[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
        const std::wstring base =
            (length > 0 && length < MAX_PATH) ? std::wstring(appData, length) : exeDir;
        const std::wstring dir = JoinPath(base, L"EchoPad");
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }();
    return cached;
}

std::wstring ConfigFilePath() {
    return JoinPath(ConfigDirectory(), L"EchoPad.ini");
}

std::wstring LogFilePath() {
    return JoinPath(ConfigDirectory(), L"echopad.log");
}

// ------------------------------------------------------------------ strings

std::string Narrow(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return std::string();
    }
    std::string result(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                        needed, nullptr, nullptr);
    return result;
}

std::wstring Widen(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0);
    if (needed <= 0) {
        return std::wstring();
    }
    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                        needed);
    return result;
}

std::wstring Trim(const std::wstring& text) {
    size_t begin = 0;
    size_t end = text.size();
    const std::wstring whitespace = L" \t\r\n";
    while (begin < end && whitespace.find(text[begin]) != std::wstring::npos) {
        ++begin;
    }
    while (end > begin && whitespace.find(text[end - 1]) != std::wstring::npos) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::wstring ToLower(std::wstring text) {
    for (wchar_t& ch : text) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
    }
    return text;
}

bool EqualsNoCase(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

std::vector<std::wstring> Split(const std::wstring& text, wchar_t separator) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (;;) {
        const size_t position = text.find(separator, start);
        if (position == std::wstring::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, position - start));
        start = position + 1;
    }
    return parts;
}

std::wstring HrText(HRESULT hr) {
    wchar_t message[512] = L"";
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        static_cast<DWORD>(hr), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), message,
        static_cast<DWORD>(std::size(message)), nullptr);
    std::wstring text = length ? Trim(std::wstring(message, length)) : L"unknown error";
    wchar_t code[32];
    StringCchPrintfW(code, std::size(code), L"0x%08lX", static_cast<unsigned long>(hr));
    return std::wstring(code) + L" (" + text + L")";
}

// ------------------------------------------------------------------ logging

void LogLine(const wchar_t* format, ...) {
    wchar_t message[1024];
    va_list args;
    va_start(args, format);
    StringCchVPrintfW(message, std::size(message), format, args);
    va_end(args);

    SYSTEMTIME now{};
    GetLocalTime(&now);

    const std::string utf8 = Narrow(message);
    char line[1400];
    if (FAILED(StringCchPrintfA(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u  %s\r\n",
                                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                                now.wSecond, utf8.c_str()))) {
        return;
    }
    const size_t length = std::strlen(line);

    std::lock_guard<std::mutex> guard(g_logMutex);
    const std::wstring path = LogFilePath();

    // Keep the log from growing without bound.
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
        const ULARGE_INTEGER size{{info.nFileSizeLow, info.nFileSizeHigh}};
        if (size.QuadPart > (1u << 20)) {
            DeleteFileW(path.c_str());
        }
    }

    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
    CloseHandle(file);
}

void LogError(const wchar_t* what, HRESULT hr) {
    LogLine(L"%s failed: %s", what, HrText(hr).c_str());
}

// --------------------------------------------------------------- config file

const std::wstring* KeyValueFile::Find(const wchar_t* key) const {
    for (const auto& entry : entries_) {
        if (EqualsNoCase(entry.first, key)) {
            return &entry.second;
        }
    }
    return nullptr;
}

bool KeyValueFile::Load(const std::wstring& path) {
    entries_.clear();
    const std::vector<uint8_t> raw = ReadAllBytes(path);
    if (raw.empty()) {
        return false;
    }
    const std::wstring text = Widen(std::string(raw.begin(), raw.end()));

    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = text.size();
        }
        std::wstring line = Trim(text.substr(start, end - start));
        start = end + 1;

        if (!line.empty() && line[0] != L'#' && line[0] != L';') {
            const size_t equals = line.find(L'=');
            if (equals != std::wstring::npos) {
                const std::wstring key = Trim(line.substr(0, equals));
                const std::wstring value = Trim(line.substr(equals + 1));
                if (!key.empty()) {
                    entries_.emplace_back(key, value);
                }
            }
        }
        if (end == text.size()) {
            break;
        }
    }
    return true;
}

bool KeyValueFile::Save(const std::wstring& path) const {
    std::wstring text = L"# EchoPad configuration. Edit with Notepad while EchoPad is closed.\r\n";
    for (const auto& entry : entries_) {
        text += entry.first;
        text += L'=';
        text += entry.second;
        text += L"\r\n";
    }
    const std::string utf8 = Narrow(text);
    return WriteAllBytes(path, utf8.data(), utf8.size());
}

std::wstring KeyValueFile::Get(const wchar_t* key, const wchar_t* fallback) const {
    const std::wstring* found = Find(key);
    return found ? *found : std::wstring(fallback);
}

int KeyValueFile::GetInt(const wchar_t* key, int fallback) const {
    const std::wstring* found = Find(key);
    if (!found || found->empty()) {
        return fallback;
    }
    return static_cast<int>(wcstol(found->c_str(), nullptr, 10));
}

float KeyValueFile::GetFloat(const wchar_t* key, float fallback) const {
    const std::wstring* found = Find(key);
    if (!found || found->empty()) {
        return fallback;
    }
    return static_cast<float>(wcstod(found->c_str(), nullptr));
}

bool KeyValueFile::GetBool(const wchar_t* key, bool fallback) const {
    const std::wstring* found = Find(key);
    if (!found || found->empty()) {
        return fallback;
    }
    return *found == L"1" || EqualsNoCase(*found, L"true") || EqualsNoCase(*found, L"yes");
}

void KeyValueFile::Set(const wchar_t* key, const std::wstring& value) {
    for (auto& entry : entries_) {
        if (EqualsNoCase(entry.first, key)) {
            entry.second = value;
            return;
        }
    }
    entries_.emplace_back(key, value);
}

void KeyValueFile::SetInt(const wchar_t* key, int value) {
    wchar_t text[32];
    StringCchPrintfW(text, std::size(text), L"%d", value);
    Set(key, text);
}

void KeyValueFile::SetFloat(const wchar_t* key, float value) {
    wchar_t text[64];
    StringCchPrintfW(text, std::size(text), L"%.4f", static_cast<double>(value));
    Set(key, text);
}

void KeyValueFile::SetBool(const wchar_t* key, bool value) { Set(key, value ? L"1" : L"0"); }

}  // namespace echopad
