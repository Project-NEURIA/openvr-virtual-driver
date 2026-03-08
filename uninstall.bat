@echo off
setlocal enabledelayedexpansion

set "DRIVER_NAME=openvr_virtual_driver"

:: --- Detect SteamVR path ---
set "STEAMVR_PATH="

:: Method 1: openvrpaths.vrpath
set "VRPATH_FILE=%LOCALAPPDATA%\openvr\openvrpaths.vrpath"
if exist "%VRPATH_FILE%" (
    for /f "usebackq tokens=*" %%a in (`powershell -NoProfile -Command "(Get-Content '%VRPATH_FILE%' | ConvertFrom-Json).runtime[0]"`) do (
        if exist "%%a\drivers" set "STEAMVR_PATH=%%a"
    )
)

:: Method 2: Steam registry key
if not defined STEAMVR_PATH (
    for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\WOW6432Node\Valve\Steam" /v InstallPath 2^>nul') do (
        set "STEAM_DIR=%%b"
        if exist "!STEAM_DIR!\steamapps\common\SteamVR\drivers" (
            set "STEAMVR_PATH=!STEAM_DIR!\steamapps\common\SteamVR"
        )
    )
)

:: Method 3: Default Steam locations
if not defined STEAMVR_PATH (
    for %%p in (
        "C:\Program Files (x86)\Steam"
        "C:\Program Files\Steam"
        "D:\Steam"
        "D:\SteamLibrary"
    ) do (
        if not defined STEAMVR_PATH if exist "%%~p\steamapps\common\SteamVR\drivers" (
            set "STEAMVR_PATH=%%~p\steamapps\common\SteamVR"
        )
    )
)

if not defined STEAMVR_PATH (
    echo ERROR: Could not find SteamVR installation.
    echo.
    pause
    exit /b 1
)

set "DEST=%STEAMVR_PATH%\drivers\%DRIVER_NAME%"

if not exist "%DEST%" (
    echo Driver is not installed.
    echo.
    pause
    exit /b 0
)

echo Found driver at: %DEST%
echo.

:: Remove the driver
rmdir /s /q "%DEST%" 2>nul
if exist "%DEST%" (
    echo ERROR: Could not remove the driver.
    echo Please close SteamVR and try again.
    echo.
    pause
    exit /b 1
)

echo Driver uninstalled successfully.
echo.
echo Please restart SteamVR if it is running.
echo.
pause
