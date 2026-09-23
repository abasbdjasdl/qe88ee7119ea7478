# RTL8852BE experimental macOS driver

This repository contains an experimental network driver and its earlier
diagnostic services. The installed `120aae9` build has demonstrated association,
DHCP and target-interface HTTPS on one 2.4 GHz WPA2-Personal/CCMP network.
It exposes an Ethernet interface. Native macOS Wi-Fi registration, WPA3/OWE,
enterprise authentication and 5 GHz operation remain unfinished.
Target: x86-64 macOS, PCI 10ec:b852, subsystem 1a3b:5470.

Current authentication work adds an administrator control connection, an
observation-only authentication/EAPOL RX queue, and offline hostap-based SAE/OWE
components. The SAE body state machine includes bounded retries and deadlines;
it is not a completed radio authentication path. See
[current native/auth status](docs/native-auth-status.md) and
[authentication integration boundaries](docs/auth-integration.md).

The sections below retain the history of early bring-up candidates. Their
pending-hardware statements describe those versions, not the current baseline.

## Initial integrated networking candidate: 0.1.0 (historical)

`RTL8852BENetwork.kext` links the concrete PCI controller, persistent firmware
boot, MAC/PHY/RFK initialization, DMA/MSI queues, station tables, scan/association
and the pinned itlwm/OpenBSD WPA2 stack. The initial profile is one 2.4 GHz
station on channels 1–11, 20 MHz, legacy rates, open or WPA2-CCMP, and an Ethernet
interface for macOS IPv4/ARP/DHCP. It does not support concurrent Bluetooth,
5 GHz, WPA3, multicast/IPv6 or sleep/resume.

The first integrated build, `db988b9`, passed cloud compilation, KEXT linking and
38 component test executables. Exact source/binary hashes and unchanged embedded
vendor firmware were independently checked. This does not prove kernel loading,
RF operation, association or Internet access. Public bundles have no SSID/key.
See [network-driver.md](docs/network-driver.md) for the implementation and limits,
and [network-capture.md](docs/network-capture.md) for the staged read-only collector.

## Earlier diagnostic candidate: 0.0.13 (HCI gate ordering before BDRAM reset)

0.0.12 stopped during queue reset, with no packet submitted and successful
cleanup. 0.0.13 corrects the internal HCI gate order and adds persistent poll
failure evidence. The same boot attempts queue setup, header acceptance, full
upload, firmware-ready/drain checks and verified shutdown. Physical upload
success is not established. See [pci-firmware-upload.md](docs/pci-firmware-upload.md).

The stopped command-ring experiment passed on hardware in 0.0.7. Version 0.0.11
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
  with simulated-backend fault tests, used by the upload candidate below.
- 0.0.8: native firmware bank passed on hardware (164 packets, 165 pages, cleanup
  OK). ROM preparation stopped at phase 1 with a readback failure; ROM was not
  started. Outer supply-off and PCI restoration passed. Exact failed bits were
  not captured; the generic stop mask and unconditional cleanup were incorrect.
- 0.0.9: physical ROM_H2C_READY reached (control 0x23); WDE/PLE/HFC and bank
  checks passed. Cleanup failed only at CLK_EN readback (0xffffffff after MAC
  function disable); CPU stop, outer power-off, PCI restore and bank cleanup passed.
- 0.0.10: hardware stopped earlier at HCI disable readback (0x1000 stayed
  0x15f00); early cleanup, supply-off and memory release passed. The clock-cleanup
  change was not reached. Initial MAC function state was not captured.
- 0.0.11: establishes/verifies minimal MAC access before HCI snapshot/stop and
  restores HCI before disabling that access. A successful, cleaned-up first pass
  is followed by one confirmation pass; both passed on physical hardware.
- 0.0.12: hardware PCI stage 3/startFailed, 0 submitted; ROM, cleanup and PCI
  restore passed. BDRAM poll failure inferred from control flow/write count.
- 0.0.13: corrects internal HCI gate order before BDRAM reset and adds poll
  failure/register evidence; hardware validation pending.
- Not operational: RF initialization, live network DMA/interrupt handling, scan,
  association, WPA authentication, or a network controller interface. Offline
  queue/protocol components are described above; they are not in the live kext.

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

Diagnostic/descriptor code uses BSD-3-Clause. The new net80211 bridge is
GPL-2.0-or-later and the reused itlwm stack retains its GPL and per-file notices;
see [network-port licensing](docs/network-port.md#licensing).
The firmware binary is **not** BSD-licensed;
see `firmware/LICENCE.rtlwifi_firmware.txt` and `firmware/provenance.json`.
Referenced SDK/reference code retains its original licenses. The complete Linux
hardware backend has not been ported.

## Firmware transport development

`FirmwarePackets.hpp` constructs the chip-specific 24-byte TXWD, header command
and section packets. `FirmwareTransfer.hpp` implements bounded handshake,
publish/drain and ownership/cleanup orchestration against a backend contract.
The pinned firmware produces a 164-packet batch for each supported cut. Buffers
remain owned until DMA quiescence is proven; a consumer index alone never frees
one. The packet encoder entered the 0.0.8 candidate; the bounded hardware upload
backend entered 0.0.12. See [firmware-transport.md](docs/firmware-transport.md). There is no
working firmware upload, scan, connection or packet TX/RX implementation yet.
