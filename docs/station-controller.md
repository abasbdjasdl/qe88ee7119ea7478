# RTL8852BE station orchestration

`StationController.hpp` is a bounded, asynchronous station orchestration component,
not a loadable network driver. It directly encodes the existing AX Role Maintain and
Join Info H2C packets and accepts decoded real C2H DONE ACKs. It sequences software
scanning, authentication/association evidence, firmware association programming,
controlled-port authorization, disconnect, and role removal. The concrete native
binding is now `MacNetworkController` with `MacRTL8852BE`'s boot service; see
`network-driver.md`. Component tests exercise a recording/fault-injecting backend
and do not prove radio operation or networking.

Ordering and field values come from the locally pinned `rtw89` commit
`d1fced1b8a741dc9f92b47c69489c24385945f6e`:

- `mac.c:rtw89_mac_vif_init`: port and initial MAC tables, create role, join
  disconnected, then CAM/default tables.
- `core.c:rtw89_core_sta_assoc`: association CMAC, Join Info connected, then CAM.
- `core.c:rtw89_core_sta_disconnect`: disconnected CMAC, Join Info disconnected,
  then CAM. Client station shares the VIF MACID; it does not create an AP-client role.
- `mac.c:rtw89_mac_vif_deinit`: remove role, then deinitialize/update CAM.
- `fw.c:rtw89_fw_h2c_role_maintain` and `rtw89_fw_h2c_join_info`: four-byte AX
  payloads, DONE ACK requested. No BE extended Join Info format is used.

The component supports one client VIF on CMAC 0 only. MACID is limited to 0–127,
port to 0–4, no DBCC, no AP/TDLS role. Channel geometry validation is separate from
regulatory permission: the real backend must enforce its power/channel policy.

## Required integration contract

All entry points and backend calls run in one controller command gate. Backend
methods cannot reenter the state machine; completion callbacks are queued for a
later gate action. Timestamps are monotonic microseconds sampled at processing
time. Each asynchronous action carries an original `(firmware epoch, operation)`
token, copied when work is accepted. Completion must retain that token. Each action
has a twelve-second deadline, including prior TX drain and the radio's full
five-phase tune budget of ten seconds. Standalone Role/Join firmware DONE ACKs
retain their separate two-second deadline. Authentication plus association has a ten-second deadline;
scan dwell is 10–1000 ms per channel, at most 64 channels, with an overall bound.

`beginAction` means **accepted**, not completed. A successful `actionComplete`
requires actual register operations, DMA fences, and requested firmware DONE ACKs
to have completed. A backend with empty success callbacks is not a valid binding.
Every action has the following obligations:

| Action | Completion evidence required from the backend |
| --- | --- |
| `prepareInterface` | Actual controller/runtime ready, port identity and RX policy installed, initial CMAC/DMAC table setup and MACID scheduler state; home channel programmed with real calibration, power policy and RFK. Normal data remains blocked. |
| `idleTables` | Address/BSSID CAM initialization and firmware CAM update completed; default CMAC/DMAC updates completed. |
| `scanBegin` | BT scan coordination and RFK scan-start completed; home state captured, normal TX paused and outstanding transmissions accounted for. |
| `scanTune` | Target channel programmed while paused; `ChannelProgramming::programPower` succeeds before `finish`; `prepareScanChannel` succeeds; RX metadata/current channel updated; regulatory policy grants the requested active probe, or action fails. |
| `scanRestore` | Saved home channel and power restored; RFK `finishScan` succeeds. This action does not release normal data TX. |
| `scanEnd` | BT scan-end and restored RX/port state completed. Only then may the controller restore its remembered traffic mode. |
| `prepareAuthentication` | Target channel, legal power and full channel RFK ready; real BSSID/RX/port state and association-start BT coordination installed. |
| `associationCmac` | Actual negotiated station capabilities/rates/aggregation parameters encoded and acknowledged in the hardware CMAC update. |
| `associationCam` | After connected Join DONE: address/BSSID CAM updated with real AID/MACID/network type; required station/rate/BT bookkeeping completed. |
| `disconnectCmac` | Real protocol cancellation, BA/beamforming/pending station resources fenced as needed; disconnected CMAC parameters acknowledged. |
| `disconnectCam` | After disconnected Join DONE: CAM and station/BT bookkeeping updated; no stale station TX can enter a subsequent connection. |
| `removeCam` | After role removal DONE: VIF CAM entries invalidated and firmware update complete; remaining host station resources released only under correct DMA ownership rules. |

