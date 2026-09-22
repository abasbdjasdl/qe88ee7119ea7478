# RTL8852B firmware capability query

`FirmwareCapabilities<Mailbox<MacMailboxIo>>` uses the controller's existing
serialized register mailbox. `FirmwareCapabilities.cpp` explicitly instantiates
the actual native combination. It sends GET_FEATURE (function 3, zero content
bytes, wire word `0x00000103`) and requires a newly received PHY_CAP (function 3)
with all four words (14 content bytes). It never substitutes an unsolicited or
stale reply. Mailbox acknowledgement and host counter updates must both succeed
before a snapshot is exposed.

Source: rtw89 revision `d1fced1b8a741dc9f92b47c69489c24385945f6e`,
`fw.h:30-49,120-141`, `mac.c:2870-2946`, `core.c:4496-4502`.

The query takes the non-null snapshot obtained from `DeviceCalibration::snapshot()`,
the parser-validated **successfully loaded** `firmware::Plan`, and the controller's
nonzero firmware epoch. It checks board identity validity, the supported cut 0/1,
an exactly matching firmware cut and NORMAL/NORMAL_CE image type. The native I/O
adapter checks PCI 10ec:b852, BAR2 mapping, workloop ownership and memory decode.
The PHY_CAP message contains no chip-cut or unique device identifier: matching
cut/identity to the live device and identifying a loaded image remain controller
responsibilities; passing an arbitrary proposed Plan is not proof of loading it.

Output retains all four raw words, sequence/ACK, reported NSS and antenna counts,
BW/PROT/NIC/WL_FUNC/HW_TYPE raw bytes, and the input MAC/RFE/cut/epoch. Source-derived
decoding applies:

- NSS zero falls back to the chip's 2; nonzero NSS is capped at 2. The separate
  fallback flags distinguish a chip default from reported firmware capability.
- One reported TX/RX antenna selects RF_B. A reported TX NSS of 1 with both
  antenna counts 2 selects RF_B for TX and enables TX path diversity.
- `antennaTx/Rx == 0` preserves upstream HAL's default-path representation.
  `effectiveTxPaths()/effectiveRxPaths()` resolve that to RF_AB for 8852B;
  `rtw8852b.c:2102` uses the equivalent RX fallback. Unknown antenna counts above
  2 are rejected instead of guessed. Zero retains the source default behavior.
- For 8852B, CCKPD is supported only after cut A; IGI is false (`core.c:4496`).

**BW and PROT are not decoded into capability masks.** The pinned source defines
their byte locations but neither a wire enumeration nor a decoder. Both
`bandwidthEncodingKnown` and `protocolEncodingKnown` remain false for every reply;
callers must not reinterpret the raw byte as MHz or 802.11 protocol bits. Chip
limits are independently 2.4/5 GHz and 20/40/80 MHz (`rtw8852b.c:2615-2620`), not
measurements from this query. No firmware protocol/bandwidth claim is fabricated.

The object is one-shot per epoch. `snapshot()` returns null on every failure.
Call `invalidate()` before power-off, CPU reset, device replacement or epoch
change, and discard any previously borrowed snapshot pointers. A stale mailbox
reply is left for the shared mailbox owner to handle; retry with a new query
object only after that owner has resolved it. A faulted mailbox requires the
existing new-epoch recovery, not a fresh wrapper around the same hardware.

`FirmwareMailbox::waitControl` now checks the post-read elapsed time before
accepting ready, including 5 ms H2C and 1 s C2H limits. Exactly-on-deadline ready
is accepted, later ready is rejected; frozen-clock poll bounds remain intact.

Validation: `network_firmware_capabilities_test.cpp` compiles and executes the
actual native `MacFirmwareMailboxIo.cpp`, with modeled PCI/BAR/MMIO/firmware.
It covers 9,216 NSS/antenna combinations, every I/O fault before and after its
side effect, cut/image/epoch rejection, stale/wrong/truncated messages, invalid
antenna counts, cancellation, lost ownership, PCI/BAR faults, frozen/backward
clocks, and late ready/last-data reads. The existing mailbox regression separately
tests exact and exceeded local deadline boundaries. This is model and compile
validation, not a successful device query or operational Wi-Fi claim.
