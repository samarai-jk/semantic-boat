@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Semantic Boat firmware build and flash wrapper for Windows.
REM
REM Usage examples:
REM   script\build.bat remote-a
REM   script\build.bat remote-a debug
REM   script\build.bat remote-a refresh
REM   script\build.bat remote-a debug refresh
REM   script\build.bat remote-a flash
REM   script\build.bat remote-a debug flash
REM   script\build.bat remote-a update

pushd "%~dp0.." >NUL 2>&1
if errorlevel 1 (
  echo [build.bat] Error: unable to enter the repository root.
  exit /b 1
)

set "HELP_EXIT=0"
if "%~1"=="" (
  set "HELP_EXIT=1"
  goto :missing_firmware
)
if /I "%~1"=="help" goto :help
if /I "%~1"=="-h" goto :help
if /I "%~1"=="/h" goto :help
if /I "%~1"=="/help" goto :help

set "FIRMWARE=%~1"
shift

if not exist "src\dev\%FIRMWARE%\CMakeLists.txt" (
  echo [build.bat] Error: unknown firmware "%FIRMWARE%".
  echo [build.bat] Expected src\dev\%FIRMWARE%\CMakeLists.txt.
  popd >NUL 2>&1
  exit /b 1
)

set "BUILD_TYPE=release"
set "DO_REFRESH=0"
set "DO_FLASH=0"

:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="debug" (
  set "BUILD_TYPE=debug"
  goto :next_arg
)
if /I "%~1"=="release" (
  set "BUILD_TYPE=release"
  goto :next_arg
)
if /I "%~1"=="refresh" (
  set "DO_REFRESH=1"
  goto :next_arg
)
if /I "%~1"=="flash" (
  set "DO_FLASH=1"
  goto :next_arg
)
if /I "%~1"=="update" (
  set "DO_REFRESH=1"
  set "DO_FLASH=1"
  goto :next_arg
)
if /I "%~1"=="help" goto :help
if /I "%~1"=="-h" goto :help
if /I "%~1"=="/h" goto :help
if /I "%~1"=="/help" goto :help
echo [build.bat] Warning: unknown argument "%~1" ignored.

:next_arg
shift
goto :parse_args

:args_done
set "PRESET=%FIRMWARE%-%BUILD_TYPE%"
set "BUILD_DIR=build\%FIRMWARE%\%BUILD_TYPE%"
set "ELF_PATH=%BUILD_DIR%\src\dev\%FIRMWARE%\%FIRMWARE%.elf"

where cmake >NUL 2>&1
if errorlevel 1 (
  echo [build.bat] Error: cmake was not found on PATH.
  popd >NUL 2>&1
  exit /b 1
)

echo [build.bat] Firmware: %FIRMWARE% ^| type: %BUILD_TYPE% ^| refresh=%DO_REFRESH% ^| flash=%DO_FLASH%
echo [build.bat] Configuring preset "%PRESET%"...
cmake --preset "%PRESET%"
if errorlevel 1 (
  echo [build.bat] Error: configure failed for preset "%PRESET%".
  popd >NUL 2>&1
  exit /b 1
)

if "%DO_REFRESH%"=="1" (
  echo [build.bat] Refresh build - no clean.
  cmake --build --preset "%PRESET%"
) else (
  echo [build.bat] Clean build.
  cmake --build --preset "%PRESET%" --clean-first
)
if errorlevel 1 (
  echo [build.bat] Error: build failed for "%FIRMWARE%".
  popd >NUL 2>&1
  exit /b 1
)

if not exist "%ELF_PATH%" (
  echo [build.bat] Error: expected firmware image was not produced:
  echo [build.bat]   %ELF_PATH%
  popd >NUL 2>&1
  exit /b 1
)

echo [build.bat] Build succeeded: %ELF_PATH%
if not "%DO_FLASH%"=="1" goto :done

set "OPENOCD_TARGET="
if /I "%FIRMWARE%"=="remote-a" set "OPENOCD_TARGET=target/stm32l4x.cfg"
if not defined OPENOCD_TARGET (
  echo [build.bat] Error: no OpenOCD target is configured for "%FIRMWARE%".
  echo [build.bat] Add its mapping to script\build.bat before flashing.
  popd >NUL 2>&1
  exit /b 1
)

if not defined OPENOCD set "OPENOCD=E:\Tools\xpack-openocd-0.12.0-7\bin\openocd.exe"
if not defined OPENOCD_SCRIPTS set "OPENOCD_SCRIPTS=E:\Tools\xpack-openocd-0.12.0-7\openocd\scripts"

if not exist "%OPENOCD%" (
  echo [build.bat] Error: OpenOCD not found at "%OPENOCD%".
  echo [build.bat] Set OPENOCD to the full path of openocd.exe.
  popd >NUL 2>&1
  exit /b 1
)
if not exist "%OPENOCD_SCRIPTS%\%OPENOCD_TARGET:/=\%" (
  echo [build.bat] Error: OpenOCD target script "%OPENOCD_TARGET%" was not found.
  echo [build.bat] Set OPENOCD_SCRIPTS to the OpenOCD scripts directory.
  popd >NUL 2>&1
  exit /b 1
)

set "ELF_OPENOCD=!ELF_PATH:\=/!"
echo [build.bat] Flashing %FIRMWARE%...
"%OPENOCD%" -s "%OPENOCD_SCRIPTS%" -f interface/stlink.cfg -f "%OPENOCD_TARGET%" -c "program {!ELF_OPENOCD!} verify reset exit"
if errorlevel 1 (
  echo [build.bat] Error: flash failed.
  popd >NUL 2>&1
  exit /b 1
)
echo [build.bat] Flash completed successfully.

:done
popd >NUL 2>&1
exit /b 0

:missing_firmware
echo [build.bat] Error: the first argument must name the firmware to build.
echo.

:help
echo Usage: script\build.bat ^<firmware^> [debug^|release] [refresh] [flash^|update]
echo.
echo   firmware - directory name below src\dev, for example remote-a
echo   debug    - use the ^<firmware^>-debug CMake preset
echo   release  - use the ^<firmware^>-release CMake preset ^(default^)
echo   refresh  - incremental build; without it, perform a clean build
echo   flash    - flash the ELF after a successful build
echo   update   - shorthand for refresh + flash
echo.
echo Examples:
echo   script\build.bat remote-a
echo   script\build.bat remote-a debug refresh
echo   script\build.bat remote-a update
popd >NUL 2>&1
exit /b %HELP_EXIT%
