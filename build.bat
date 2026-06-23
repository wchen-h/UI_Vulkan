@echo off
REM ============================================================
REM  UI_Vulkan build helper (Windows).
REM
REM    build.bat            clean Release rebuild (default)
REM    build.bat inc        incremental build (fast, keeps build dir)
REM    build.bat debug      clean Debug rebuild
REM    build.bat clean      explicit clean Release rebuild
REM
REM  Run from a "Developer Command Prompt for VS" (needs cmake + MSVC
REM  or another compiler on PATH). Dependencies (glfw/glm) are cached
REM  in .deps\ so a clean rebuild does NOT re-download them.
REM ============================================================
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "BUILD=%ROOT%\build"
set "DEPS=%ROOT%\.deps"
set "TYPE=Release"
set "CLEAN=1"

set "ARG=%~1"
if "%ARG%"=="" goto doconfig
if /I "%ARG%"=="inc"    ( set "CLEAN=0" & goto doconfig )
if /I "%ARG%"=="fast"   ( set "CLEAN=0" & goto doconfig )
if /I "%ARG%"=="debug"   ( set "TYPE=Debug" & goto doconfig )
if /I "%ARG%"=="release" goto doconfig
if /I "%ARG%"=="clean"   goto doconfig
echo Usage: build.bat [clean^|inc^|debug]   (default: clean Release)
exit /b 1

:doconfig
where cmake >nul 2>nul || ( echo [ERROR] cmake not found on PATH. & exit /b 1 )

if "%CLEAN%"=="1" (
  echo [clean] removing build\
  if exist "%BUILD%" rmdir /s /q "%BUILD%"
)
if not exist "%BUILD%" mkdir "%BUILD%"

if not exist "%BUILD%\CMakeCache.txt" (
  echo configure: %TYPE%  deps cache: %DEPS%
  cmake -S "%ROOT%" -B "%BUILD%" -DCMAKE_BUILD_TYPE="%TYPE%" -DFETCHCONTENT_BASE_DIR="%DEPS%"
  if errorlevel 1 exit /b 1
)

echo build (%TYPE%)
cmake --build "%BUILD%" --config %TYPE% -j
if errorlevel 1 exit /b 1

echo.
echo done. Executables:
if exist "%BUILD%\%TYPE%\UI_Vulkan_SDR.exe" (
  dir "%BUILD%\%TYPE%\UI_Vulkan_SDR.exe" "%BUILD%\%TYPE%\UI_Vulkan_HDR.exe"
) else if exist "%BUILD%\UI_Vulkan_SDR.exe" (
  dir "%BUILD%\UI_Vulkan_SDR.exe" "%BUILD%\UI_Vulkan_HDR.exe"
) else (
  echo [WARN] executables not found in expected locations; check %BUILD%.
)
endlocal
