@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM build.bat - convenience wrapper for GNU Make builds on Windows
REM Usage examples:
REM   build.bat                     -> Clean + Release build
REM   build.bat debug               -> Clean + Debug build (-O0, rich debug info)
REM   build.bat refresh             -> Refresh build (no clean) if type unchanged; otherwise clean rebuild
REM   build.bat debug refresh       -> Debug refresh if prior build was debug; else clean + debug build
REM   build.bat debug flash         -> Clean + Debug build, then flash
REM   build.bat flash               -> Clean + Release build, then flash

REM Change to project root (folder with Makefile)
pushd "%~dp0.." >NUL 2>&1
if not exist Makefile (
  echo [build.bat] Error: Makefile not found in %CD%
  popd >NUL 2>&1
  exit /b 1
)

REM Parse args (case-insensitive): refresh, debug, flash
set "DO_REFRESH=0"
set "DO_DEBUG=0"
set "DO_FLASH=0"

if "%~1"=="" goto :no_args

:parse_args
if "%~1"=="" goto :args_done
set "ARG=%~1"
for %%A in ("%ARG%") do set "ARG=%%~A"
if /I "%ARG%"=="refresh" set "DO_REFRESH=1"& goto :next_arg
if /I "%ARG%"=="debug"   set "DO_DEBUG=1"  & goto :next_arg
if /I "%ARG%"=="flash"   set "DO_FLASH=1"  & goto :next_arg

if /I "%ARG%"=="/h"  goto :help
if /I "%ARG%"=="-h"  goto :help
if /I "%ARG%"=="/help" goto :help
if /I "%ARG%"=="help"  goto :help

echo [build.bat] Warning: Unknown argument "%~1" ignored.

:next_arg
shift
goto :parse_args

:args_done
goto :begin

:no_args
REM Default: clean + release build
set "DO_REFRESH=0"
set "DO_DEBUG=0"
set "DO_FLASH=0"
goto :begin

:help
echo Usage: build.bat [refresh] [debug] [flash]
echo   refresh - force rebuild without clean when possible; if type changed, clean rebuild
echo   debug   - build with FULL_DEBUG=1 (-O0, rich debug info)
echo   flash   - flash after successful build
popd >NUL 2>&1
exit /b 0

:begin
set "BUILD_TYPE=release"
set "MAKE_FLAGS="
if "%DO_DEBUG%"=="1" (
  set "BUILD_TYPE=debug"
  set "MAKE_FLAGS=FULL_DEBUG=1"
)

REM Determine last build type (if any)
set "LAST_TYPE=unknown"
if exist build\.build_type.txt (
  set /p LAST_TYPE=< build\.build_type.txt
)

REM Decide whether to clean, refresh, or normal build
set "NEED_CLEAN=1"
set "USE_REFRESH=0"
if "%DO_REFRESH%"=="1" (
  if /I "%LAST_TYPE%"=="%BUILD_TYPE%" (
    set "NEED_CLEAN=0"
    set "USE_REFRESH=1"
  ) else (
    set "NEED_CLEAN=1"
    set "USE_REFRESH=0"
  )
) else (
  REM No refresh requested -> clean build
  set "NEED_CLEAN=1"
  set "USE_REFRESH=0"
)

echo [build.bat] Requested build: %BUILD_TYPE% ^| Last: %LAST_TYPE% ^| refresh=%DO_REFRESH% ^| flash=%DO_FLASH%

if "%NEED_CLEAN%"=="1" (
  echo [build.bat] Cleaning...
  make clean
  if errorlevel 1 (
    echo [build.bat] Error: make clean failed.
    popd >NUL 2>&1
    exit /b 1
  )
)

if "%USE_REFRESH%"=="1" (
  echo [build.bat] Refresh build - no clean - flags: %MAKE_FLAGS%
  if defined MAKE_FLAGS (
    make %MAKE_FLAGS%
  ) else (
    make
  )
) else (
  echo [build.bat] Building - flags: %MAKE_FLAGS%
  if defined MAKE_FLAGS (
    make %MAKE_FLAGS%
  ) else (
    make
  )
)

if errorlevel 1 (
  echo [build.bat] Error: build failed.
  popd >NUL 2>&1
  exit /b 1
)

REM Record build type for future refresh decisions
if not exist build mkdir build
echo %BUILD_TYPE%> build\.build_type.txt

echo [build.bat] Build succeeded. Type: %BUILD_TYPE%

if not "%DO_FLASH%"=="1" goto :done

echo [build.bat] Flashing...
if defined MAKE_FLAGS (
  make %MAKE_FLAGS% flash
) else (
  make flash
)
if errorlevel 1 (
  echo [build.bat] Error: flash failed.
  popd >NUL 2>&1
  exit /b 1
)

echo [build.bat] Flash completed successfully.

:done
popd >NUL 2>&1
exit /b 0
