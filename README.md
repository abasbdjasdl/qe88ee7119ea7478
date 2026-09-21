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
- 0.0.3: build and host validation pending; physical register reads pending.
- Not implemented: firmware upload, RF initialization, DMA queues, TX/RX, scan,
  association, WPA authentication, or an IO80211 network interface.

The CI workflow builds with the pinned MacKernelSDK and Apple toolchain, tests
PCI parsing and the MMIO transaction with ASan/UBSan mocks, and validates the
Mach-O bundle. It does not load kernel code on the CI host.

References:
- https://github.com/acidanthera/MacKernelSDK/tree/05094e5e88cec7caedbfb35e8449ed0db94bf95b
- https://github.com/lwfinger/rtw89/tree/d1fced1b8a741dc9f92b47c69489c24385945f6e
  (pci.c BAR2 mapping; reg.h offsets 0xF0/0xF4; core.c chip-cut extraction)

Sources in this repository use BSD-3-Clause; referenced SDK/reference code retains
its original licenses. Firmware and the Linux wireless stack have not been ported.
