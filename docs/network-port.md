# Reusing a macOS Wi-Fi stack for RTL8852BE

This is an unfinished port. There is no installable networking kext and no
demonstrated association, DHCP lease or Internet connection. The live diagnostic
0.0.13 remains unchanged. Network components must not be packaged as a functional
driver simply because the archive compiles.

## Code reused

- `OpenIntelWireless/itlwm` at `53c51c2cdd6e4b69beb91f310d74c53422b0f8bd`:
  the existing macOS/OpenBSD net80211 state machine, packet encapsulation,
  decapsulation, station authentication, RSN/EAPOL and software ciphers. The
  upstream files are fetched at this commit and built with their GPL and
  embedded BSD/ISC notices. 47 protocol sources are unchanged; the original
  CTimeout.cpp is replaced by the API-compatible Net80211Timers.cpp with explicit
  allocation/event-source error handling. Intel hardware and firmware are excluded.
- `lwfinger/rtw89` at `d1fced1b8a741dc9f92b47c69489c24385945f6e`:
  the actual 8852B 24-byte TXWD + 24-byte TXWI construction functions and AX RXWD
  parser. `import_network_reference.py` preserves their function bodies and
  relevant structures/masks, with a small descriptor-only type adapter. This is
  not a Linux kernel compatibility layer. BSD option selected for these files.

The imported descriptor parser assumes trusted buffer lengths. The new wrapper
checks the PCI prefix offset, short/long descriptor size, shift, driver-info
overhead and payload size before exposing any slice. It uses an aligned local
descriptor copy and rejects ICV/CRC errors. It distinguishes Wi-Fi packets from
C2H and other event reports, rather than handing all DMA payloads to net80211.

`Net80211PacketBridge.cpp` hands complete Realtek frames (FCS removed) into the
existing receive state machine. TX prioritizes management traffic; Ethernet data
passes through existing encapsulation and software encryption. Packet/node
ownership is explicit. No key, association or DMA completion is invented.
Hardware-decrypted RX is rejected until CAM/key and replay metadata support
exists. The bridge must run under a controller command gate after net80211
attachment, using real channel/RSSI metadata; it does not initialize that host.

## PCI and host integration components

`Net80211PciQueue` now holds the real mbuf/node leases through both TXBD consumer
advance and the TX release report, in either order. It builds the 8852BE's
TXWD/TXWI + TXWP + 32-bit address entry, copies the frame into supplied prepared
DMA mappings, then stages the BD. It rejects overlapping mappings, unsupported
queues and mismatched completion MACID/qsel. The caller still must provide real
IOKit DMA allocations, cache sync/barriers, MMIO doorbells and interrupt dispatch.
Reusing a page ID across a hardware reset requires draining old RPQ reports;
the wire report contains no software generation number.

`PciRxAssembly` handles FS/LS segmentation with bounded packet lengths and
metadata. Separate RXQ/RPQ dispatch feeds Wi-Fi to net80211, firmware C2H to the
firmware callback, PHY reports to the PHY callback and release reports to the
appropriate TX queue. Channel/RSSI must come from the hardware PHY layer.

`FirmwareProtocol` encodes AX role/join H2C commands and validates C2H receive and
done acknowledgements. `Net80211FirmwareQueue` stages command DMA and retains
the eight most recently consumed buffers, matching upstream PCI multi-tag
requirements. A command ACK does not free those buffers or establish a link.

`PciRingSetup` programs all nine implemented queues while bus mastering,
TXHCI/RXHCI and IRQs remain disabled, verifies readback, performs bounded BDRAM
reset and provides explicit restoration. `MacPciRingIo` binds this logic to a
validated RTL8852BE BAR2 map and restricts register writes. It cannot enable DMA.
The register/BDRAM tables are checked against the pinned original driver.

`Net80211Runtime` supplies the protocol workloop/gate symbols and enforces one
owner. Teardown must run after protocol timers/tasks/nodes are drained. The timer
adapter handles allocation/add/arm failures, cancellation and stale callbacks.
There is still no IOEthernetController start/stop implementation or full radio
backend calling these components. None is wired into the installed diagnostic kext.

### Runtime DMA and MSI integration

