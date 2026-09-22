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
backoff, both-path RCK, DRCK/ADDCK/DACK, RXDCK, channel IQK/TSSI/DPK and thermal
tracking and scan power restoration: 140 functions, 51 RFK command tables,
43 arrays and 504 referenced
constants. It retains per-path ADC/DAC calibration
values, MSBK arrays and original success-path restoration. Unlike the upstream
warning-only timeout paths, a timeout latches an error, suppresses subsequent
normal I/O and cannot expose `dack_done` as valid. Partially programmed hardware
requires a controller power cycle; the engine does not claim full rollback.
The two-second deadline and independent operation/poll bounds also handle
frozen clocks. All calibration tables retain the original operation order.

`MacRfkIo` connects this engine to native `RadioAccess<MacRadioIo>` and the
single MAC PHYREG write required by AFE setup. Borrowed device/map and control
callback owner must remain alive while a lease is outstanding. Every stage
requires explicit controller calibration begin/end/recover callbacks; absent callbacks
are rejected. Those callbacks must coordinate firmware/BT and acknowledge the
firmware scheduler pause. They are **not implemented by this component**.
Once a lease is acquired, the adapter independently verifies firmware state 7,
CMAC enable, BB reset-enable bits and zero scheduler TX mask. Firmware readiness
requires scheduler control through the firmware register-message protocol,
not a blind write to CTN_TXEN. The driver must program BB/RF/NCTL tables and
device-specific calibration data before invoking this engine. On failure,
end callbacks still run, must leave TX disabled, and may report incomplete
cleanup; the borrowed owner cannot be destroyed while its lease remains active.
The adapter records possible hardware modification before issuing a write.
After partial failure, `recover` must verify hardware reset/quiescence while
TX/BT ownership is retained, before an outstanding oneshot STOP or parent
release. A failed recovery keeps the lease; successful recovery is not repeated
if a subsequent bookkeeping release needs retry. This callback is a mandatory
controller contract, not a reset implemented by clearing a completion bit.

Host tests verify calibration result arrays, RF state and DPD backoff values,
all 1,070 I/O failures, every delay failure, all begin/end/drain failures,
RCK/DRCK/ADDCK/DACK timeouts, cancellation and clock anomalies. These are register
models, not real calibration measurements. Physical tuning/power control,
firmware/BT control callbacks and controller lifecycle remain unimplemented;
initial calibration success is not full RFK or network readiness.

`calibrateIq()` executes both IQK paths for 2.4 GHz 20/40 MHz and 5 GHz
20/40/80 MHz after initial calibration. It repeats RXDCK on each programmed
channel before starting IQK, with a separate required calibration lease.
The caller must already have programmed
the requested center channel and bandwidth; this API does not tune the radio.
It saves/restores the upstream BB/RF register sets, selects the two coefficient
banks across channel changes and records TX/RX CFIR and LOK results. Geometry
validation is not regulatory permission to use a channel. Every operation has
its own two-second/200,000-I/O budget, rather than exhausting a lifetime quota
during ordinary channel changes.

LOK coarse/fine and both VBUFFER failure reports participate in the existing
three-attempt retry, including reports ignored upstream. Terminal LOK failure,
any TX/RX group failure and the restore command's error prevent `iqReady`.
Transport faults suppress subsequent normal I/O; this is not a hardware rollback.
Per-path coexistence oneshot notifications are mandatory native callbacks,
with faulted STOP deferred to parent cleanup so hardware recovery precedes the
notification. Parent cleanup retries an outstanding STOP before releasing
control. Controller BT policy is still
missing, so this component cannot yet run as an independent network driver.

IQK model tests cover both bands/all supported widths, restoration of all saved
BB/RF values, 1,001 read/write failures, 66 delay failures, command failures,
LOK out-of-range measurements, all four oneshot notification failures,
parent lease failures, cancellation, bounded timeout/clock anomalies, invalid
channels and 512 successive calibrations. Native code also compiles for the
x86_64 macOS kernel. These tests supply synthetic register responses, not
measured RF calibration or over-the-air results.

`calibrateTssi()` implements both-path RF/system/BB power setup, HE-TB setup,
DCK, thermal compensation, DAC/slope setup, measured two-point alignment,
tracking enable and per-channel eFuse/trim DE values. It requires successful IQK
for the currently programmed channel and immutable device calibration supplied
before initialization. `configureCalibration()` consumes decoded board/PHY
calibration plus signed BB offset/RSSI bases captured before channel gain writes,
and the selected RX antenna. The controller must validate the source data and
channel/power permissions; it must not invent these inputs. The large per-channel
coefficient state belongs on the heap, not the kernel stack.

