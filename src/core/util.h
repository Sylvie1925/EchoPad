// EchoPad - paths, text helpers, logging, and the flat config file.
#pragma once

#include "common.h"

namespace echopad {

// ------------------------------------------------------------------- paths

std::wstring ExePath();
std::wstring ExeDirectory();

// Directory that holds EchoPad.ini / sounds.tsv. Prefers the portable location
// next to the executable; falls back to %APPDATA%\EchoPad when the executable
// directory is not writable (e.g. installed under Program Files).
std::wstring ConfigDirectory();
std::wstring ConfigFilePath();

std::wstring JoinPath(const std::wstring& directory, const std::wstring& leaf);
std::wstring FileNameOf(const std::wstring& path);
std::wstring FileStemOf(const std::wstring& path);
std::wstring ExtensionOf(const std::wstring& path);  // lowercase, includes the dot
bool DirectoryExists(const std::wstring& path);
bool FileExists(const std::wstring& path);

// UTF-8 text file helpers.
bool ReadTextFile(const std::wstring& path, std::wstring& out);
bool WriteTextFile(const std::wstring& path, const std::wstring& text);
bool DeleteFileAt(const std::wstring& path);

// ------------------------------------------------------------------ strings

std::string Narrow(const std::wstring& text);
std::wstring Widen(const std::string& text);
std::wstring Trim(const std::wstring& text);
std::wstring ToLower(std::wstring text);
bool EqualsNoCase(const std::wstring& a, const std::wstring& b);
std::vector<std::wstring> Split(const std::wstring& text, wchar_t separator);

// "0x80070005 (Access is denied.)"
std::wstring HrText(HRESULT hr);

// ------------------------------------------------------------------ logging
//
// Logging is for control threads only. The audio thread must never call these:
// they touch the filesystem and take a lock.
void LogLine(const wchar_t* format, ...);
void LogError(const wchar_t* what, HRESULT hr);
std::wstring LogFilePath();

// --------------------------------------------------------------- config file
//
// A deliberately simple UTF-8 "key=value" file with '#' comments. Keeping the
// format flat means the whole parser is a few dozen lines and a user can fix a
// broken config in Notepad.
class KeyValueFile {
public:
    bool Load(const std::wstring& path);
    bool Save(const std::wstring& path) const;

    std::wstring Get(const wchar_t* key, const wchar_t* fallback = L"") const;
    int GetInt(const wchar_t* key, int fallback) const;
    float GetFloat(const wchar_t* key, float fallback) const;
    bool GetBool(const wchar_t* key, bool fallback) const;

    void Set(const wchar_t* key, const std::wstring& value);
    void SetInt(const wchar_t* key, int value);
    void SetFloat(const wchar_t* key, float value);
    void SetBool(const wchar_t* key, bool value);

private:
    std::vector<std::pair<std::wstring, std::wstring>> entries_;

    const std::wstring* Find(const wchar_t* key) const;
};

}  // namespace echopad
