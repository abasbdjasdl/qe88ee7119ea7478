#!/bin/bash
# Apple toolchain build, exercised in GitHub Actions.
set -euo pipefail
cd "$(dirname "$0")"
: "${MAC_KERNEL_SDK:?Set MAC_KERNEL_SDK to the pinned MacKernelSDK checkout}"
if [[ "$(uname -s)" != Darwin ]]; then
  echo 'This step requires the Apple macOS toolchain.' >&2
  exit 1
fi
test -f "$MAC_KERNEL_SDK/Headers/IOKit/IOService.h"
test -f "$MAC_KERNEL_SDK/Library/x86_64/libkmod.a"
mkdir -p build/macos
flags=(-target x86_64-apple-macos11.0 -mkernel -DKERNEL -DKERNEL_EXTENSION
       -fno-stack-protector -mno-red-zone -I "$MAC_KERNEL_SDK/Headers")
xcrun clang++ "${flags[@]}" -std=c++14 -fno-exceptions -fno-rtti \
  -c src/RTL8852BEProbe.cpp -o build/macos/RTL8852BEProbe.o
xcrun clang "${flags[@]}" -c src/Module.c -o build/macos/Module.o
xcrun clang++ "${flags[@]}" -std=c++14 -fno-exceptions -fno-rtti \
  -Wall -Wextra -Werror -c tests/firmware_kernel_compile.cpp -o build/macos/FirmwarePlan-compile-only.o
bundle=build/macos/RTL8852BEProbe.kext
mkdir -p "$bundle/Contents/MacOS"
xcrun ld -arch x86_64 -kext -undefined dynamic_lookup \
  -o "$bundle/Contents/MacOS/RTL8852BEProbe" \
  build/macos/RTL8852BEProbe.o build/macos/Module.o \
  "$MAC_KERNEL_SDK/Library/x86_64/libkmod.a"
cp Info.plist "$bundle/Contents/Info.plist"
plutil -lint "$bundle/Contents/Info.plist"
xcrun otool -hv "$bundle/Contents/MacOS/RTL8852BEProbe"
echo 'Diagnostic prototype built. Not installed, signed, or hardware validated.'
