@echo off
rem Builds OKTAKT for Windows (Standalone OKTAKT.exe, VST3, CLAP) into dist-windows\
rem Needs: Visual Studio 2022 or newer (or its Build Tools) with "Desktop development with C++", and CMake 3.24+.
setlocal
cd /d "%~dp0\.."

where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake was not found. Install it from https://cmake.org/download/ and tick "Add CMake to the PATH".
    exit /b 1
)

rem CMake picks the newest Visual Studio it finds
cmake -S . -B build-win -A x64 -DOKTAKT_INSTALL_PLUGINS=OFF
if errorlevel 1 goto fail
cmake --build build-win --config Release --parallel
if errorlevel 1 goto fail

set ART=build-win\OKTAKT_artefacts\Release
set OUT=dist-windows
if exist %OUT% rmdir /s /q %OUT%
mkdir %OUT%
copy /y "%ART%\Standalone\OKTAKT.exe" %OUT%\ >nul
xcopy /e /i /q /y "%ART%\VST3\OKTAKT.vst3" %OUT%\OKTAKT.vst3 >nul
if exist "%ART%\CLAP\OKTAKT.clap" copy /y "%ART%\CLAP\OKTAKT.clap" %OUT%\ >nul

echo.
echo Done. In %CD%\%OUT%:
echo   OKTAKT.exe    - standalone app
echo   OKTAKT.vst3   - copy this folder to C:\Program Files\Common Files\VST3\ (needs admin), then rescan plugins in your DAW
echo   OKTAKT.clap   - copy to C:\Program Files\Common Files\CLAP\ for DAWs with CLAP support
exit /b 0

:fail
echo.
echo Build failed. Scroll up for the first error.
exit /b 1