The TSSI dependency closure includes actual RTL8852B PMAC PLCP programming,
TX/RX path selection, gain-offset application, BT-sharing register settings,
power/packet programming and register restoration from rtw8852b.c. Thermal
swing tables and the tracking configuration come from the pinned chip tables.
Both CW reports drive alignment offsets and cache entries. Missing reports are
errors; a default table is not treated as a measured alignment. Cached alignment
is reused only for that channel and the same immutable device data. C++ signed
gain shifts and thermal byte packing use defined arithmetic with matching bits.

Scheduler TX stays paused for the whole TSSI lease, including both paths. Before
PMAC programming, `armCalibrationTx()` records potential emission. The native
`stopCalibrationTx()` independently clears both packet/continuous emission bits
with BAR2 read-modify-write and readback; this restricted cleanup works even
after cancellation or a calibration I/O failure. Unknown/failed stop retains
ownership and prevents coexistence STOP. Parent cleanup retries emission stop
before releasing either lease. Other failed register restoration still requires
a power cycle. This is a component contract, not an implemented controller/BT
policy or a complete network kext.

TSSI tests exercise 2,016 normal I/O failure positions, eight successful-path
delay failures, both paths/all thermal subbands, signed eFuse/gain values,
two-point output arithmetic, cached reuse, missing thermal data, report timeouts,
clock/cancel/parent/oneshot failures, all four arm/stop failures and persistent
stop failure. A separate test compiles the actual MacRfkIo/MacRadioIo sources
against explicitly modeled IOKit/MMIO to verify cancellation cleanup, invalid
reads/readback, ignored stop writes, absent PCI memory access, and retained
leases. Both suites are host evidence, not real RF or radio power measurements.

`calibrateDpk()` requires current-channel IQK/TSSI, and executes both DPK paths
with the pinned driver's AFE/KIP/BB/RF backup and restore, RXDCK, AGC, loopback
IQK, IDL/MPA and coefficient programming. Each NCTL command must pass both
completion stages; missing completion, bad correlation/DC, exhausted AGC search
or absent baseline thermal measurement prevents `dpkReady`. Reaching a valid
TXAGC bound is a source-defined converged result, distinct from exhausting the
search. The 8852B source has no FEM setup callback and sets no EPA flags, so this
port performs DPK rather than inventing an external-amplifier bypass.

`trackDpk()` reads the chip's actual per-path thermal registers, maintains the
same four-fractional-bit/quarter-weight average as the pinned driver, and applies
the DPD power-scale update. Missing initial samples cause no compensation write.
Thermal deltas use wide signed arithmetic for the six-bit sensor range; the
initial power difference is reset per path to avoid cross-path contamination.
A channel change invalidates DPK/TSSI readiness. The top-level controller still
must schedule tracking, tune the radio and implement firmware/BT coordination.

DPK model checks include all 540 normal I/O and 22 delay failure positions,
both completion timeouts, 2G/5G bandwidth-dependent programming, DC/correlation,
gain bounds/search exhaustion, absent thermal readings and lease/cancel/clock
faults. Tracking checks cover 26 I/O failures, cancellation, different path
temperatures, averaged samples, zero samples and both extremes of the sensor
range. Native tests exercise required recovery, retained ownership on reset
failure and retry without a second reset. These are modeled device responses;
no RF linearization, radio emission or real-chip calibration is established.

The scan RFK path uses the imported `rtw8852b_tssi_scan`,
`rtw8852b_wifi_scan_notify` and their dependency closure. Normal TSSI now uses
the same imported outer function; the native parent lease holds scheduler TX
paused for both paths instead of resuming it between paths. PMAC stop and
measured-result validation still precede coexistence release.

`beginScan()` requires an IQ-calibrated home channel and immutable device
calibration. If TSSI is not initialized, it executes full measured TSSI under
the ordinary TSSI lease first. It saves home calibration validity and clears
normal IQ/TSSI/DPK readiness while scanning. `prepareScanChannel()` configures
TSSI RF/system/thermal/eFuse data for each visited channel and restores measured
band alignment when available, otherwise the source's default alignment. It
never labels a default as measured calibration and does not run PMAC on every
scan hop. `scanReady` only records completion of this RFK programming step.

`finishScan()` accepts only the saved home channel/bandwidth, reapplies its
TSSI settings and scan-end offset/enable sequence, restores alignment and then
restores home readiness. IQK/DPK coefficient state is preserved through scan;
a scan that began without DPK does not manufacture DPK readiness afterward.
Any programming/cleanup failure retains the error and prevents normal-TX
readiness. This is not hardware channel verification: the controller must first
retune the radio and apply channel power, and separately handle scan BT policy,
CAM/MAC identity, probes, dwell timers, regulatory restrictions and results.

