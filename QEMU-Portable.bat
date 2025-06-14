@echo off
setlocal enabledelayedexpansion

:: Set console window size (columns x lines)
mode con: cols=65 lines=18

:: Detect script location
set "LAUNCHER_DIR=%~dp0"
if "%LAUNCHER_DIR%"=="" set "LAUNCHER_DIR=%CD%"
set "LAUNCHER_DIR=%LAUNCHER_DIR:\=\%"
if "%LAUNCHER_DIR:~-1%"=="\" set "LAUNCHER_DIR=%LAUNCHER_DIR:~0,-1%"

:: Set QEMU paths
set "QEMU_ROOT=%LAUNCHER_DIR%\qemu"
set "QEMU_BIN=%QEMU_ROOT%\qemu-system-x86_64.exe"
set "QEMU_IMG=%QEMU_ROOT%\qemu-img.exe"

:: Check if QEMU exists
if not exist "%QEMU_BIN%" (
    echo Error: QEMU binaries not found in %QEMU_ROOT%
    echo.
    echo Found files in directory:
    dir /b "%QEMU_ROOT%" 2>nul
    echo.
    echo Current directory: %CD%
    pause
    exit /b 1
)

:: Create necessary directories
for %%d in (images isos scripts snapshots) do (
    if not exist "%LAUNCHER_DIR%\%%d" mkdir "%LAUNCHER_DIR%\%%d"
)

:: Check for WHPX acceleration (conservative detection)
set "ACCEL_AVAILABLE=0"
set "ACCEL_TYPE=TCG"

:: Simple Windows version check
ver | findstr /C:"10.0" >nul
if !errorlevel! equ 0 set "ACCEL_AVAILABLE=1"
ver | findstr /C:"11.0" >nul  
if !errorlevel! equ 0 set "ACCEL_AVAILABLE=1"

if %ACCEL_AVAILABLE%==1 set "ACCEL_TYPE=WHPX"

:MAIN_MENU
cls
echo.
echo  ======================
echo  QEMU PORTABLE LAUNCHER
echo  ======================
echo.
if %ACCEL_AVAILABLE%==1 (
    echo  [Hardware Acceleration: %ACCEL_TYPE% Available - Use with caution]
) else (
    echo  [Hardware Acceleration: %ACCEL_TYPE% Only]
)
echo.
echo  1. Boot From Image
echo  2. Boot from Existing Disk
echo  3. Boot From Image with Disk
echo  4. Netboot.xyz
echo  5. Force TCG Mode (Disable Acceleration)
echo  0. Exit
echo.
set /p choice=Select an option (0-5): 

if "%choice%"=="1" goto BOOT_IMAGE
if "%choice%"=="2" goto BOOT_PHYSICAL
if "%choice%"=="3" goto BOOT_IMAGE_WITH_DISK
if "%choice%"=="4" goto NETBOOT_XYZ
if "%choice%"=="5" goto FORCE_TCG
if "%choice%"=="0" exit /b
goto MAIN_MENU

:FORCE_TCG
set "ACCEL_AVAILABLE=0"
set "ACCEL_TYPE=TCG (Forced)"
echo TCG mode forced. Hardware acceleration disabled.
pause
goto MAIN_MENU

:GET_VM_CONFIG
cls
echo.
echo  === VM Configuration ===
echo.
set /p ram=Enter RAM in MB (default: 2048): 
if "%ram%"=="" set ram=2048

echo.
set /p cores=Enter CPU cores (1-8, default: 2): 
if "%cores%"=="" set cores=2

echo.
echo  Firmware Type:
echo  1. BIOS (Legacy) - Default
echo  2. UEFI - Requires OVMF files
echo.
set /p fw_choice=Select firmware (1-2, default: 1): 
if "%fw_choice%"=="" set fw_choice=1