`PciRuntime` now starts/stops the nine native queues, checks fresh 64-entry ring
addresses/counts/indices, MAC/CMAC/HCI/firmware prerequisites, masks interrupts,
and handles bounded DMA-idle proof and PCI bus-master cutoff. It publishes only
the host half of each queue index after a barrier, checks TX one-slot advance
and RX consumption bounds, and latches errors. This does not replace full MAC,
RFK, channel or PCI link initialization. Start is one-shot per queue epoch;
recovery must build a fresh epoch after stopping and draining the old one.

`MacPciRuntimeIo` supplies real, bounded BAR2/config accesses. Its writes are
limited to IRQ masks/observed W1C status, DMA control bits and host doorbells.
`R16PciInterrupts` attaches a verified MSI event source to the controller
workloop. There is no primary-interrupt driver filter. Deferred handling masks
chip interrupts, acknowledges only observed enabled causes, drains bounded
queue work, then rearms. A 1 ms timer continues budget-limited work while IRQs
remain masked; a 10 ms backstop handles coalesced/lost edges and firmware TX
completion without a dedicated enabled TX cause. HALT or stuck-DMA causes stop
the runtime and notify the controller; SER/reset/reconnect remains controller
work. Reentrant stop cannot authorize buffer release until the callback returns.

`PciQueueService` and its native `MacQueueService` binding now connect this
dispatch to all seven TX consumer ledgers, RPQ first, and RXQ second, using the
real native queue types. Each RX ring processes at most 32 descriptors per pass
and rechecks hardware indices after publishing recycled buffers. Transmit entry
points stage/synchronize the real data or firmware queue, verify its ring
identity, and publish the hardware doorbell. Controller-provided synchronous
receive callbacks still must bind packet/PHY/C2H handling and real channel/RSSI.
Ownership of the device, BAR2, workloop, all queues and these callbacks belongs
to the future controller; they must outlive the interrupt sources. Stop/detach
precedes protocol/DMA destruction. All mappings remain retained on stop failure.

Tests cover register-model TX wraparound, W1C/rearm races, each start/stop write
failure, partial bus-master activation and bounded idle failures. Actual native
MSI owner code is tested with explicit IOKit models for allocation/add/arm
failures, continuation, fatal events, stale callbacks and callback reentrancy.
The native sources compile against the pinned kernel SDK; no physical interrupt,
packet reception or network connection is established by these tests.

## Build and verification

`network-port.yml` builds the pinned protocol source and native adapters with Apple kernel
headers. It produces a static archive and a relocatable-link check, verifies
that protocol/crypto/timer/host references are resolved, and reports remaining kernel
imports. It is not a loadable kext. Source and licenses accompany the archive.
`network_descriptors_test.cpp` checks golden TX words, unaligned RX, every buffer
truncation, invalid lengths/fields, corruption flags, C2H separation and 100000
malformed-buffer cases under ASan/UBSan. These are software evidence only.
Additional tests exercise 100000 data ownership cycles, 100008 command-retention
cycles, fragmented/truncated RX, H2C/C2H golden packets, every one of the 37 ring
setup write failures, failed rollback and DMA/IRQ gates. Timer error paths use an
explicit IOKit model; they are not a claim of running timer tests inside macOS.

## Remaining real integration

`MacDmaBuffer` now provides native wired allocation, per-device IOMMU mapping,
32-bit single-segment validation, directional synchronization and staged cleanup.
Allocation/prepare is rejected inside the supplied workloop gate. A possibly
device-visible buffer cannot be ordinarily freed; confirmed hardware shutdown is
required. Failed unmapping keeps the allocation for cleanup retry. The destructor
retains resources and logs if shutdown/unmapping was not confirmed, avoiding DMA
use-after-free. That is an error containment measure, not a substitute for a
controller stop implementation. `DataMapping.physical` is an IOVM bus address.

`MacTxDmaQueue` and `MacFirmwareDmaQueue` now connect these allocations to the
existing data/RPQ and firmware/multi-tag ledgers. Allocation occurs outside the
gate, protocol attachment/staging under the gate; data and firmware buffers are
fully preallocated. Data staging reports the actual WD page (not the BD index),
then synchronizes frame, WD and BD in that order before exposing a producer for
the caller's doorbell. A sync failure retains committed ownership, faults the
bank and prevents subsequent staging. Cleanup reclaims protocol leases only
after the controller has confirmed DMA stop. Tests compile all three actual
native source files with IOKit/mbuf models, check packet bytes and completion
ownership, inject all three sync failures and all 194 bank allocation failures.

