# RTL8852BE diagnostic driver

This is an experimental diagnostic service, not a working Wi-Fi driver.
Target: x86-64 macOS, PCI 10ec:b852, subsystem 1a3b:5470.

## Current version: 0.0.4 (hardware test pending)

The PCI/resource/D0/no-bus-master gates and three SYS_CFG1/SYS_STATUS1 reads from
0.0.3 remain. Inside that same transaction, the new preflight captures system
isolation, power, clock and firmware-control registers. Only a stable digital
cut 1, valid system power-ready state with the analog crystal not off, and an
idle XTAL interface permit one indirect READ command for analog revision 0x41.
The exact MMIO write is 0x81000041 to BAR2 offset 0x270; the adapter exposes no
arbitrary write API. The mapping is now uncached read/write for this command.

Polling has a 50 ms monotonic deadline and a 1001-read cap. Invalid register
values, a busy interface or power not ready cause a diagnostic exit. There is
no retry, analog-register write, power-on/reset, DMA, interrupt setup or firmware
upload. The transaction unmaps BAR2 and restores the PCI memory-enable bit after
every normal return, including timeout. It does not replay the old XTAL command.
A kernel fault or stalled bus access cannot be recovered by this software timer.

IORegistry contains XtalStatus, polls/writes/elapsed time, analog revision validity,
raw power/control samples and PCI before/during/after values. The recovery collector
already captures that whole service automatically and reboots after collection.
The driver itself does not trigger a reboot or change NVRAM.

## Evidence and remaining work

- 0.0.1: physical matching and PCI/resource capture confirmed on macOS 15.4.1.
- 0.0.2: earlier NVRAM experiment failed; retired, cause not established.
- 0.0.3: Apple build, ASan/UBSan tests and physical register reads passed on
  macOS 15.4.1. SYS_CFG1 was 0x0C491D39 twice, SYS_STATUS1 was 0x1401F278;
  PCI Command was 0 -> 2 -> 0, and automatic collection/reboot completed.
- 0.0.4: bounded XTAL/power preflight implemented; physical validation pending.
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