Scan model tests cover all four thermal bands, measured/default alignment,
selected IQK/DPK register preservation, cold/warm TSSI setup, invalid transitions
and incorrect home channels, all 260 hop and 288 home-restoration I/O failures,
cancellation, lease/clock faults and 128 repeated scans. The native adapter test
checks that a scan lease cannot arm PMAC and retains ownership after failed
recovery. These do not demonstrate discovery of an access point.

`firmware::Mailbox` now implements the AX register-message channel used for
firmware-acknowledged scheduler pause/resume. It writes four H2C words, increments
the low host-counter nibble, triggers firmware, captures all four C2H words,
acknowledges the response and increments the high counter nibble. Counter updates
read the existing byte so the other nibble is preserved. Request function/length
fields and padding are encoded explicitly; response lengths outside 1..4 words
are rejected. Response ACK/sequence metadata is retained, not interpreted as an
undocumented request correlation guarantee. A scheduler operation requires the
TX_PAUSE_RPT response type and matching CTN_TXEN readback. Pause saves the prior
SCC mask; resume requires the same mailbox's successful pause and zero current
mask. Generic requests cannot bypass scheduler ownership with command ID 5.

All access requires the owner's workloop gate. Pending replies are preserved
before a new send: `staleReply` is a nonfatal refusal, allowing an explicit
`receivePending()` before retry. No request was written in that case. Other
I/O/protocol/timeout failures latch the mailbox until a new firmware epoch;
there is no retry that could consume a late reply from the failed transaction.
Captured unexpected replies remain in the result for controller diagnostics.
The H2C wait is bounded to 5 ms and the C2H wait to 1 second, with finite polling
and clock checks. This startup/control path blocks its calling workloop while
waiting for firmware MMIO; it is not an interrupt-filter API.

`MacMailboxIo` supplies actual BAR2 access, validates 10ec:b852 and the mapping,
and permits only mailbox words/control/counter writes; it cannot directly write
CTN_TXEN. The controller must own one mailbox per device and keep all borrowed
objects alive. Host tests cover 42 pause/resume I/O failure points, all payload
sizes and 256 counter starts, stale/malformed/unexpected responses, missing
firmware processing, frozen/backward clocks, cancellation and gate/reentry.
The reference checker verifies registers, counter masks, command IDs and bit
layouts against pinned rtw89. The transport is not yet wired to RFK begin/end:
firmware/BT coexistence handling and the controller are still required.

### Channel programming

`ChannelProgramming<MacRfkIo>` implements the pinned chip's MAC, baseband and
both-path DAV/DDV RF channel sequence. The separate importer retains hashes
for 37 source functions, five arrays and 137 additional constants, sharing the
same pinned RFK numeric definitions. It includes MAC bandwidth/subcarrier/rate
checks, primary-channel geometry, SCO/CCK, gain and RXSC compensation, 5 MHz
masks, baseband resets, RF band/bandwidth programming and PLL recovery.
Configuration copies this device's parsed BB gain and eFuse data plus captured
gain bases and RX antenna; it must be allocated off the kernel stack.

`program()` validates center/primary channel and 20/40/80 MHz geometry before
acquiring the required firmware/BT scheduler lease. It disables PPDU reporting,
TSSI tracking and ADC, asserts BB reset, and checks that quiescent settings read
back before reprogramming. Both RF register banks/paths and critical MAC/BB
channel settings are read back. The PLL path retains upstream recovery attempts
but fails if the final lock indication is still absent; busy timeout also latches
failure instead of continuing from a warning.

Successful programming leaves the lease held and reports `prepared`. The
controller must apply the actual by-rate/offset/shape/limit/RU power configuration
in this interval, then call `finish()` to restore receivers/PPDU/tracking. Power
tables and regulatory policy are **not supplied by this channel component**.
Both phases have a shared bounded deadline. `abort()` is available when external
power work fails, including after a native preflight acquired a lease whose
cleanup failed. Partial failures require the native owner's verified recovery.

Neither `registersProgrammed` nor `receiversRestored` authorizes TX. A channel
lease release must keep scheduler TX paused until power, RFK and controller
prerequisites are satisfied. Native code checks the TX mask before and after
the release callback and retains ownership on violation. Channel MAC writes
are restricted to two byte registers and three word registers, with CMAC state
read-only; this interface cannot write the scheduler mask, PCI or unrelated MAC
registers. The same native RF/BB transport and recovery owner are reused.

Tests cover valid primary placements in both bands, signed gain values, 297 I/O
and four delay faults, cancellation/deadline/clock anomalies, all PLL retry
levels and final failure, ignored programming and receiver-restoration writes,
retained abort/recovery ownership and 128 consecutive channel changes. Native
tests verify byte-width isolation, register/kind restrictions, detection of a
release callback that resumes TX, and recovery of a lease retained after failed
preflight cleanup. These are register/IOKit models and cross-compilation, not
real channel tuning, implemented power limits or network readiness.

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
