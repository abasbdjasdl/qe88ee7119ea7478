# RTL8852BE diagnostic driver

This is an experimental diagnostic service, not a working Wi-Fi driver.
Target: x86-64 macOS, PCI 10ec:b852, subsystem 1a3b:5470.

## Current version: 0.0.3

The device is opened, its PCI configuration is captured, and a bounded BAR2
read experiment is attempted only when the capability chain is valid, PMCSR
reports D0, bus mastering is disabled, and the assigned 1 MiB memory aperture
matches the PCI BAR. The mapping is uncached and read-only. The driver temporarily
sets only the PCI command memory-enable bit if needed, reads SYS_CFG1 twice and
SYS_STATUS1 once, releases the mapping, restores the changed bit, and reports the
before/during/after command values in IORegistry. It never writes MMIO, changes
device power state, enables DMA, installs interrupts, or loads firmware.

Zero, all-ones, 0xDEADBEEF, or inconsistent SYS_CFG1 results are not accepted as a
stable register value. A stable sample yields a candidate chip-cut field, not
proof that firmware or wireless operation works. Every skip and failure has a
separate diagnostic status. PCI reads themselves cannot guarantee freedom from
hardware or kernel faults; host tests cannot establish real MMIO compatibility.

The 0.0.2 timer/NVRAM code has been removed. No firmware variable logging remains.
The separate recovery-autolog collector has successfully saved a 0.0.1 report
on the physical machine through a direct FAT mount and automatically returned to
Windows. Keep that collector and runtime NVRAM write protection for this test.

## Evidence and remaining work

- 0.0.1: physical matching and PCI/resource capture confirmed on macOS 15.4.1.
- 0.0.2: earlier NVRAM experiment failed; retired, cause not established.
- 0.0.3: Apple build, ASan/UBSan tests and physical register reads passed on
  macOS 15.4.1. SYS_CFG1 was 0x0C491D39 twice, SYS_STATUS1 was 0x1401F278;
  PCI Command was 0 -> 2 -> 0, and automatic collection/reboot completed.
- Not implemented: firmware upload, RF initialization, DMA queues, TX/RX, scan,
  association, WPA authentication, or an IO80211 network interface.

The CI workflow builds with the pinned MacKernelSDK and Apple toolchain, tests
PCI parsing and the MMIO transaction with ASan/UBSan mocks, and validates the
Mach-O bundle. It does not load kernel code on the CI host.

References:
- https://github.com/acidanthera/MacKernelSDK/tree/05094e5e88cec7caedbfb35e8449ed0db94bf95b
- https://github.com/lwfinger/rtw89/tree/d1fced1b8a741dc9f92b47c69489c24385945f6e
  (pci.c BAR2 mapping; reg.h offsets 0xF0/0xF4; core.c chip-cut extraction)

## Firmware preparation (offline; not installed)

`src/FirmwarePlan.hpp` selects a normal CE/normal image for the exact chip-cut
field, validates the multi-image container and v0 sections, and enumerates bounded
2020-byte payload chunks. There is no allocation or OS dependency in this module.
All failures clear its output. Unsupported layouts are rejected. Dynamic feature
records remain opaque; file-layout acceptance is not firmware ABI compatibility,
cryptographic verification, hardware checksum verification or permission to DMA.

The unchanged fixture in `firmware/` is pinned by source commit and SHA-256 and
retains its separate Realtek license. Test data corruption uses synthetic data.
`tools/firmware_inspect.cpp` produces a JSON layout without writing to hardware.
CI runs ASan/UBSan tests, a bounded libFuzzer campaign, both real cut-1/cut-2
images, and a compile-only check under the kernel SDK. This module is not linked
into the live 0.0.3 driver. See `docs/firmware-bringup.md` for remaining dependencies.

Project source uses BSD-3-Clause. The firmware binary is **not** BSD-licensed;
see `firmware/LICENCE.rtlwifi_firmware.txt` and `firmware/provenance.json`.
Referenced SDK/reference code retains its original licenses. The Linux wireless
stack, firmware transport and hardware initialization have not been ported.