`RxDmaQueue<MacDmaBuffer>` allocates a 64-entry RX ring and 64 buffers, writes the
original 8-byte RTL8852B RX BDs, processes a bounded batch using the hardware
producer index, synchronizes each received/recycled buffer and returns a host
consumer to publish. RXQ and RPQ require separate instances. The callback is
synchronous and cannot retain a DMA pointer. Malformed sizes are dropped; any
sync failure latches a fault until DMA has stopped, preventing callback replay.
Native adapters compile against the pinned kernel SDK. Tests compile the actual
DMA implementation against an explicit IOKit model and inject allocation,
prepare, mapping, sync and cleanup errors; RX tests cover all 65 allocation
failures and 100,000 wrap/recycle operations. These are not live DMA tests.

The radio port now contains the pinned RTL8852B BB, RF A/B, NCTL and gain tables,
original packed eFuse layout, bounded DDV/DAV decoder, board/PHY calibration
parsing, AX RF v1 direct/SWSI access, RF firmware pages, NCTL handshake and
thermal/PA trim stages. `BasebandGain` decodes the signed receive gain and
bandwidth/subchannel compensation tables with preflight bounds checks; tests
compare both the complete gain table and every supported record form against
the original upstream decoder. `MacRadioIo` supplies actual kernel MMIO access through
validated BAR2; AX PHY offsets include the required `0x10000` base. RF v1 treats
`0xf9..0xfe` as register addresses, unlike the BB delay opcodes.

`RadioInitialization` exposes separate programming stages, not a complete radio
startup. It validates tables and firmware-page capacity before writes, stops on
I/O errors/cancellation, bounds SWSI/NCTL polling and exposes RF firmware pages
only after all writes and the final SWSI drain succeed. Partial hardware writes
are not rolled back: the controller must quiesce/reset the chip before retrying.
Caller-owned firmware-page storage must stay alive until the command queue has
copied it and obeys that queue's DMA ownership rules. No stage enables DMA or
claims RF calibration/association has completed.

The new radio test compares 6,400 complete table traces against the unaltered
pinned upstream selection/execution functions. It also checks 100,000 malformed
eFuse banks, exact RF register/firmware bytes, bank/output bounds, package
selection, interrupted writes, timeout/cancellation, NCTL gating and trim data.
These are host tests with a register model, not hardware evidence. The table
importer verifies the upstream commit and records hashes, and CI checks that
regeneration produces the committed files unchanged.

`EfuseReader` now reads the main DDV bank and PHY calibration window through
`MacEfuseIo`. The adapter permits only read commands and the required rail/burst
control bits. It acquires a quiescent rail, applies the cut-A burst workaround,
enforces per-byte and total deadlines, handles cancellation and attempts every
cleanup step even after errors. Readback must confirm restoration before output
is marked valid. Native I/O stays available during cancellation for cleanup;
the owner must retain the mapping until the operation returns. The register-model
test injects failure at all 58 read/write points plus delay failures, stale clocks,
ignored cleanup writes and device removal. This has not yet read this device's
physical OTP in macOS. The DAV/XTAL bank read path is still missing.

`MacInitialization` now imports 60 original AX BB/RF/system/DMAC/CMAC/IMR/report
functions and 598 referenced constants from the pinned rtw89 source. The SCC
sequence programs DLE sizes and quotas, HFC pages, scheduler, MPDU/security,
CMAC0, internal error masks and PCIe host release reports. It explicitly disables
hardware packet crypto while net80211 owns encryption. Its five one-shot stages
are `enableRadio`, `enableSystem`, `initializeDmac`, `initializeCmac`, and `finishTrx`; callers
must check every return and require `trxReady` before runtime DMA startup.

`MacInitializationIo` performs actual 8/16/32-bit BAR2 access and uses kernel
timing. The controller must retain the device/map and serialize its use. Memory
decoding must be enabled, bus mastering off, all host engines/channels stopped,
PCI interrupts masked and firmware state 7 before setup. Internal MAC error
masks are distinct from the still-masked PCI MSI sources. Failure latches the
first address/error and suppresses further accesses; partial setup requires a
controller-owned power cycle, not reuse of the failed object. BB/RF enable uses
the original AX XTAL serial write/read handshake, refuses an outstanding command,
and verifies both RFC banks plus the BB reset/PHY cycle configuration. This is
normal post-firmware MAC setup, not firmware upload, RFK, PCIe post-init,
or a top-level controller. The object has a 500 ms total setup deadline.

