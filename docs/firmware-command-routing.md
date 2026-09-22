# Shared native firmware command path

`FirmwareCommands<Transport>` is the single per-device H2C allocator and ACK
router. `MacCommandTransport` binds it directly to `submitNativeFirmware`, the
existing native CH12 queue staging/cache-sync/doorbell path. It does not invent
DMA addresses or release DMA packets when an ACK arrives. The existing eight
consumed-packet retention rule in `FirmwareDmaOwnership` remains authoritative.
AX headers/ACK parsing reuse the pinned rtw89-derived `FirmwareProtocol.hpp`.

All calls occur under one workloop gate. The bus owns 256 command records and a
16 KiB staging buffer; allocate it on the heap. Construct it once for a verified
firmware/RX incarnation, and keep runtime, queues and callback owners alive until
interrupts are drained and physical DMA stop is proven. There is no software
reset method that makes a possibly stale wire sequence safe again.

## Submission and completion

- Producers use the same `reserve`/`publish` or `submit` path, including radio
  commands without requested ACKs. Reserved sequences cannot be published twice.
  Existing role/join encoders can encode their allocated sequence then publish.
- Each reservation has an owner callback, opaque operation token and absolute
  deadline. It expires even if submission never happens. Payload/header bounds,
  reserved bits and exact length are checked before DMA submission. The native
  queue copies the staging buffer before returning.
- Receive ACK only records receipt when Done ACK was requested. Done ACK must
  match both command ID and allocated sequence before dispatch to that command's
  owner. Duplicate, unmatched and old RX-epoch ACKs cannot complete a new command.
- Records are completed before invoking the owner. A station callback may submit
  its next join command immediately without corrupting the previous record.
  Recursive event delivery and events during transport publication invalidate
  the bus. No synchronous C2H callback is permitted inside DMA publication.
- Deadlines and monotonic time are rechecked at submission return, service and
  receipt, so a late ACK or late transport return is not success. Firmware error,
  ambiguous transport error, callback failure or timeout invalidates this shared
  bus. All users must stop normal work and enter physical recovery together.
- `MacFirmwareEventBinding::receive` plugs into the existing
  `ReceiveCallbacks::firmwareEvent`. Its epoch is captured when the RX queue is
  constructed, never relabeled on receipt. Non-ACK notifications go to the
  controller's supplied dispatcher. The binding and all owners remain borrowed.

Wire ACKs have an 8-bit sequence and no epoch. The current policy never reuses a
sequence in one firmware incarnation; exhaustion after 256 reservations requires
verified firmware reset and RX drain. This is a real remaining throughput and
lifetime limitation, not a claim of indefinite operation. A validated wire fence
or another source-supported reuse policy is needed before removing that limit.

## Native Bluetooth RFK interface

`FirmwareCommandClient` supplies a stable per-owner link to the shared bus.
`MacBtRfkIo` consumes that link and instantiates the actual
`RfkCoordination<MacBtRfkIo>` algorithm. It validates RTL8852BE PCI identity, the
exact BAR2 mapping, workloop gate, PCI memory-enable/device-loss state and global
command validity. Byte/word/dword operations are restricted to the scoreboard,
LTE grant port, WLAN control path, PLT and read-only firmware/CMAC/scheduler
status. Only the source-defined grant-register commands `0x800f0038` and
`0xc00f0038` are accepted at the LTE control port. Reads and writes have ordering
barriers and availability checks before and after access. Individual delays are
at most 1 ms. The caller still owns power/clock and exclusive coex arbitration.

Policy submission accepts the real 26-byte category-2/class-16/function-3 command
with Done ACK required. The client routes its completion to the owning
coordinator's `acceptEvent`; the controller must supply that stable callback and
keep the corresponding operation alive. `invalidateFirmwareEpoch` poisons the
same shared bus used by station commands, rather than only setting a local flag.

## Evidence and remaining integration

`network_firmware_commands_test.cpp` checks two independent command owners,
receipt versus execution ACKs, command/sequence routing, callback-chained join,
payload copying, firmware rejection, stale epochs/duplicates, exact-deadline and
late-publication failures, malformed headers, maximum payload, all 256 sequences,
clock rollback/overflow, gate failure and recursive callback rejection.

`network_bt_native_test.cpp` executes the actual `MacBtRfkIo.cpp` against an
explicit PCI/MMIO model and a real `FirmwareCommands`/`FirmwareCommandClient`.
It checks access widths, adjacent-byte preservation, register/opcode permissions,
missing/wrong PCI/BAR/workloop/command link, cancellation, command invalidation,
policy copying and ACK delivery. The older BT test covers the complete grant and
policy protocol with 46 I/O fault positions. Kernel compilation covers both
native modules; these checks are not physical interrupts, DMA or network proof.

Still required: controller ownership/allocation and RX binding construction;
actual station and RFK callback/lifecycle integration; conversion of every
remaining H2C producer to the shared allocator; initial coex policy/scoreboard
setup; full firmware/power/recovery sequence; and a complete loadable networking
controller. This change does not install a driver or request a reboot.
