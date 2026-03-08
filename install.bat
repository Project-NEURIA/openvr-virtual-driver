@echo off
setlocal enabledelayedexpansion

set "DRIVER_NAME=openvr_virtual_driver"
set "DRIVER_SOURCE=%~dp0%DRIVER_NAME%"

:: Check that the driver folder exists next to this script
if not exist "%DRIVER_SOURCE%\driver.vrdrivermanifest" (
    echo ERROR: Could not find the driver folder.
    echo Make sure "%DRIVER_NAME%" folder is in the same directory as this script.
    echo.
    pause
    exit /b 1
)

:: --- Detect SteamVR path ---
set "STEAMVR_PATH="

:: Method 1: openvrpaths.vrpath (most reliable - SteamVR's own config)
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
    echo Please make sure SteamVR is installed via Steam.
    echo.
    pause
    exit /b 1
)

set "DEST=%STEAMVR_PATH%\drivers\%DRIVER_NAME%"

echo Found SteamVR at: %STEAMVR_PATH%
echo.

:: Close SteamVR if running
tasklist /FI "IMAGENAME eq vrserver.exe" 2>nul | find /I "vrserver.exe" >nul
if not errorlevel 1 (
    echo SteamVR is running. Closing it...
    taskkill /IM vrserver.exe /F >nul 2>&1
    taskkill /IM vrmonitor.exe /F >nul 2>&1
    taskkill /IM vrdashboard.exe /F >nul 2>&1
    taskkill /IM vrcompositor.exe /F >nul 2>&1
    taskkill /IM vrwebhelper.exe /F >nul 2>&1
    timeout /t 3 /nobreak >nul
)

:: Check if driver is already installed
if exist "%DEST%" (
    echo Existing installation found. Updating...
    rmdir /s /q "%DEST%" 2>nul
    if exist "%DEST%" (
        echo ERROR: Could not remove the existing driver.
        echo A process may still be locking the files. Please try again.
        echo.
        pause
        exit /b 1
    )
)

:: Copy the driver
echo Installing %DRIVER_NAME%...
xcopy /s /e /i /q "%DRIVER_SOURCE%" "%DEST%" >nul
if errorlevel 1 (
    echo ERROR: Failed to copy driver files.
    echo.
    pause
    exit /b 1
)

echo.
echo Installation complete!
echo Driver installed to: %DEST%
echo.
pause
