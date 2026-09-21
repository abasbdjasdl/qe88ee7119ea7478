# RTL8852BE PCI diagnostic prototype

**Hardware test status:** the 0.0.2 NVRAM capture experiment failed to reach the
recovery UI on the target machine and produced no report. It has been rolled
back to 0.0.1; the cause remains unresolved. Do not treat this build as a working
Wi-Fi driver or as a validated automatic-capture implementation.

`recovery-autolog/` is a separate userspace diagnostic experiment. It adds a
launchd collector to a copy of the verified recovery image while retaining the
previously bootable kernel drivers and runtime NVRAM write protection. Its CI
workflow verifies the rebuilt disk image and exercises launchd, automatic FAT
mounting and file logging on a disposable disk image. Those checks still do not
replace a physical boot test. The temporary original-image draft input is removed
after verification; the artifact contains changed chunks only.

This is an experimental **PCI diagnostic service, not a working Wi-Fi driver**.
It targets x86-64 macOS and PCI 10ec:b852, subsystem 1a3b:5470.

Implemented: device matching, read-only PCI configuration capture, bounded
capability-list parsing, and IORegistry diagnostic properties.

Version 0.0.2 adds a command-free capture experiment: 60 seconds after matching,
the service saves at most 2047 bytes to the new Apple-vendor NVRAM variable
`RTL8852BE-AutoReport`. The report contains the build's test token, the 256-byte
PCI configuration snapshot, counts of keyboard/storage services, and up to eight
IOMedia BSD names, sizes and partition UUIDs. It does not contain network names,
passwords, user files or serial numbers. It never edits boot variables or disk
partitions. A matching token already in NVRAM suppresses future writes for that
build. Readiness retries stop after five attempts over two minutes; successful
submission stops the timer. Firmware persistence must be verified after reboot.

The bounded-buffer tests run under ASan and UBSan. They cannot validate hardware,
kernel ABI compatibility, timer lifecycle on real hardware, or NVRAM persistence.
The ordinary IORegistry diagnostics remain available if automatic capture fails.

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
