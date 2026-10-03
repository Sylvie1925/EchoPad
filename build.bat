@echo off
rem ===========================================================================
rem  EchoPad build script  (MinGW-w64 / w64devkit)
rem
rem  Produces a single self-contained build\EchoPad.exe with no runtime DLL
rem  dependencies. Just double-click this file, or run it from a terminal.
rem
rem  For an MSVC build use CMakeLists.txt instead.
rem ===========================================================================
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "SRCDIR=%ROOT%src"
set "RESDIR=%ROOT%res"
set "BUILD=%ROOT%build"
set "OBJDIR=%BUILD%\obj"

rem ------------------------------------------------------------- toolchain ---
rem Prefer the portable w64devkit shipped next to this project, then a
rem sibling "tools" folder, then whatever g++ happens to be on PATH.
set "TOOLBIN="
if exist "%ROOT%tools\w64devkit\w64devkit\bin\g++.exe" set "TOOLBIN=%ROOT%tools\w64devkit\w64devkit\bin"
if not defined TOOLBIN if exist "%ROOT%..\tools\w64devkit\w64devkit\bin\g++.exe" set "TOOLBIN=%ROOT%..\tools\w64devkit\w64devkit\bin"

if defined TOOLBIN (
  set "CXX=%TOOLBIN%\g++.exe"
  set "WINDRES=%TOOLBIN%\windres.exe"
) else (
  set "CXX=g++"
  set "WINDRES=windres"
)

"%CXX%" --version >nul 2>nul
if errorlevel 1 (
  echo.
  echo [ERROR] No C++ compiler found.
  echo         Looked for a portable w64devkit in:
  echo             %ROOT%tools\w64devkit\w64devkit\bin
  echo             %ROOT%..\tools\w64devkit\w64devkit\bin
  echo         and for g++ on PATH.
  echo.
  echo         Either install w64devkit / MSYS2 / MinGW-w64 and put g++ on PATH,
  echo         or open the project in Visual Studio and use CMakeLists.txt.
  echo.
  exit /b 1
)

rem ------------------------------------------------------------------ flags ---
set "CXXFLAGS=-std=c++20 -O2 -DNDEBUG -Wall -Wextra -Wno-unused-parameter -finput-charset=UTF-8 -fexec-charset=UTF-8 -I"%SRCDIR%" -I"%RESDIR%""
rem 6.1 subsystem: tells Windows this is a Windows 7+ app so no compatibility
rem shims are applied. "--dynamicbase --nxcompat" opt into ASLR and DEP.
set "LDFLAGS=-mwindows -static -static-libgcc -static-libstdc++ -s -Wl,--major-subsystem-version,6 -Wl,--minor-subsystem-version,1 -Wl,--dynamicbase -Wl,--nxcompat"
set "LIBS=-lole32 -loleaut32 -luuid -lmmdevapi -lavrt -lmfplat -lmfreadwrite -lmfuuid -lmf -lshlwapi -lcomctl32 -lcomdlg32 -lshell32 -luser32 -lgdi32 -ladvapi32"

if not exist "%OBJDIR%" mkdir "%OBJDIR%" >nul 2>nul
if exist "%BUILD%\objects.rsp" del /q "%BUILD%\objects.rsp"

rem ------------------------------------------------------------ temp folder ---
rem The compiler writes intermediate files through GetTempPath(), which reads
rem %%TMP%% first and only then %%TEMP%%. On locked-down machines and inside
rem restricted sandboxes that path can be unwritable, which surfaces as
rem   "Cannot create temporary file in C:\...\Temp: Permission denied"
rem before a single line of code is compiled. Point both variables at a
rem directory we create ourselves, inside the build output.
set "BUILDTMP=%BUILD%\tmp"
mkdir "%BUILDTMP%" >nul 2>nul
if exist "%BUILDTMP%" (
  set "TMP=%BUILDTMP%"
  set "TEMP=%BUILDTMP%"
) else (
  echo [WARN] could not create "%BUILDTMP%", using the system TEMP instead.
)

echo Building EchoPad with "%CXX%"
echo.

set /a COUNT=0
for /r "%SRCDIR%" %%F in (*.cpp) do (
  echo   [cc] %%~nxF
  "%CXX%" %CXXFLAGS% -c "%%F" -o "%OBJDIR%\%%~nF.o"
  if errorlevel 1 (
    echo.
    echo [ERROR] compilation failed: %%F
    exit /b 1
  )
  >>"%BUILD%\objects.rsp" echo "%OBJDIR%\%%~nF.o"
  set /a COUNT+=1
)

rem --------------------------------------------------------------- resources ---
"%WINDRES%" --version >nul 2>nul
if not errorlevel 1 (
  echo   [rc] echopad.rc
  "%WINDRES%" -I"%RESDIR%" "%RESDIR%\echopad.rc" -O coff -o "%OBJDIR%\echopad_resources.o"
  if errorlevel 1 (
    echo [WARN] resource compilation failed; building without icon/manifest.
  ) else (
    >>"%BUILD%\objects.rsp" echo "%OBJDIR%\echopad_resources.o"
  )
) else (
  echo [WARN] windres not found; building without icon/manifest.
)

rem ------------------------------------------------------------------- link ---
echo.
echo   [ld] EchoPad.exe
"%CXX%" @"%BUILD%\objects.rsp" -o "%BUILD%\EchoPad.exe" %LDFLAGS% %LIBS%
if errorlevel 1 (
  echo.
  echo [ERROR] link failed.
  exit /b 1
)

echo.
echo Done. %COUNT% source files compiled.
for %%A in ("%BUILD%\EchoPad.exe") do echo Output: %%~fA  ^(%%~zA bytes^)
echo.
endlocal
exit /b 0