The host MAC test covers 366 read/write failure points, XTAL/DLE/scheduler/CAM
timeouts, frozen/backward clocks, cancellation, invalid order, ignored release
report/error mask writes, and SCC quota/filter golden values. All-ones is a
valid value for the two global error masks; it remains rejected as an invalid
read elsewhere. Generated functions/tables are reproducible and hash-recorded;
CI checks regeneration and compiles the native adapter with the protocol stack.
These are model/compile checks, with no physical MAC/RFK or network evidence.

`rfk::Initialization` imports the original dependency closure for initial DPD
backoff, both-path RCK, DRCK/ADDCK/DACK and RXDCK: 22 functions, 13 RFK command
tables, and 127 referenced constants. It retains per-path ADC/DAC calibration
values, MSBK arrays and original success-path restoration. Unlike the upstream
warning-only timeout paths, a timeout latches an error, suppresses subsequent
normal I/O and cannot expose `dack_done` as valid. Partially programmed hardware
requires a controller power cycle; the engine does not claim full rollback.
The two-second deadline and independent operation/poll bounds also handle
frozen clocks. All calibration tables retain the original operation order.

`MacRfkIo` connects this engine to native `RadioAccess<MacRadioIo>` and the
single MAC PHYREG write required by AFE setup. Borrowed device/map and control
callback owner must remain alive while a lease is outstanding. Every stage
requires explicit controller calibration begin/end callbacks; absent callbacks
are rejected. Those callbacks must coordinate firmware/BT and acknowledge the
firmware scheduler pause. They are **not implemented by this component**.
Once a lease is acquired, the adapter independently verifies firmware state 7,
CMAC enable, BB reset-enable bits and zero scheduler TX mask. Firmware readiness
requires scheduler control through the firmware register-message protocol,
not a blind write to CTN_TXEN. The driver must program BB/RF/NCTL tables and
device-specific calibration data before invoking this engine. On failure,
end callbacks still run, must leave TX disabled, and may report incomplete
cleanup; the borrowed owner cannot be destroyed while its lease remains active.

Host tests verify calibration result arrays, RF state and DPD backoff values,
all 1,070 I/O failures, every delay failure, all begin/end/drain failures,
RCK/DRCK/ADDCK/DACK timeouts, cancellation and clock anomalies. These are register
models, not real calibration measurements. Channel IQK/TSSI/DPK, RF tracking,
firmware/BT control callbacks and controller lifecycle remain unimplemented;
initial calibration success is not full RFK or network readiness.

1. Complete and preserve the RTL8852B power/MAC/PHY/RF/efuse/calibration sequence;
   the diagnostic subset currently shuts the chip down after probing. Physical
   DAV eFuse reads, applying gain state to channel registers, full BB reset/TX power
   and RFK remain.
2. Connect the new RXQ/RPQ/data/management/firmware queue components to native
   allocation and cache synchronization adapters, hardware start/stop, interrupts and recovery.
   The one-shot diagnostic CH12 bank remains separate from this runtime path.
3. Adapt rtw89 firmware commands, event dispatch, channel/power/regulatory data,
   station/address CAM and association transitions to the net80211 callbacks.
4. Add IOEthernetController lifecycle using the new workloop/timer binding, statistics,
   control client for scan/join, key handling, disconnect and suspend/resume.
5. Verify on this hardware: firmware execution, scan results, real AP
   authentication, EAPOL exchange, DHCP, packet TX/RX and connection stability.

The missing hardware backend is substantial. Imported protocols and successful
compilation do not establish an operating radio or a working Internet connection.
No new reboot test is requested for these component-only changes.

## Licensing

Existing diagnostic and descriptor-wrapper code remains BSD-3-Clause. The
net80211 bridge is GPL-2.0-or-later, and an eventual combined binary must comply
with the upstream itlwm GPL license and retained per-file notices. See
`src/network/COPYING.itlwm`. The firmware keeps its separate Realtek license.
