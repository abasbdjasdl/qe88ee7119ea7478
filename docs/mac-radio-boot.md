# Native radio lifecycle

The full five-step tune has a 10-second overall deadline; initialization also
has 10 seconds. Short scan operations retain their 2-second bound. Individual
BT RFK leases retain the unchanged 300 ms hardware coordination deadline;
the longer outer budget never extends a live lease or fabricates an ACK.

`MacRadioBoot` is a native composition of `RadioInitialization<MacRadioIo>`,
`MacPhyInitialization`, `BtInitialization<MacBtInitializationIo>`,
`BtRfkCoordination<MacBtRfkIo>`, `RfkInitialization<MacRfkIo>` and
`ChannelProgramming<MacRfkIo>`. It uses the controller's **existing** shared
`NativeFirmwareCommands` and `Mailbox<MacMailboxIo>`. It does not create a
second sequence allocator, mailbox owner, fake firmware acknowledgement, or
independent reset epoch. State, tables, gain and RF page buffers are allocated
off the kernel stack. The controller owns device, BAR2, workloop, calibration,
capability, loaded plan, command bus and mailbox lifetimes.

## Native entry points

`allocate(device, map, loop, calibration, capabilities, loadedPlan, commands,
mailbox)` validates board/cut/MAC/RFE/epoch and firmware coex protocol identity.
`begin()` starts initialization. Call `service(monotonicMicroseconds)` from the
same gate as actual C2H delivery; a regular timer is needed when no C2H arrives.
All operational entry points require that gate. Command callbacks only deliver
the actual matching epoch/token event to the waiting BT component; they do not
run a nested RFK or recursively pump the workloop.

`ready()` means the current requested operation completed. Immediately after
initial `begin()`, it means initial radio/RFK configuration completed, **not**
that an operating channel was tuned. `tuned()` and result `fullCalibration`
distinguish an actually programmed channel from initial readiness and from a
scan channel. `busy()` remains true across each real firmware ACK wait;
`result()->completion` increments once per completed boot/tune/scan action.

`tune(channel, powerPolicy, false)` performs normal channel and RFK setup.
`beginScan()` requires a fully IQ/TSSI/DPK-calibrated home channel.
`tune(channel, powerPolicy, true)` is permitted only inside that scan session;
`endScan()` requires the caller to have retuned the exact saved home channel.
All three are asynchronous. Keep the scan session across individual channel
visits and intervening home-channel restores; end it only at station scanEnd.

`setTraffic(station::Traffic)` maps none to scheduler mask 0,
management/scanProbe to MG0 bit 8, and controlledPort/authorized to BE bit 0 plus
MG0 bit 8. The station host TX admission filter must restrict controlledPort BE
traffic to EAPOL. `setTraffic(uint16_t)` accepts only these queues. Every update
uses the real firmware register-mailbox FUNC_SCH_TX_EN (5), reply 4, and actual
CTN_TXEN readback. Opening queues requires completed calibration; during scan
only MG0 can open, and only after scan coefficients complete. No traffic is
opened automatically at the end of RFK. `schedulerPaused()` checks both local
acknowledged ownership and fresh hardware CTN_TXEN=0. `stationWindowOwned()`
additionally requires idle/ready; the root owner still serializes the complete
station register-window operation against new radio work.

The new mailbox `setSchedulerMask` refuses to overwrite an outstanding
pauseScheduler/resumeScheduler pair. It leaves that owner's saved mask intact.
Radio operations use absolute masks instead of creating nested pause ownership.

`stop()` attempts acknowledged scheduler closure, stops any armed PMAC TX,
invalidates the shared command epoch and cancels these adapters. It returns
false if TX closure or release of an active synchronous RFK lease cannot be
proved. It returns true with no allocated state. This is not proof that PCI DMA
or all RF engines are stopped; the root's verified hardware/power shutdown is
still required. `release()` requires stopped state and invalidated command
callbacks; it does not free transport DMA. Destruction without stop retains
state rather than leaving an ACK receiver pointing at freed memory.

## Programming order and source basis

All programming components derive from fixed local rtw89 commit
`d1fced1b8a741dc9f92b47c69489c24385945f6e`. `RadioBootSequence` enforces:

1. Acknowledged scheduler stop; BB table; PHY power unit; decoded BB gain table;
   PHY BB reset; RF A/B tables and final SWSI drain.
2. RF firmware pages submitted on the one real CH12 command bus, at most one
   page per service. `phy.c:rtw89_phy_init_rf_reg` uploads these pages without a
   DONE ACK request. Completion here is source-defined submission, not a claim
   of a nonexistent firmware execution ACK or DMA retirement.
3. Hardware BT/PTA setup and five matching firmware DONE ACKs for monitor,
   slots, driver init, driver control and baseline policy. Then PHY pre-RFK
   initialization, NCTL and captured gain-base calibration configuration, as
   `core.c` orders `btc_ntfy_init` before `phy_dm_init`.
4. Separate acknowledged BT policy leases around initial RCK, DACK and RXDCK,
   matching `rtw8852b_rfk_init` order. Then PHY power reference, board power trim,
   and initial receive path, matching `phy.c:rtw89_phy_dm_init` ordering.