:: Set firmware options
set "FIRMWARE_OPTS="
if "%fw_choice%"=="2" (
    if exist "%QEMU_ROOT%\bios\OVMF_CODE.fd" (
        if exist "%QEMU_ROOT%\bios\OVMF_VARS.fd" (
            set "FIRMWARE_OPTS=-drive if=pflash,format=raw,readonly=on,file=%QEMU_ROOT%\bios\OVMF_CODE.fd -drive if=pflash,format=raw,file=%QEMU_ROOT%\bios\OVMF_VARS.fd"
            echo Using UEFI firmware from %QEMU_ROOT%\bios\
        ) else (
            echo OVMF_VARS.fd not found, using BIOS instead
            set fw_choice=1
        )
    ) else (
        echo OVMF_CODE.fd not found, using BIOS instead
        set fw_choice=1
    )
)
if "%fw_choice%"=="1" echo Using BIOS firmware

exit /b

:BUILD_BASE_COMMAND
:: Base options
set "BASE_OPTS=-m %ram% -smp %cores%"

:: Add acceleration
if %ACCEL_AVAILABLE%==1 (
    set "BASE_OPTS=%BASE_OPTS% -accel tcg,thread=multi -cpu qemu64"
    echo Using TCG emulation (WHPX disabled for stability)
) else (
    set "BASE_OPTS=%BASE_OPTS% -accel tcg,thread=multi -cpu qemu64"
    echo Using TCG emulation
)

:: Add firmware options if specified
if not "%FIRMWARE_OPTS%"=="" (
    set "BASE_OPTS=%BASE_OPTS% %FIRMWARE_OPTS%"
)

:: Basic system options
set "BASE_OPTS=%BASE_OPTS% -rtc base=localtime"

:: Network
set "BASE_OPTS=%BASE_OPTS% -netdev user,id=net0 -device e1000,netdev=net0"

:: Graphics
set "BASE_OPTS=%BASE_OPTS% -vga std"

exit /b

:LIST_PHYSICAL_DISKS
cls
echo.
echo  === WARNING: Physical Disk Access ===
echo  This can be dangerous! Make sure you select the correct disk.
echo.
echo  Available physical disks:
wmic diskdrive get caption,size,deviceid 2>nul || echo Could not retrieve disk information
echo.
set /p disk=Enter physical disk path (e.g. \\.\PhysicalDrive1): 
if "%disk%"=="" (
    echo No disk specified!
    pause
    goto LIST_PHYSICAL_DISKS
)
exit /b

:BOOT_PHYSICAL
call :LIST_PHYSICAL_DISKS
call :GET_VM_CONFIG
call :BUILD_BASE_COMMAND
echo.
echo Starting VM with physical disk: %disk%
echo RAM: %ram%MB, Cores: %cores%
echo.
echo Press Ctrl+Alt+G to release mouse cursor
echo.
"%QEMU_BIN%" %BASE_OPTS% -drive file="%disk%",format=raw,cache=none
if not !errorlevel!==0 (
    echo.
    echo VM exited with error. Press any key to return to menu...
    pause >nul
)
goto MAIN_MENU

:BOOT_IMAGE
cls
echo.
echo  === Boot From Image ===
echo  Supported formats: .iso (as CDROM), others as disk
echo.
echo  Available images:
dir /b "%LAUNCHER_DIR%\isos\*.*" 2>nul || echo No images found in isos folder
echo.
set /p image=Enter image filename (or full path): 
if "%image%"=="" (
    echo No image specified!
    pause
    goto BOOT_IMAGE
)

if not exist "%image%" (
    if not exist "%LAUNCHER_DIR%\isos\%image%" (
        echo Image not found!
        pause
        goto BOOT_IMAGE
    )
    set "image=%LAUNCHER_DIR%\isos\%image%"
)

call :GET_VM_CONFIG
call :BUILD_BASE_COMMAND

echo.
echo Starting VM with image: %image%
echo RAM: %ram%MB, Cores: %cores%
echo.
echo Press Ctrl+Alt+G to release mouse cursor
echo.

:: Check if file is ISO
if /i "%image:~-4%"==".iso" (
    "%QEMU_BIN%" %BASE_OPTS% -cdrom "%image%" -boot d
) else (
    "%QEMU_BIN%" %BASE_OPTS% -drive file="%image%",cache=writeback
)

if not !errorlevel!==0 (
    echo.
    echo VM exited with error. Press any key to return to menu...
    pause >nul
)
goto MAIN_MENU

