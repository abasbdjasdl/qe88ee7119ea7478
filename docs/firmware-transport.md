# Firmware packet and transfer core (offline, not a hardware backend)

Physical 0.0.7 testing passed on 2026-09-21: RING_CONFIG_VALIDATED, six setup and
six restoration writes, matching configuration readback, confirmed supply-off,
DMA memory cleanup and PCI Command restoration. No doorbell/DMA/firmware upload
occurred. The live kext remains 0.0.7 and does not include the code described here.

## Exact 8852B packet format

`FirmwarePackets.hpp` parses an immutable source container and produces a finite
batch. It does not accept caller-constructed layout metadata. The maximum is 255
packets so the complete transaction fits a 256-entry ring with a spare slot.
The pinned cut-1 and cut-2 images each require one header command plus 163 section
packets. All packet storage is retained for the whole transaction, with no reuse
based solely on a hardware consumer-index advance: the reference driver delays
release by RTW89_PCI_MULTITAG (8).

RTL8852B uses `rtw89_core_fill_txdesc` and a 24-byte TXWD body. The newer
`rtw89_core_fill_txdesc_fwcmd_v1` RX-short format must not be substituted.
Channel DMA is 12, WD page/info are disabled, and FW_DL is set for raw section
payloads only. The length at TXWD dword 2 excludes the 24-byte descriptor. The
SW_SEQ field follows the reference payload-derived value when at least 24 bytes
are available; shorter tails use zero instead of an out-of-bounds read.

Header packet: TXWD + 8-byte H2C + base firmware header/section table. H2C category
1 / class 3 / function 0 / delivery type 0, caller sequence, no ACK flags. Dynamic
feature records are excluded as in `__rtw89_fw_download_hdr`. Part size is set
to 2020 in the in-memory header copy only. Section packets contain TXWD + up to
2020 unchanged bytes, including checksum trailers accounted for by the parser.
The original firmware fixture is never modified or repackaged for distribution.

## Transfer protocol and ownership contract

`FirmwareTransfer.hpp` is a portable protocol core tested with a simulated backend.
There is no production backend and no hardware success claim. Backend operations
must be bounded and invoked from one serialized thread. Backend code must not
retain references to stack arguments. Its contract is:

1. `prepare`: allocate, encode, pin, map and synchronize **all** packets and the
   ring with zero producer/consumer indices and bus mastering disabled. Partial
   preparation owns resources but has not started DMA. It must expose no index.
2. `startDownload`: perform the still-unimplemented DMAC/DLE/HFC/PCI pre-init,
   reset the firmware CPU/download state and start the fresh H2C download path.
   Any partial attempt is treated as potentially active DMA, even on failure.
3. The core waits for H2C ready, publishes the header, waits for FWDL ready,
   clears halt controls, publishes section packets in order, waits at least 5 ms
   and polls firmware state 7. State 7 before header/download preparation is
   rejected as stale. Checksum, security, cut mismatch and invalid register
   readings fail explicitly; there is no automatic retry/reset loop.
4. `publish`: publish a previously prepared producer index in order, with the
   required memory ordering. It must never rewrite or release a packet. Even a
   failed call may have reached hardware; cleanup must assume ownership passed.
5. The core checks host and consumer indices, which cannot wrap in this bounded
   batch, and requires both to reach the packet count. Consumer advancement is
   not sufficient to release memory.
6. `quiesceAndProveIdle`: stop HCI/channel DMA, verify idle, disable PCI bus
   mastering and confirm hardware cannot reference the buffers. A timeout or
   ambiguous result returns false. The core then retains ownership and never
   calls `releaseAll`; the backend must keep the buffers alive until a separately
   verified reset/power-off allows recovery.
7. `releaseAll`: complete/unmap/release allocations, including partial prepares,
   and report cleanup success. A partial release failure is reported separately
   and unresolved allocations remain backend-owned.

Readiness/drain waits are capped at 400 ms and 8001 polls, including a stopped
clock. A monotonic 5 s overall budget covers the protocol between backend calls.
Backend calls must supply their own time bounds; the portable core cannot preempt
a blocked OS/hardware operation. Cancellation and backward clocks are explicit
failures. These limits are diagnostics choices, not claims of measured timings.

## Validation and remaining integration

Local tests and CI exercise both real firmware images, packet byte layout,
immutable input, small/overlapping output buffers, a one-byte tail, excluded
dynamic records, ring capacity, every one of 164 publish failures, partial prepare
and start, lost readiness, stale status, stuck/backward clocks, early/late cancel,
firmware error statuses, invalid indices and failed quiescence/release. Apple CI
adds ASan/UBSan and a kernel compile-only check. None loads the protocol on a card.

The next integration still needs actual DMAC/DLE/HFC and PCI initialization,
firmware CPU control, a pinned packet bank and a proven stop/idle backend.
Subsequent networking needs EFUSE/MAC/regulatory data, BB/RF setup/calibration,
RX/TX queue handling, management frames, scan scheduling, association/security
and a macOS networking interface. These are not represented as working stubs.
No reboot should be requested merely to exercise this offline packet core.

Reference, pinned revision d1fced1b8a741dc9f92b47c69489c24385945f6e:
https://github.com/lwfinger/rtw89/tree/d1fced1b8a741dc9f92b47c69489c24385945f6e

Relevant definitions: rtw8852b.c chip operations/h2c_desc_size; core.c
rtw89_core_tx_update_h2c_info/rtw89_core_fill_txdesc; fw.c header/section download;
pci.c rtw89_pci_fwcmd_submit/rtw89_pci_release_fwcmd; pci.h RTW89_PCI_MULTITAG.