5. Each normal tune performs channel + regulatory power + receiver restoration,
   then RXDCK, IQK, TSSI and DPK, each under its own actual BT lease. This follows
   `rtw8852b_rfk_channel`. TX stays stopped through the last restore ACK.

The initial receive path uses `{2GHz, 20MHz, channel 1}` because
`core.c:rtw89_get_default_chandef` / `chan.c:rtw89_entity_init` choose the first
2-GHz channel and NO_HT. That source default does not declare a tuned or
regulatorily permitted channel. Actual tuning still requires the caller's
live `power::Policy`, identity generation and per-channel conducted-power
ceiling. No constant permissive power policy is manufactured by this module.

Scan source functions `rtw8852b_wifi_scan_notify` and `rtw8852b_tssi_scan` run
through the existing `RfkInitialization` scan APIs, preserving calibrated home
state. Scan visits do not repeat full IQK/TSSI PMAC/DPK. In the implemented
conservative scheduling, 11 scan visits with a home restore after each cost
88 BT policy commands. Including six RF pages, five BT initialization commands,
six initial RFK commands, ten initial home-tune commands, four scan boundary
commands and ten association-tune commands totals at most **129** commands.
Station/offload/role/join commands additionally consume the shared 256-sequence
epoch budget. No sequence is reused to evade that bound. Long-running scans and
reassociations must eventually restart a physically verified firmware epoch.

## Explicit WLAN-only coexistence mode

The first networking path selects actual source `BTC_MODE_WL`, not normal
concurrent Bluetooth service. `BtInitialization::Board.wlanOnly` sets
`fw.h:RTW89_H2C_CXINIT_INFO_WL_ONLY` bit 0 along with WL_INITOK bit 1. The packed
initialization INFO byte is therefore 3. `coex.c:_action_wl_only` selects
`BTC_ANT_WONLY` and **BTC_CXP_OFF_BT**, not OFF_WL. `_set_ant_v0` programs WLAN
grant high, Bluetooth grant low, control owned by WLAN, and PLT_NONE in both
directions (0x100 including PLT_EN). Its OFF slot remains the real cxtbl[2]
0xe5555555 MIX table; the existing policy Done ACK is still required. RFK
arbitration continues checking actual BT request/run scoreboard bits and uses
real start/restore policy acknowledgements. No synthetic scan/connect coex
notification is reported as successful.

The loaded `Plan.version` is the raw firmware header W1. Header major/minor/
sub/index occupy bits 7:0/15:8/23:16/31:24 respectively; BT version selection
expects major<<24 | minor<<16 | sub<<8 | index. `MacRadioBoot` explicitly converts
between them before protocol selection. Passing raw W1 directly would select
the wrong ABI. Concurrent Bluetooth coexistence, dynamic BT profile policies,
and Bluetooth availability are not advertised by this initial mode.

## ACK, lease and failure behavior

`RadioBootSequence` never calls the calibration body until its backend reports
the matching BT start DONE ACK. `MacRfkIo::begin` consumes that already-ready
lease. Every hardware access/indirect RF poll/sleep slice invokes the mandatory
live lease guard. `MacRfkIo::end` only ends the synchronous programming scope;
after its stack returns, the sequence submits BT restore and waits for the
actual restore ACK before creating a new coordinator or exposing completion.
The source coex oneshot transitions are local state transitions; no invented
per-oneshot H2C commands inflate the command budget.

The BT lease retains its real 300-ms watchdog, and each underlying RFK operation
retains its own checked timing/operation limits. Boot orchestration has a 10-s
bound; a tune/scan action has a 2-s bound. Initial table/PHY access is additionally
guarded by an actual scheduler read and a native clock deadline. A workloop
timer cannot retroactively authorize a lease that expired during calibration.

Failures close ordinary TX where the mailbox still operates, attempt narrow
PMAC emission cleanup, invalidate the shared command epoch and retain failed
calibration ownership. `CalibrationControl::recover` deliberately returns
false for unproved recovery: this code cannot claim a few writes have restored
a partly executed KIP/NCTL/RF engine. The real device-power owner must reset it.

## Validation limits

`network_radio_boot_test.cpp` tests the shared orchestration state machine:
explicitly absent start/restore ACKs, exact initial/full/scan step order,
22 scan/home channel operations, 129 command accounting, 53 failing backend
stage positions, deadlines, backwards time, reentry, and no readiness after
failure. Existing radio/RFK/channel/BT component tests exercise their actual
source register programming separately. `network_mailbox_test.cpp` now tests
absolute masks, every mask-operation IO failure, readback rejection and saved
pause-mask preservation. BT initialization tests verify the real WLAN-only wire
INFO byte, grants, priority and acknowledged policy; both model and fake-native
IOKit modes pass UBSan.

`MacRadioBoot.cpp` compiles against the MacKernelSDK for x86_64-macos-none with
`-mkernel -DKERNEL -DKERNEL_EXTENSION -fno-exceptions -fno-rtti -Wall -Wextra
-Werror`. This validates concrete adapter composition and template interfaces.
No physical radio, association, packet transfer or internet connection has yet
been proven by these tests.
