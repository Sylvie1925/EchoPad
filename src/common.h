// EchoPad - shared includes, COM pointer, and the internal audio format.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// Without these, MinGW-w64 trims NOTIFYICONDATA and other shell structures down
// to their pre-2000 shape. Windows 7 is the floor EchoPad targets.
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0A00
#endif

#include <windows.h>
#include <mmreg.h>
#include <objbase.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// MinGW-w64's audioclient.h is missing a few flags that ship in the MSVC SDK.
// Defining them here keeps the source identical for both compilers.
#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

namespace echopad {

// ---------------------------------------------------------------------------
// Minimal COM smart pointer.
//
// Deliberately hand-rolled rather than using <wrl/client.h>: it is small, and
// it behaves identically under MSVC and MinGW.
// ---------------------------------------------------------------------------
template <typename T>
class ComPtr {
public:
    ComPtr() noexcept = default;
    ComPtr(std::nullptr_t) noexcept {}
    explicit ComPtr(T* raw) noexcept : p_(raw) {}

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : p_(other.p_) { other.p_ = nullptr; }

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            p_ = other.p_;
            other.p_ = nullptr;
        }
        return *this;
    }

    ~ComPtr() { Reset(); }

    void Reset() noexcept {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }

    T* Get() const noexcept { return p_; }

    // Address-of for COM out-parameters. Releases any existing reference first.
    T** Put() noexcept {
        Reset();
        return &p_;
    }

    void** PutVoid() noexcept {
        Reset();
        return reinterpret_cast<void**>(&p_);
    }

    T* operator->() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
    bool operator==(std::nullptr_t) const noexcept { return p_ == nullptr; }
    bool operator!=(std::nullptr_t) const noexcept { return p_ != nullptr; }

    T* Detach() noexcept {
        T* raw = p_;
        p_ = nullptr;
        return raw;
    }

private:
    T* p_ = nullptr;
};

// ---------------------------------------------------------------------------
// Internal audio format.
//
// Everything inside the mixer is 32-bit float, interleaved, at the render
// device's sample rate. Decoders hand us exactly this format, so the audio
// callback never converts or resamples.
// ---------------------------------------------------------------------------
using Sample = float;

struct AudioFormat {
    uint32_t sampleRate = 48000;
    uint16_t channels = 2;

    uint32_t bytesPerFrame() const {
        return static_cast<uint32_t>(channels) * static_cast<uint32_t>(sizeof(Sample));
    }

    bool operator==(const AudioFormat& o) const {
        return sampleRate == o.sampleRate && channels == o.channels;
    }
    bool operator!=(const AudioFormat& o) const { return !(*this == o); }
};

// Longest sound we are willing to keep fully decoded in RAM. Decoding up front
// is what keeps the audio thread allocation-free and near 0% CPU.
constexpr uint32_t kMaxSoundSeconds = 600;

}  // namespace echopad
