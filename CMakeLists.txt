cmake_minimum_required(VERSION 3.20)
project(EchoPad LANGUAGES CXX)

# ---------------------------------------------------------------------------
#  EchoPad - lightweight soundboard for Windows
#
#  MinGW users can just run build.bat instead; this file exists so the project
#  also opens and builds cleanly in Visual Studio / CLion / plain CMake.
#
#    cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
#    cmake --build build-vs --config Release
# ---------------------------------------------------------------------------

if(NOT WIN32)
  message(FATAL_ERROR "EchoPad targets Windows only (WASAPI + Media Foundation).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ECHOPAD_SOURCES
  src/main.cpp
  src/app.cpp
  src/core/util.cpp
  src/core/ring_buffer.cpp
  src/core/guids.cpp
  src/audio/format.cpp
  src/audio/devices.cpp
  src/audio/engine.cpp
  src/media/decoder.cpp
  src/media/sound.cpp
  src/hotkey/hotkeys.cpp
  src/ui/tray.cpp
  src/ui/hotkey_capture.cpp
)

add_executable(EchoPad WIN32 ${ECHOPAD_SOURCES} res/echopad.rc)

target_include_directories(EchoPad PRIVATE src res)

target_compile_definitions(EchoPad PRIVATE
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  WINVER=0x0601
  _WIN32_WINNT=0x0601
  _WIN32_IE=0x0A00
)

target_link_libraries(EchoPad PRIVATE
  ole32 oleaut32 uuid
  mmdevapi avrt
  mfplat mfreadwrite mfuuid mf
  shlwapi comctl32 comdlg32 shell32 user32 gdi32 advapi32
)

if(MSVC)
  target_compile_options(EchoPad PRIVATE /W4 /permissive- /utf-8 /EHsc /MP)
  # Static CRT so the result is a single portable exe, like the MinGW build.
  set_property(TARGET EchoPad PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
else()
  target_compile_options(EchoPad PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
  target_link_options(EchoPad PRIVATE
    -static -static-libgcc -static-libstdc++
    -Wl,--major-subsystem-version,6 -Wl,--minor-subsystem-version,1
  )
endif()

set_target_properties(EchoPad PROPERTIES
  OUTPUT_NAME "EchoPad"
  DEBUG_POSTFIX ""
)
