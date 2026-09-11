#!/bin/bash
# Build QEMU-Portable GUI launcher (cross-compile from Linux)
cd "$(dirname "$0")"
set -e
echo "Compiling resources..."
x86_64-w64-mingw32-windres resources.rc -o resources.o
echo "Compiling main.c..."
x86_64-w64-mingw32-gcc -O2 -o QEMU-Portable.exe main.c resources.o \
    -lgdi32 -lcomctl32 -lcomdlg32 -mwindows -municode
echo "Done: QEMU-Portable.exe ($(du -h QEMU-Portable.exe | awk '{print $1}'))"
