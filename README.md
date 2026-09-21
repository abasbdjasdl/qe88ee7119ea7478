# RTL8852BE diagnostic driver

This is an experimental diagnostic service, not a working Wi-Fi driver.
Target: x86-64 macOS, PCI 10ec:b852, subsystem 1a3b:5470.

## Current candidate: 0.0.8 (native firmware bank and ROM test pending)

The stopped command-ring experiment passed on hardware in 0.0.7. Version 0.0.8
prepares the complete real firmware batch in 165 native IOKit pages, initializes
the DLFW DMAC/DLE/HFC blocks, resets the firmware CPU and polls the ROM H2C-ready
bit. It then stops the CPU/internal blocks, powers down and releases the bank.
No address is submitted and PCI bus mastering stays disabled; firmware upload,
RF and Wi-Fi remain unimplemented. See [firmware-bank-rom.md](docs/firmware-bank-rom.md).

## Evidence and remaining work

- 0.0.1: physical matching and PCI/resource capture confirmed on macOS 15.4.1.
- 0.0.2: earlier NVRAM experiment failed; retired, cause not established.
- 0.0.3: Apple build, ASan/UBSan tests and physical register reads passed on
  macOS 15.4.1. SYS_CFG1 was 0x0C491D39 twice, SYS_STATUS1 was 0x1401F278;
  PCI Command was 0 -> 2 -> 0, and automatic collection/reboot completed.
- 0.0.4: physical XTAL read passed: raw analog revision 0x11, one command,
  two polls in 75 us, power register unchanged, PCI Command restored to 0.
- 0.0.5: physical supply cycle passed, MAC state 0 -> 1 -> 0; on/off errors 0,
  cleanup and PCI command restoration confirmed.
- 0.0.6: physical DMA memory preparation passed: two single-segment 4096-byte
  mappings, CPU verification and cleanup OK, OS return 0; no transfer submitted.
- 0.0.7: physical stopped FWCMD ring readback/restoration passed, including
  six setup/six restore writes, supply-off and memory cleanup. No DMA submitted.
- Offline additions: 8852B firmware packet encoder and bounded transfer protocol
  with simulated-backend fault tests; no live transport backend yet.
- 0.0.8: native firmware bank and ROM preparation implemented; hardware test pending.
- Not implemented: firmware upload, RF initialization, DMA queues, TX/RX, scan,
  association, WPA authentication, or an IO80211 network interface.

The CI workflow builds with the pinned MacKernelSDK and Apple toolchain, tests
PCI parsing and the MMIO transaction with ASan/UBSan mocks, and validates the
Mach-O bundle. It does not load kernel code on the CI host.

References:
- https://github.com/acidanthera/MacKernelSDK/tree/05094e5e88cec7caedbfb35e8449ed0db94bf95b
- https://github.com/lwfinger/rtw89/tree/d1fced1b8a741dc9f92b47c69489c24385945f6e
  (pci.c BAR2 mapping; reg.h offsets 0xF0/0xF4; core.c chip-cut extraction)

## Firmware preparation

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
images, and a compile-only check under the kernel SDK. The packet parser/encoder is linked into 0.0.8 for in-memory preparation only. See `docs/firmware-bringup.md` for remaining dependencies.

Project source uses BSD-3-Clause. The firmware binary is **not** BSD-licensed;
see `firmware/LICENCE.rtlwifi_firmware.txt` and `firmware/provenance.json`.
Referenced SDK/reference code retains its original licenses. The Linux wireless
stack, firmware transport and hardware initialization have not been ported.

## Firmware transport development

`FirmwarePackets.hpp` constructs the chip-specific 24-byte TXWD, header command
and section packets. `FirmwareTransfer.hpp` implements bounded handshake,
publish/drain and ownership/cleanup orchestration against a backend contract.
The pinned firmware produces a 164-packet batch for each supported cut. Buffers
remain owned until DMA quiescence is proven; a consumer index alone never frees
one. The packet encoder is used by the 0.0.8 candidate; the transfer protocol
remains offline without an active DMA backend. See [firmware-transport.md](docs/firmware-transport.md). There is no
working firmware upload, scan, connection or packet TX/RX implementation yet.
