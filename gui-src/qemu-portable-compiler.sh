#!/bin/bash
# ============================================================
# qemu-portable-compiler.sh - Build QEMU Portable SFX packages
#
# Can run from anywhere, or auto-detect when placed inside
# the qemu/ directory alongside the binaries.
#
# Requires: mingw-w64, p7zip-full (7z)
#
# (c) 2026 Glitch Linux - https://glitchlinux.com
# ============================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
GREY='\033[1;30m'
BOLD='\033[1m'
NC='\033[0m'

banner() {
    echo ""
    echo -e "${CYAN}============================================${NC}"
    echo -e "${BOLD}  QEMU Portable Compiler${NC}"
    echo -e "${GREY}  Build self-extracting QEMU launcher${NC}"
    echo -e "${CYAN}============================================${NC}"
    echo ""
}

die() { echo -e "${RED}Error: $1${NC}" >&2; exit 1; }
ok()  { echo -e "${GREEN}$1${NC}"; }

check_deps() {
    command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 || die "mingw-w64 not found. Install with: apt install mingw-w64"
    command -v x86_64-w64-mingw32-windres >/dev/null 2>&1 || die "mingw-w64 windres not found."
    command -v 7z >/dev/null 2>&1 || die "7z not found. Install with: apt install p7zip-full"
}

# Auto-detect environment: if this script is inside a qemu/ dir
# that contains a gui-src/ sibling
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
AUTO_QEMU=""
AUTO_GUI=""

if [ -f "$SCRIPT_DIR/qemu-system-x86_64.exe" ] || [ -f "$SCRIPT_DIR/qemu-system-aarch64w.exe" ]; then
    # Script is inside the qemu/ dir
    AUTO_QEMU="$SCRIPT_DIR"
    if [ -d "$SCRIPT_DIR/../gui-src" ]; then
        AUTO_GUI="$SCRIPT_DIR/../gui-src"
    fi
elif [ -d "$SCRIPT_DIR/qemu" ] && [ -d "$SCRIPT_DIR/gui-src" ]; then
    # Script is in the project root alongside qemu/ and gui-src/
    AUTO_QEMU="$SCRIPT_DIR/qemu"
    AUTO_GUI="$SCRIPT_DIR/gui-src"
fi

banner
check_deps

# ---- Architecture selection ----
echo -e "${BOLD}Select target architecture:${NC}"
echo "  1. x86_64 (AMD64)"
echo "  2. ARM64 (AArch64)"
echo ""
read -p "Enter 1 or 2 > " ARCH_CHOICE
echo ""

case "$ARCH_CHOICE" in
    1) ARCH="x64";   ARCH_DEFINE=""; QEMU_BIN="qemu-system-x86_64.exe" ;;
    2) ARCH="arm64"; ARCH_DEFINE="-DARCH_ARM64"; QEMU_BIN="qemu-system-aarch64w.exe" ;;
    *) die "Invalid choice. Enter 1 or 2." ;;
esac

ok "Architecture: $ARCH"
echo ""

# ---- Path to qemu source directory ----
if [ -n "$AUTO_QEMU" ] && [ -f "$AUTO_QEMU/$QEMU_BIN" ]; then
    echo -e "${GREY}Auto-detected qemu directory: $AUTO_QEMU${NC}"
    read -p "Path to 'qemu' source directory [$AUTO_QEMU] > " QEMU_DIR
    QEMU_DIR="${QEMU_DIR:-$AUTO_QEMU}"
else
    read -p "Path to 'qemu' source directory > " QEMU_DIR
fi

[ -d "$QEMU_DIR" ] || die "Directory not found: $QEMU_DIR"
[ -f "$QEMU_DIR/$QEMU_BIN" ] || die "$QEMU_BIN not found in $QEMU_DIR"
ok "QEMU directory: $QEMU_DIR"
echo ""

# ---- Path to gui-src ----
if [ -n "$AUTO_GUI" ] && [ -f "$AUTO_GUI/main.c" ]; then
    echo -e "${GREY}Auto-detected gui-src directory: $AUTO_GUI${NC}"
    read -p "Path to 'gui-src' directory [$AUTO_GUI] > " GUI_DIR
    GUI_DIR="${GUI_DIR:-$AUTO_GUI}"
else
    read -p "Path to 'gui-src' directory > " GUI_DIR
fi

[ -d "$GUI_DIR" ] || die "Directory not found: $GUI_DIR"
[ -f "$GUI_DIR/main.c" ] || die "main.c not found in $GUI_DIR"
[ -f "$GUI_DIR/resources.rc" ] || die "resources.rc not found in $GUI_DIR"
[ -f "$GUI_DIR/qemu.ico" ] || die "qemu.ico not found in $GUI_DIR"
ok "GUI source: $GUI_DIR"
echo ""

# ---- Output path ----
DEFAULT_OUT="$(pwd)/QEMU-Portable-$ARCH.exe"
read -p "Path to save .exe [$DEFAULT_OUT] > " OUT_PATH
OUT_PATH="${OUT_PATH:-$DEFAULT_OUT}"
echo ""

# ---- SFX stub ----
# Must use 7zSD.sfx (supports RunProgram in sfx_config.txt)
# The generic 7z.sfx only shows an "Extract to" dialog - NOT what we want
SFX_STUB=""
if [ -f "$GUI_DIR/7zSD.sfx" ]; then
    SFX_STUB="$GUI_DIR/7zSD.sfx"
