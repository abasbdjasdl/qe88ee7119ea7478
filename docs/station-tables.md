# AX RTL8852BE station tables

`StationTables.hpp` implements actual station CMAC and address/BSSID CAM payloads,
shared command-bus submission and acknowledgment sequencing, and the initial AX
DMAC/CMAC indirect register writes. `StationTables.cpp` instantiates the command
programmer against the existing native `MacCommandTransport` and CH12 bus. This is
a table-programming component, not a complete controller or proof of networking.

`tools/import_station_tables_reference.py ../rtw89` imports 94 exact field setters
from pinned Realtek commit `d1fced1b8a741dc9f92b47c69489c24385945f6e`. It refuses a
different commit or modified reference files. Generated
`Rtw8852bStationTableFields.hpp` preserves the data masks and the separate CMAC
update-mask words, using byte-safe little-endian accesses. The adjacent provenance
JSON records source-file and individual setter hashes. The source BSD option and
Realtek copyright are retained.

## Implemented commands

| API | Actual wire operation |
| --- | --- |
| `defaultCmac` | 68-byte AX CCTL payload, category 1/class 5/function 2; MACID update, TX path and antenna mapping, zero Doppler and TX-power tolerance. Follows `fw.c:rtw89_fw_h2c_default_cmac_tbl` and `__rtw89_fw_h2c_set_tx_path`. Antenna setting 0 follows the source fallback to path B. |
| `associationCmac` | Same CCTL command with exact association/disconnection update masks: disable RTS/data fallback, band-dependent RTS floor, station UL/DL, port, HE padding and BSR queue-size format. Follows `fw.c:rtw89_fw_h2c_assoc_cmac_tbl`; a disconnect still uses the supplied station capabilities exactly as the source does. |
| `addressCam` | 60-byte category 1/class 6/function 0 update containing the full address and BSSID entries: indices, valid bits, network type, source/target hashes and addresses, port/TSF, MACID, AID, BSSID mask/color. Follows `cam.c:rtw89_cam_fill_addr_cam_info` and `rtw89_cam_fill_bssid_cam_info`. Clearing `Cam::valid` invalidates both entries while retaining their identity fields. |
| `idlePlan` | CAM update first, default CMAC second, each with a real DONE ACK. This is the order after role-create/initial disconnected Join in `rtw89_mac_vif_init`. |
| `cmacPlan`, `camPlan` | Single acknowledged command used at the association/disconnect/removal action boundaries already controlled by `StationController`. These helpers do not insert or fake Join ACKs. |

8852B uses `H2C_FUNC_MAC_CCTLINFO_UD`, not V1/G7. Its
`h2c_default_dmac_tbl` chip operation is NULL: no fictitious default-DMAC H2C is
emitted. Initial DMAC entries are programmed through the actual AX indirect
register sequence below.

HE PPE padding follows `__get_sta_he_pkt_padding`: choose
`min(peer RX NSS, device TX NSS)-1`, iterate the four RU bitmap positions, extract
six bits of PPET16/PPET8 for each present RU, and apply source padding values.
Absent RU slots receive 1; absent PPE uses PHY capability byte 9's nominal value.
The implementation adds full payload bounds validation and uses bytewise extraction
instead of upstream's potentially unaligned two-byte read. It handles up to eight
advertised RX streams and the chip's two TX streams; malformed/truncated PPE and a
selected stream absent from the PPE header are rejected before any command.

The current API is for a single ordinary client station on CMAC/PHY 0, 2.4/5 GHz.
It accepts MACID/address index 0–127, BSSID index 0–9, port 0–4, valid unicast local
address and association AID 1–2007. The owner must allocate and retain those CAM
indices; this module does not claim free indices by accepting numbers. No AP,
TDLS, P2P, DBCC, or hardware key-CAM mode is implied. Security mode is source NORMAL
(2) with all hardware key indices/valid bits zero, matching the existing software
crypto RX/TX bridge. This neither installs keys nor authorizes a controlled port.

## Actual ACK routing and cancellation

Construct `NativeProgrammer` with the **same** `NativeFirmwareCommands` instance
used by role/join and BT. The programmer copies its one/two-command plan, submits
each payload with DONE ACK requested, and waits for that command's callback through
the existing global sequence allocator and ACK router. Receive ACK, DMA retirement,
wrong/stale sequence or wrong RX epoch cannot complete a table update. It submits
the second idle command only after the first command's DONE ACK. Native CH12
transport still owns cache synchronization, doorbells and retained DMA buffers;
this component never retires buffers because an ACK arrived.

The completion carries the original station action token. The completion callback
must record/enqueue the result for a later station command-gate action, not reenter
the station backend inline. Starting this programmer again while its completion
callback runs is rejected. Two seconds per command is enforced by the shared bus;
the native owner must call bus/programmer `service()` from its timer. A rejected
command, uncertain transport result, timeout, cancellation or failed completion
invalidates the epoch and requires actual recovery. There is no rollback claim.
The programmer and its callback owner must stay alive until bus/RX teardown has
drained all callbacks, including after failure. On `begin` failure the caller owns
error completion; accepted asynchronous failures deliver one failure callback.

## Initial register programming

`seedMacTables(io, macid)` executes the source sequence from
`mac.c:rtw89_mac_dmac_tbl_init` and `rtw89_mac_cmac_tbl_init`:

1. For four DMAC words, select `0x18800000 + macid*16 + word*4` via
   `R_AX_FILTER_MODEL_ADDR` (`0x0c04`), then write zero to indirect entry `0x40000`.
2. Select `0x18840000 + macid*32`, then write the eight source CMAC defaults to
   indirect entry offsets 0–28.
3. Drain posted writes and recheck ownership/pause/time constraints.

These are 17 register writes, bounded by a 100-ms total deadline with backward-clock
detection. The backend must implement `inGate`, `stationTableWindowOwned`,
`schedulerPaused`, `nowUs`, `write32` and `drainWrites`. The caller must already hold
the shared indirect-window lease with real scheduler TX paused and initialized MAC
access. The helper never releases that lease or resumes TX. Every failed attempted
write, loss of pause/ownership, or failed final drain after modification marks reset
required. Success means the exact write sequence and drain succeeded; no unsupported
indirect readback or hardware-table correctness claim is invented.

## Verification and remaining bindings

Host tests check full golden CMAC/CAM word arrays and update masks, source-address
packing, all 192 address-mask/hash combinations, 1815 HE/PPE capability combinations
with every shorter PPE length rejected, firmware ordering and duplicate/stale/receive
ACK behavior, bus transport/firmware/timeout/cancel/completion failures, and all 17
register write-failure and scheduler-loss positions. Kernel compilation covers the
actual native programmer instantiation.

Still required in the native controller: CAM resource allocation/retirement;
binding station actions to these plans; extracting negotiated capabilities from real
net80211 nodes; ownership and native I/O for the initial indirect-window writes;
port/MAC identity, RX filters, rate adaptation/aggregation/beamforming updates and
BT role notifications. Radio/RFK/power readiness remains a separate prerequisite.
These commands and their model tests are not evidence that a real AP association,
DHCP, packet exchange or Internet access has occurred.
