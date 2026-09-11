# QEMU Portable v2.1

Portable QEMU hypervisor for Windows with a native Win32 GUI launcher. Available for x86_64 and ARM64 emulation.

| Download | Arch | Size |
|----------|------|------|
| `QEMU-Portable-v2.1-x64.exe` | x86_64 | 34 MB |
| `QEMU-Portable-v2.1-arm64.exe` | ARM64 | 36 MB |

Self-extracting archives - extract to `%TEMP%\qemu-portable\` and launch the GUI automatically. No installation required.

## Features

- Boot from physical drives (diskpart-based enumeration, works in WinPE)
- Boot from ISO or virtual disk images (.iso, .img, .qcow2, .vhd, .vhdx, .vdi)
- Boot ISO with extra physical or virtual disk attached (wizard flow)
- NetBoot XYZ iPXE network boot
- BIOS (SeaBIOS) and UEFI (OVMF/AAVMF) firmware support
- Configurable RAM allocation
- Custom boot utilities (auto-detected from `qemu/boot-utilities/`)
- GTK display with full QEMU menubar (screenshot, zoom, fullscreen)
- Dark theme GUI with DPI awareness

## Boot Utilities

Place `.iso`, `.img`, `.qcow2`, `.vhd`, `.vhdx`, or `.vdi` files in `qemu/boot-utilities/` and a "Boot Utilities" menu option appears automatically. Each file becomes a bootable button in a submenu (filename without extension as the label).

Example: `qemu/boot-utilities/refind-x64.iso` creates a "refind-x64" boot option.

## Building from Source

### Quick build

```bash
cd gui-src
./build.sh
```

### Full SFX build with compiler script

```bash
./gui-src/qemu-portable-compiler.sh
```

The compiler prompts for architecture (x64/arm64), paths, and builds the complete self-extracting package. It auto-detects its environment when placed inside the `qemu/` directory.

Requires `mingw-w64` and `p7zip-full`.

### Auto-detection

Place `qemu-portable-compiler.sh` inside your `qemu/` directory (next to the QEMU binaries). It will auto-detect the qemu directory and look for `../gui-src/` for the GUI source. Just run it and follow the prompts.

## QEMU Version

Built with QEMU 11.1.0 for Windows (August 2026). Stripped to target architecture only.

## License

QEMU is licensed under GPL v2. GUI launcher (c) 2026 Glitch Linux - https://glitchlinux.com
