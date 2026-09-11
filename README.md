# QEMU Portable v2.0

Portable x86_64 QEMU hypervisor for Windows with a native Win32 GUI launcher.

**34 MB self-extracting archive** - extracts to `%TEMP%\qemu-portable\` and launches a lightweight GUI for VM configuration. No installation required.

## Features

- Boot from physical drives (diskpart-based enumeration, works in WinPE)
- Boot from ISO or virtual disk images (.iso, .img, .qcow2, .vhd, .vhdx, .vdi)
- Boot ISO with extra physical or virtual disk attached
- gLiTcH iPXE netboot
- BIOS (SeaBIOS) and UEFI (OVMF) firmware support
- Configurable RAM allocation
- GTK display with full QEMU menubar (screenshot, zoom, fullscreen)
- Dark theme GUI with DPI awareness

## Usage

Download `QEMU-Portable-v2.0.exe` and run it. The SFX extracts to `%TEMP%\qemu-portable\` and launches the GUI automatically.

The app always extracts fresh on launch, overwriting any existing files in the temp directory.

## Building the GUI launcher

Cross-compile from Linux with MinGW:

```bash
cd gui-src
./build.sh
```

Requires `mingw-w64` (`apt install mingw-w64`).

The SFX is assembled by concatenating the 7z SFX stub + sfx_config.txt + the 7z payload archive.

## QEMU version

Built with QEMU 11.1.0 for Windows (August 2026).

Stripped to x86_64-only: no non-x86 firmware, minimal keymaps (en-us + sv), essential VGA BIOS and PXE ROMs only.

## License

QEMU is licensed under GPL v2. See COPYING.LIB in the qemu directory.

GUI launcher (c) 2026 Glitch Linux - https://glitchlinux.com