:BOOT_IMAGE_WITH_DISK
cls
echo.
echo  === Boot From Image with Disk ===
echo  First select the bootable image
echo.
echo  Available images:
dir /b "%LAUNCHER_DIR%\isos\*.*" 2>nul || echo No images found in isos folder
echo.
set /p image=Enter image filename (or full path): 
if "%image%"=="" (
    echo No image specified!
    pause
    goto BOOT_IMAGE_WITH_DISK
)

if not exist "%image%" (
    if not exist "%LAUNCHER_DIR%\isos\%image%" (
        echo Image not found!
        pause
        goto BOOT_IMAGE_WITH_DISK
    )
    set "image=%LAUNCHER_DIR%\isos\%image%"
)

:DISK_TYPE_MENU
cls
echo.
echo  === Select Disk Media Format ===
echo.
echo  1. Virtual Disk [.img .vhd .qcow2]
echo  2. Physical Device [local SSD, HDD, USB, SDCX]
echo.
set /p disk_type=Select disk type (1-2): 

if "%disk_type%"=="1" goto SELECT_VIRTUAL_DISK
if "%disk_type%"=="2" goto SELECT_PHYSICAL_DISK
goto DISK_TYPE_MENU

:SELECT_VIRTUAL_DISK
cls
echo.
echo  === Available Virtual Disks ===
echo  #  Filename
echo  -------------------------------
set count=0
for /f "delims=" %%f in ('dir /b "%LAUNCHER_DIR%\images\*.*" 2^>nul') do (
    set /a count+=1
    set "vdisk!count!=%%f"
    echo  !count!. %%f
)
echo.
if %count%==0 (
    echo No virtual disks found in images folder!
    pause
    goto DISK_TYPE_MENU
)
set /p disk_num=Select disk number (1-!count!): 
if "%disk_num%"=="" goto SELECT_VIRTUAL_DISK
if !disk_num! lss 1 goto SELECT_VIRTUAL_DISK
if !disk_num! gtr !count! goto SELECT_VIRTUAL_DISK
call set "disk=%%vdisk!disk_num!%%"
set "disk=%LAUNCHER_DIR%\images\%disk%"
goto START_WITH_DISK

:SELECT_PHYSICAL_DISK
call :LIST_PHYSICAL_DISKS
goto START_WITH_DISK

:START_WITH_DISK
call :GET_VM_CONFIG
call :BUILD_BASE_COMMAND

echo.
echo Starting VM with:
echo Boot Image: %image%
echo Disk: %disk%
echo RAM: %ram%MB, Cores: %cores%
echo.
echo Press Ctrl+Alt+G to release mouse cursor
echo.

:: Set disk options based on type
set "DISK_OPTS="
echo %disk% | findstr /C:"\\.\PhysicalDrive" >nul
if !errorlevel! equ 0 (
    set "DISK_OPTS=-drive file=%disk%,format=raw,cache=none"
) else (
    set "DISK_OPTS=-drive file=%disk%,cache=writeback"
)

:: Check if boot image is ISO
if /i "%image:~-4%"==".iso" (
    "%QEMU_BIN%" %BASE_OPTS% %DISK_OPTS% -cdrom "%image%" -boot d
) else (
    "%QEMU_BIN%" %BASE_OPTS% %DISK_OPTS% -drive file="%image%",cache=writeback -boot c
)

if not !errorlevel!==0 (
    echo.
    echo VM exited with error. Press any key to return to menu...
    pause >nul
)
goto MAIN_MENU

:NETBOOT_XYZ
cls
echo.
echo  === Netboot.xyz ===
set "netboot_iso=%QEMU_ROOT%\netboot.xyz.iso"

if not exist "%netboot_iso%" (
    echo netboot.xyz.iso not found in %QEMU_ROOT%
    echo Please download it from https://netboot.xyz/downloads/
    pause
    goto MAIN_MENU
)

call :GET_VM_CONFIG
call :BUILD_BASE_COMMAND

echo.
echo Starting Netboot.xyz
echo RAM: %ram%MB, Cores: %cores%
echo.
echo Press Ctrl+Alt+G to release mouse cursor
echo.

"%QEMU_BIN%" %BASE_OPTS% -cdrom "%netboot_iso%" -boot d

if not !errorlevel!==0 (
    echo.
    echo VM exited with error. Press any key to return to menu...
    pause >nul
)
goto MAIN_MENU