This component does not encode CMAC/CAM payloads itself and does not substitute
its action tokens for firmware acknowledgments. `StationTables`, `MacStationIo`
and the concrete boot service now provide those encoders and native actions.
Model tests remain separate from hardware execution evidence.

`publishH2c` must copy the transient 12-byte encoded command into the actual
`MacFirmwareDmaQueue`/`Net80211FirmwareQueue` transport, cache-sync and ring CH12.
Submission, DMA retirement, receive ACK and DONE ACK are distinct. Command buffer
release remains exclusively governed by `FirmwareDmaOwnership` and its eight-tag
retention rule; station ACK handling never frees a DMA buffer.

The global H2C sequence allocator must not reuse an eight-bit sequence during a
firmware epoch. This conservative version fails into recovery at exhaustion
instead of guessing that old ACKs are gone. The component also independently
rejects locally repeated sequence values. Full controller integration needs a
coordinated firmware restart on exhaustion (or a separately proven firmware ACK
fence protocol); merely resetting the host sequence counter is invalid. ACK wire
format contains **no epoch**. `firmwareEvent`'s epoch is captured by the RX queue
incarnation, not stamped from current state when a packet arrives. `restart` only
accepts a greater epoch after the owner verifies firmware reset, DMA/interrupt
shutdown, drained old C2H/callbacks and queue reinitialization. Timed-out or rejected
commands latch recovery required and cannot be retried in the same epoch.

## Protocol and traffic binding

`beginAuthentication` enters the real net80211 authentication path. Only validated
AP receive/state callbacks may call `authenticated`, `associationReceived` or
`associationRejected`. The latter checks the attempt token, selected BSSID,
negotiated channel and AID. A TX completion, our own transmitted frame or a local
request to enter RUN is not association evidence. Successful Join DONE still waits
for the CAM action and then permits only controlled-port traffic. `authorizePort`
must be sourced from real `IEEE80211_NODE_PORT_VALID` after RSN/EAPOL/key handling,
or the real open-network association path. It is never generated from Join DONE.

Backend traffic policies must enforce the following at dequeue and scheduling:

- `none`: no normal management/data transmission; RX remains available as required.
- `management`: only permitted station management frames.
- `scanProbe`: only the scan's legal probe traffic; no stale home-channel data.
- `controlledPort`: management and protocol-authorized EAPOL, no ordinary data.
- `authorized`: normal data allowed with the current peer/key/descriptor policy.

`sendProbe` asks the real net80211 management path to build/transmit an active scan
probe. No fake scan results are generated. Passive channels never invoke it; RX
beacon/probe frames continue through `receiveRxq` and the existing packet bridge
with the actual tuned channel/RSSI. `scanFinished` is emitted only after home radio
restoration and scan-end completion. Foreground scans and background scans of an
already authorized station are supported; scans do not interrupt an uncompleted
RSN handshake. A controlled-port revocation during a background scan is remembered
and prevents data from reopening on return. Disconnect during any scan/association
action waits for that original action/ACK before ordered cleanup, with traffic
blocked throughout. Action failure or uncertain firmware state instead requires
reset and retains resources; cleanup success is never fabricated.

## Verification and remaining integration

`tests/network_station_controller_test.cpp` checks exact role/join wire words,
ordered DONE ACK handling, old/duplicate/wrong-command/wrong-sequence ACK rejection,
separate real association and controlled-port gates, AID/BSSID/attempt validation,
127 reconnect cycles followed by safe sequence exhaustion, timeout/restart epochs,
MACID/CMAC/port restrictions, passive/active scans, all scan cancellation stages,
disconnect during all association asynchronous stages, port revocation during
scanning, all action failure completions, and every backend failure position in a
complete create/scan/connect/scan/disconnect/remove flow.

Still needed for a working driver: native IOEthernetController lifecycle and
net80211 hooks; native action implementations including real CMAC/CAM encoders,
station rate/capability setup and BT notifications; global ACK allocation/reset
coordination; actual interrupt/RX dispatch into these callbacks; probe scheduling,
scan-result consumption and connection selection; key/controlled-port integration;
TX descriptor station policy and real resource teardown. Neither this component's
`associated()` nor `portAuthorized()` reports DHCP or Internet reachability.
