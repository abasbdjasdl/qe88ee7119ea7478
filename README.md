# RTL8852BE PCI diagnostic prototype

This is an experimental **PCI diagnostic service, not a working Wi-Fi driver**.
It targets x86-64 macOS and PCI 10ec:b852, subsystem 1a3b:5470.

Implemented: device matching, read-only PCI configuration capture, bounded
capability-list parsing, and IORegistry diagnostic properties.

Not implemented: firmware upload, radio initialization, DMA, TX/RX, scan,
association, WPA authentication, or a network interface. A successful build
does not establish hardware compatibility or provide Internet access.

## Build

The GitHub Actions workflow uses the standard macos-15-intel runner, pinned
MacKernelSDK and Apple clang/ld. It runs parser tests, builds the kext, validates
Mach-O architecture/type and bundle metadata, and uploads a ZIP and report.
The build never loads the kext or changes host security settings.

For a local macOS build, clone MacKernelSDK and check out
05094e5e88cec7caedbfb35e8449ed0db94bf95b, then run:

```sh
MAC_KERNEL_SDK="$PWD/MacKernelSDK" bash build_macos.sh
python3 verify_bundle.py build/macos/RTL8852BEProbe.kext
```

The bundle is unsigned and has not been tested on a physical RTL8852BE.
No EFI configuration or automatic installation is included.

## References

- https://github.com/acidanthera/MacKernelSDK
- https://github.com/lwfinger/rtw89 (reference commit d1fced1b8a741dc9f92b47c69489c24385945f6e)
- https://github.com/torvalds/linux/blob/master/drivers/net/wireless/realtek/rtw89/rtw8852be.c

The Linux wireless implementation has not been ported into this prototype.
New source in this repository is BSD-3-Clause; SDK files retain their own licenses.