elif [ -f "$SCRIPT_DIR/7zSD.sfx" ]; then
    SFX_STUB="$SCRIPT_DIR/7zSD.sfx"
fi

if [ -z "$SFX_STUB" ]; then
    echo ""
    echo -e "${RED}7zSD.sfx not found!${NC}"
    echo -e "${GREY}The generic 7z.sfx stub only shows an extract dialog.${NC}"
    echo -e "${GREY}7zSD.sfx is required for auto-extract + auto-launch.${NC}"
    echo ""
    echo -e "Place 7zSD.sfx in: $GUI_DIR/"
    echo -e "Download from: https://github.com/nicenemo/7zip-extra/releases"
    die "Missing 7zSD.sfx - cannot build auto-launching SFX."
fi

ok "SFX stub: $SFX_STUB"

# ---- SFX config ----
SFX_CONFIG="$GUI_DIR/sfx_config.txt"
if [ ! -f "$SFX_CONFIG" ]; then
    # Create default config
    SFX_CONFIG="/tmp/qemu_sfx_config.txt"
    cat > "$SFX_CONFIG" << 'SFXEOF'
;!@Install@!UTF-8!
Title="QEMU Portable"
ExtractPathText="Extracting QEMU Portable..."
ExtractPathTitle="QEMU Portable"
ExtractDialogText="Extracting files..."
InstallPath="%TEMP%\\qemu-portable"
OverwriteMode="2"
RunProgram="QEMU-Portable.exe"
;!@InstallEnd@!
SFXEOF
    echo -e "${GREY}Created default SFX config${NC}"
fi

# ---- Check for boot-utilities ----
BU_DIR="$QEMU_DIR/boot-utilities"
BU_COUNT=0
if [ -d "$BU_DIR" ]; then
    BU_COUNT=$(find "$BU_DIR" -maxdepth 1 -type f \( -name "*.iso" -o -name "*.img" -o -name "*.qcow2" -o -name "*.vhd" -o -name "*.vhdx" -o -name "*.vdi" \) 2>/dev/null | wc -l)
    if [ "$BU_COUNT" -gt 0 ]; then
        ok "Found $BU_COUNT boot utilities in $BU_DIR"
        find "$BU_DIR" -maxdepth 1 -type f \( -name "*.iso" -o -name "*.img" -o -name "*.qcow2" \) -printf "  - %f\n" 2>/dev/null
    fi
fi

echo ""
echo -e "${CYAN}Building...${NC}"
echo ""

# ---- Build temp directory ----
BUILD_DIR=$(mktemp -d /tmp/qemu-portable-build.XXXXXX)
trap "rm -rf '$BUILD_DIR'" EXIT

# ---- Compile GUI launcher ----
echo -n "  Compiling resources... "
cp "$GUI_DIR/qemu.ico" "$BUILD_DIR/"
cp "$GUI_DIR/resources.rc" "$BUILD_DIR/"
(cd "$BUILD_DIR" && x86_64-w64-mingw32-windres resources.rc -o resources.o)
ok "done"

echo -n "  Compiling main.c ($ARCH)... "
x86_64-w64-mingw32-gcc -O2 $ARCH_DEFINE \
    -o "$BUILD_DIR/QEMU-Portable.exe" "$GUI_DIR/main.c" "$BUILD_DIR/resources.o" \
    -lgdi32 -lcomctl32 -lcomdlg32 -mwindows -municode
ok "done ($(du -h "$BUILD_DIR/QEMU-Portable.exe" | awk '{print $1}'))"

# ---- Assemble package tree ----
echo -n "  Assembling package tree... "
TREE_DIR="$BUILD_DIR/tree"
mkdir -p "$TREE_DIR"
cp "$BUILD_DIR/QEMU-Portable.exe" "$TREE_DIR/"

# Copy entire qemu directory
cp -a "$QEMU_DIR" "$TREE_DIR/qemu"

# Remove the compiler script from the package if it exists
rm -f "$TREE_DIR/qemu/qemu-portable-compiler.sh"

ok "done ($(du -sh "$TREE_DIR" | awk '{print $1}'))"

# ---- Create 7z archive ----
echo -n "  Compressing (LZMA2)... "
(cd "$TREE_DIR" && 7z a -t7z -m0=LZMA2 -mx=7 -ms=on "$BUILD_DIR/payload.7z" * > /dev/null 2>&1)
ok "done ($(du -h "$BUILD_DIR/payload.7z" | awk '{print $1}'))"

# ---- Assemble SFX ----
echo -n "  Creating SFX executable... "
cat "$SFX_STUB" "$SFX_CONFIG" "$BUILD_DIR/payload.7z" > "$OUT_PATH"
ok "done"

# ---- Verify ----
echo -n "  Verifying archive... "
7z t "$OUT_PATH" > /dev/null 2>&1 && ok "OK" || die "Archive verification failed!"

echo ""
echo -e "${CYAN}============================================${NC}"
echo -e "${GREEN}  Build complete!${NC}"
echo ""
echo -e "  Output: ${BOLD}$OUT_PATH${NC}"
echo -e "  Size:   $(du -h "$OUT_PATH" | awk '{print $1}')"
echo -e "  Arch:   $ARCH"
[ "$BU_COUNT" -gt 0 ] && echo -e "  Boot utilities: $BU_COUNT"
echo -e "${CYAN}============================================${NC}"
echo ""
