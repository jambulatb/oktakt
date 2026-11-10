@echo off
rem Builds OKTAKT for Windows and packs it into one installer: dist-windows\OKTAKT-<version>-Windows-Setup.exe
rem The installer lets you tick what to install: the app (Standalone), VST3, CLAP and the demo kit.
rem Uninstall: Windows Settings > Apps > OKTAKT.
rem Needs what build-windows.bat needs, plus Inno Setup 6.5.2 or newer: https://jrsoftware.org/isdl.php
setlocal
cd /d "%~dp0\.."

set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%LocalAppData%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" goto noinno

set VERSION=
for /f "tokens=4 delims=() " %%v in ('findstr /b /c:"project(OKTAKT VERSION" CMakeLists.txt') do set "VERSION=%%v"
if not defined VERSION goto nover

call "%~dp0build-windows.bat"
if errorlevel 1 exit /b 1

echo.
echo == Packing the installer, OKTAKT %VERSION%
"%ISCC%" /DAppVersion=%VERSION% "scripts\OKTAKT.iss"
if errorlevel 1 goto fail

echo.
echo Done: %CD%\dist-windows\OKTAKT-%VERSION%-Windows-Setup.exe
echo Run it and tick what to install. Windows SmartScreen may warn at first: More info, Run anyway.
exit /b 0

:noinno
echo Inno Setup 6 was not found. Install it from https://jrsoftware.org/isdl.php and run this again.
exit /b 1

:nover
echo Could not read the version from CMakeLists.txt.
exit /b 1

:fail
echo.
echo Packing the installer failed. Scroll up for the first error.
exit /b 1
