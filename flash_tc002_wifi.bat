@echo off
setlocal EnableExtensions DisableDelayedExpansion
title TC002 Wi-Fi Firmware Flasher

set "ADB="
where adb >nul 2>&1
if not errorlevel 1 set "ADB=adb"
if not defined ADB if exist "%USERPROFILE%\.local\bin\adb.exe" set "ADB=%USERPROFILE%\.local\bin\adb.exe"
if not defined ADB if exist "%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe" set "ADB=%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe"

if not defined ADB (
    echo [ERROR] adb.exe was not found.
    echo Install Android Platform-Tools or add adb.exe to PATH.
    pause
    exit /b 1
)

echo 使用 ADB: %ADB%
"%ADB%" version
echo.

set "IP="
set /p "IP=Enter TC002 IP (IP:port allowed, default 192.168.1.12): "
if not defined IP set "IP=192.168.1.12"
set "IP=%IP:"=%"
echo(%IP%| findstr /r /c:":" >nul
if errorlevel 1 set "DEVICE=%IP%:5555"
if not errorlevel 1 set "DEVICE=%IP%"

set "IMAGE="
set /p "IMAGE=Enter the full path to update.img: "
set "IMAGE=%IMAGE:"=%"
if not defined IMAGE (
    echo [ERROR] No image path was entered.
    pause
    exit /b 1
)
if not exist "%IMAGE%" (
    echo [ERROR] Image file was not found: %IMAGE%
    pause
    exit /b 1
)

echo.
echo Target device: %DEVICE%
echo Image file: %IMAGE%
echo.
echo Connecting to device...
"%ADB%" connect "%DEVICE%"
if errorlevel 1 (
    echo [ERROR] ADB connect command failed.
    pause
    exit /b 1
)

set "STATE_FILE=%TEMP%\tc002_adb_state_%RANDOM%.txt"
"%ADB%" -s "%DEVICE%" get-state > "%STATE_FILE%" 2>nul
if errorlevel 1 (
    del /q "%STATE_FILE%" >nul 2>&1
    echo [ERROR] Unable to query device state.
    pause
    exit /b 1
)
set "STATE="
set /p "STATE="<"%STATE_FILE%"
del /q "%STATE_FILE%" >nul 2>&1
if /i not "%STATE%"=="device" (
    echo [ERROR] Device is not ready. Current state: %STATE%
    echo Check Wi-Fi and ADB debugging permission on the clock.
    pause
    exit /b 1
)

echo.
echo The image will be pushed and a permanent upgrade will be triggered.
choice /c YN /n /m "Continue? [Y/N] "
if errorlevel 2 (
    echo Cancelled.
    pause
    exit /b 0
)

echo.
echo [1/4] Pushing update.img...
"%ADB%" -s "%DEVICE%" push "%IMAGE%" /tmp/update.img
if errorlevel 1 (
    echo [ERROR] Image push failed.
    pause
    exit /b 1
)

echo [2/4] Setting upgrade flag...
"%ADB%" -s "%DEVICE%" shell setprop sys.zkupgrade.flag 255
if errorlevel 1 goto :upgrade_error

echo [3/4] Setting upgrade directory...
"%ADB%" -s "%DEVICE%" shell setprop sys.zkupgrade.dir /tmp
if errorlevel 1 goto :upgrade_error

echo [4/4] Restarting upgrade service...
"%ADB%" -s "%DEVICE%" shell setprop ctl.restart zkswe
if errorlevel 1 goto :upgrade_error

echo.
echo Upgrade triggered. Keep power and Wi-Fi connected; do not operate the clock.
echo After reboot, run adb devices to check whether it is online again.
pause
exit /b 0

:upgrade_error
echo [ERROR] Failed to set upgrade parameters; upgrade may not have started.
echo Check the ADB connection and device logs. Do not power off immediately.
pause
exit /b 1
