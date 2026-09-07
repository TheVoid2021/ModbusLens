@echo off
setlocal enabledelayedexpansion

rem ============================================================
rem T008.1 - standalone Windows deployment helper
rem Usage:
rem   deploy_windows.bat [BUILD_DIR] [QT_BIN] [MINGW_BIN]
rem   - BUILD_DIR defaults to <project>\build\debug
rem   - QT_BIN / MINGW_BIN are derived from BUILD_DIR\CMakeCache.txt
rem     when omitted (no machine-specific paths are stored in this file)
rem Produces: build\deploy\ModbusLens.exe + all runtime dependencies
rem Exits non-zero on any failure.
rem ============================================================

set "SCRIPT_DIR=%~dp0"
set "SOURCE_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%SOURCE_DIR%\build\debug"
set "DEPLOY_DIR=%SOURCE_DIR%\build\deploy"

if not "%~1"=="" set "BUILD_DIR=%~1"
if not "%~2"=="" set "QT_BIN=%~2"
if not "%~3"=="" set "MINGW_BIN=%~3"

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [ERROR] CMakeCache.txt not found in "%BUILD_DIR%". Configure and build first.
    exit /b 1
)

rem ---- 1. derive MinGW compiler bin from CMAKE_CXX_COMPILER (CMakeCache) ----
if not defined MINGW_BIN (
    set "CXX_LINE="
    for /f "usebackq tokens=1* delims==" %%A in (`findstr /B /C:"CMAKE_CXX_COMPILER:" "%BUILD_DIR%\CMakeCache.txt"`) do set "CXX_LINE=%%B"
    if not defined CXX_LINE (
        echo [ERROR] CMAKE_CXX_COMPILER not found in CMakeCache.txt
        exit /b 1
    )
    for %%F in ("!CXX_LINE!") do set "MINGW_BIN=%%~dpF"
    set "MINGW_BIN=!MINGW_BIN:~0,-1!"
)
if not exist "!MINGW_BIN!\g++.exe" (
    echo [ERROR] g++.exe not found in "!MINGW_BIN!"
    exit /b 1
)

rem ---- 2. derive Qt bin from Qt6_DIR (CMakeCache) ----
if not defined QT_BIN (
    set "QT6_LINE="
    for /f "usebackq tokens=1* delims==" %%A in (`findstr /B /C:"Qt6_DIR:" "%BUILD_DIR%\CMakeCache.txt"`) do set "QT6_LINE=%%B"
    if not defined QT6_LINE (
        echo [ERROR] Qt6_DIR not found in CMakeCache.txt
        exit /b 1
    )
    set "QT_BIN=!QT6_LINE:/lib/cmake/Qt6=!"
    set "QT_BIN=!QT_BIN!/bin"
)
if not exist "!QT_BIN!\windeployqt.exe" (
    echo [ERROR] windeployqt.exe not found in "!QT_BIN!"
    exit /b 1
)

rem ---- 3. clean deployment directory ----
if exist "%DEPLOY_DIR%" rmdir /s /q "%DEPLOY_DIR%"
if exist "%DEPLOY_DIR%" (
    echo [ERROR] Failed to clean "%DEPLOY_DIR%"
    exit /b 1
)
mkdir "%DEPLOY_DIR%"

rem ---- 4. copy the app executable (final name kept) ----
if not exist "%BUILD_DIR%\modbuslens.exe" (
    echo [ERROR] "%BUILD_DIR%\modbuslens.exe" not found. Build first.
    exit /b 1
)
copy /y "%BUILD_DIR%\modbuslens.exe" "%DEPLOY_DIR%\ModbusLens.exe" >nul

rem ---- 5. windeployqt: Qt DLLs / plugins / QML runtime ----
pushd "%SOURCE_DIR%"
"!QT_BIN!\windeployqt.exe" --qmldir "%SOURCE_DIR%\src\ui\qml" --compiler-runtime "%DEPLOY_DIR%\ModbusLens.exe" >nul
set "WDEPLOY_EXIT=!ERRORLEVEL!"
popd
if not "!WDEPLOY_EXIT!"=="0" (
    echo [ERROR] windeployqt failed with exit code !WDEPLOY_EXIT!
    exit /b !WDEPLOY_EXIT!
)

rem ---- 6. force MinGW runtime from the ACTUAL compiler bin ----
rem (windeployqt copies may come from elsewhere; provenance must be the
rem  toolchain that compiled this exe. Overwrite deterministically.)
for %%D in (libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll) do (
    if not exist "!MINGW_BIN!\%%D" (
        echo [ERROR] "!MINGW_BIN!\%%D" not found
        exit /b 1
    )
    copy /y "!MINGW_BIN!\%%D" "%DEPLOY_DIR%\%%D" >nul
)

rem ---- 7. deploy the app QML module (type Main comes from this qmldir) ----
mkdir "%DEPLOY_DIR%\ModbusLens"
copy /y "%SOURCE_DIR%\src\ui\qml\Main.qml" "%DEPLOY_DIR%\ModbusLens\Main.qml" >nul
> "%DEPLOY_DIR%\ModbusLens\qmldir" (
    echo module ModbusLens
    echo Main 1.0 Main.qml
    echo depends QtQuick
)

rem ---- 7b. canonical replay sample (business data — explicitly NOT
rem windeployqt's job): one source in samples/, shared by tests, deployment
rem and the manual smoke. ----
if not exist "%SOURCE_DIR%\samples\demo_v1.mlog" (
    echo [ERROR] Canonical sample "%SOURCE_DIR%\samples\demo_v1.mlog" not found
    exit /b 1
)
mkdir "%DEPLOY_DIR%\samples"
copy /y "%SOURCE_DIR%\samples\demo_v1.mlog" "%DEPLOY_DIR%\samples\demo_v1.mlog" >nul

rem ---- 8. verify key deployment files ----
for %%F in (ModbusLens.exe Qt6Core.dll Qt6Gui.dll Qt6Qml.dll Qt6Quick.dll Qt6QuickControls2.dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll platforms\qwindows.dll ModbusLens\qmldir ModbusLens\Main.qml samples\demo_v1.mlog) do (
    if not exist "%DEPLOY_DIR%\%%F" (
        echo [ERROR] Missing deployment file: %%F
        exit /b 1
    )
)

echo [OK] Deployment directory ready: %DEPLOY_DIR%
echo [OK] Double-click ModbusLens.exe to launch.
exit /b 0