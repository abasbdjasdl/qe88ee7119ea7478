# RTL8852B RFK / Bluetooth arbitration component

`src/network/BtRfkCoordination.hpp` implements bounded RFK admission, physical
grant programming, an acknowledged firmware calibration policy, and restoration
of the previous policy and grants. It is a portable component, **not a native
controller binding or a complete Bluetooth coexistence implementation**. No
hardware result or working networking is claimed.

## Source and protocol evidence

Reference: Realtek/Linux `rtw89`, fixed commit
`d1fced1b8a741dc9f92b47c69489c24385945f6e`, BSD-3-Clause option.

| Behavior | Source functions / definitions |
|---|---|
| RFK types 0..7, STOP/START/ONESHOT_START/ONESHOT_STOP values 0..3, packed path/PHY/band map | `coex.h`: `btc_wl_rfk_type`, `btc_wl_rfk_state`, `BTC_RFK_*_MAP` |
| B2W RUN bit 5 / REQUEST bit 6, W2B WLRFK bit 11 / TDMA bit 9 | `coex.c`: `btc_b2w_scoreboard`, `btc_w2b_scoreboard`, `_chk_wl_rfk_request` |
| Admission polls every 40 us for 100 ms; calibration watchdog 300 ms | `coex.c`: `rtw89_btc_ntfy_wl_rfk`, `rtw89_coex_rfk_chk_work`; `coex.h`: `RTW89_COEX_RFK_CHK_WORK_PERIOD` |
| RFK uses WLAN control, software WLAN high / BT low, PLT none | `coex.c`: `_action_wl_rfk`, `_set_ant` / `BTC_ANT_WRFK`, `_set_gnt` |
| RTL8852B uses original LTE grant path and `btc_set_policy_v1` | `rtw8852b.c`: `rtw8852b_chip_ops.cfg_ctrl_path`, `.mac_cfg_gnt`, `.btc_set_policy` |
| LTE CTRL / WDATA / RDATA `0xdaf0/4/8`, status bit 5, 50 us polling / 50 ms timeout, commands `0x800f0000` and `0xc00f0000` | `mac.c`: `rtw89_mac_read_lte`, `rtw89_mac_write_lte`; `reg.h`: `R_AX_LTE_*` |
| Grant register `0x38`, mask `0xff00ff00`, WLAN-high/BT-low value `0x77007700` for both RF grants | `mac.c`: `rtw89_mac_cfg_gnt`; `reg.h`: `B_AX_GNT_*_S0/S1_SW_VAL/CTRL` |
| WLAN control byte `0x73`, bit 2 | `mac.c`: `rtw89_mac_cfg_ctrl_path`; `reg.h`: `R_AX_SYS_SDIO_CTRL` |
| CMAC0 PLT `0xc67c`, enable bit 8, 16-bit access | `mac.c`: `rtw89_mac_cfg_plt_ax`; `reg.h`: `R_AX_BT_PLT`, `B_AX_PLT_EN` |
| CMAC0 enable `0xc000`, bit 30 | `mac.c`: `rtw89_mac_check_mac_en_ax`; `reg.h`: `R_AX_CMAC_FUNC_EN`, `B_AX_CMAC_EN` |
| Scoreboard `0xac`, 24-bit driver shadow, upper FW bits preserved, toggle bit 31, powered-on notify bit 24, mandatory 1 ms delay | `mac.c`: `rtw89_mac_cfg_sb`, `rtw89_mac_get_sb`; `reg.h`: `R_AX_SCOREBOARD`, `B_MAC_AX_SB_*`, `MAC_AX_NOTIFY_TP_MAJOR` |
| TDMA schema 3, slot schema 1 for 8852B firmware entries 0.27.0.0, 0.29.14.0, 0.29.29.0 | `coex.c`: `rtw89_btc_ver_defs` |
| 26-byte calibration policy: TDMA v3 TLV plus OFF slot v1 TLV, TDMA all zero, duration 100, table `0xaaaaaaaa`, slot MIX | `coex.c`: `_append_tdma`, `_append_slot_v1`, `t_def[CXTD_OFF]`, `s_def[CXST_OFF]`, `cxtbl[1]`, `rtw89_btc_set_policy_v1/BTC_CXP_OFF_WL`; `core.h`: `rtw89_btc_fbtc_tdma_v3`, `rtw89_btc_fbtc_slot` |
| Policy command category 2, class 0x10, function 3, done ACK requested | `coex.c`: `_send_fw_cmd`, `_fw_set_policy`; `fw.c`: `rtw89_fw_h2c_raw_with_hdr`; `fw.h`: `BTFC_SET`, `SET_CX_POLICY`, `H2C_CAT_OUTSRC` |
| Optional 6-byte CXDRVINFO_RFK encoder, class 0x10/function 5, state/path/PHY/band/type bitfields | `fw.c`: `rtw89_fw_h2c_cxdrv_rfk`; `fw.h`: `RTW89_SET_FWCMD_CXRFK_*`, `CXDRVINFO_RFK`, `SET_DRV_INFO` |

SHA-256 of the exact source files inspected:

| File | SHA-256 |
|---|---|
| `coex.c` | `334cfd08b080301436053538f84c84a440d103794ab0c5e5d17eeb26390b575d` |
| `mac.c` | `eff3ff31136b6accf7c6bcf3aaa6cb32e3a579bf5f117ae52f9782835094b728` |
| `fw.c` | `1b18b1ef3654493846e277229c51508824122ca2d9b1aec759574e386de66ea9` |
| `fw.h` | `2fabbea4858d620b9d0ef084bdaae84f93a9bda202456a389ea278abf82da202` |
| `core.h` | `270894eef74cd6b07fe0d67dd3daf8a1aca8f45c70e6493529c61b6cda80538b` |
| `reg.h` | `ed7a3500553a068ab7e56487e4d667c5f3cf5c32e67820742eede9640de796f2` |

## Controller integration contract

1. Use the physical device's serialized workloop and a live mapped native I/O
   adapter. The component requires real byte, word and dword MMIO, bounded
   delays and a monotonic microsecond clock. No `return true` stand-ins.
2. Hold the device awake and the radio/channel/coexistence owners exclusively.
   Complete the existing firmware mailbox's **acknowledged scheduler pause**
   before `begin()`. This component checks scheduler `0xc348 == 0`; that check
   alone does not establish the firmware pause lease. Hold that lease throughout.
3. Pass the actual W2B scoreboard shadow and the last successfully programmed
   firmware TDMA/OFF-slot snapshot. Reading `0xac` yields B2W state and cannot
   initialize that shadow. Invalid/missing snapshots are rejected. All other
   coex updates must defer while this owner holds its lease. Unexpected shadow
   changes are detected rather than overwritten on restore.
4. Derive the protocol from the loaded firmware header, not a hardcoded guessed
   version. `Protocol::select` supports the pinned RTL8852B 0.27–0.29 family;
   unrecognized versions fail before mutation. The component supports single
   CMAC0/PHY0, RF path mask A/B/both and 2/5 GHz; no implicit DBCC/6 GHz routing.
5. `begin()` returns true for **submission**, never for RFK permission. Its
   scoreboard and LTE polling have independent elapsed-time checks and finite
   iteration bounds, including a frozen-clock bound. It saves the grant,
   control-path byte and PLT word; publishes WLRFK with the 1 ms delay; checks
   the BT race window; programs and reads back the RF grants; submits policy.
6. `submitH2c` copies the 26-byte payload into the existing shared command queue,
   allocates a safe wire sequence and requests Done ACK. Feed decoded real C2H
   events into `acceptEvent`. Wrong sequence, command, receive-only ACK, and
   short ACK do not authorize calibration. Done ACK errors are terminal. A
   transport return, DMA completion or elapsed wait is not an ACK.
7. Only after start policy Done ACK may the controller consume `ready()` and
   run RFK. `ready()` rechecks gate, cancellation, time and paused TX **at the
   point of consumption**, even if `service()` has not run. The existing
   synchronous `CalibrationControl.begin` must consume this pre-acquired ready
   lease. It must not block the workloop waiting for a C2H callback scheduled on
   the same workloop. Use async phases around the synchronous RFK component.
8. Call `oneshot(phyPath, true/false)` around the actual per-path one-shots;
   STOP must match START's packed path map. Upstream's oneshot notification is
   local state bookkeeping, not an independent hardware ACK. Accordingly this
   method sends no invented firmware notification. `encodeRfkInfo` exposes the
   source-defined packet for a future explicit caller; the coordinator does
   not add it to upstream's RFK call flow.
9. The 300 ms lease is absolute and is never renewed by oneshot calls. Run
   `service()` from the native timer and validate the lease during calibration
   polling/callbacks. `MacRfkIo` now requires `CalibrationControl.check`; the
   controller must bind it to this coordinator's live `ready()` plus its own
   ownership checks. The native adapter checks before/after register accesses,
   including nested indirect RF polls, and after each 1 ms sleep slice. Failure
   latches until verified recovery; it cannot be cleared by a later ready result.
   PMAC emission-stop cleanup remains available after expiry. The RFK
   algorithm's separate 2-second upper bound does
   **not** extend BT ownership to 2 seconds. A timer queued behind a synchronous
   2-second workloop operation cannot enforce the 300 ms contract: the adapter
   must check elapsed time during that operation and trigger verified recovery
   on expiration. The coordinator is not an independent hardware watchdog.
10. After RFK succeeds and no oneshot remains active, call `finish()` and wait
    for the baseline policy's Done ACK. Then exact saved grants, PLT and control
    path are restored and read back, and WLRFK/TDMA scoreboard bits are restored.
    Success still leaves scheduler TX paused. Apply any deferred current-BT
    policy changes before normal TX is resumed by the higher-level controller.
11. Every failure after acquiring ownership retains the lease. A timed-out or
    potentially submitted H2C invalidates the shared firmware command epoch.
    Never clear WLRFK, return grants to BT, or resume normal TX while a failed
    RFK engine might still transmit. Invoke the real device recovery/reset,
    verify RFK/TX quiescence and drain old DMA/C2H events before constructing a
    fresh coordinator and firmware queue. This header does not implement that
    physical reset and contains no fake success recovery callback.

Wire ACKs carry an 8-bit sequence and no hardware epoch. This instance refuses
all previously used sequences (256-bit set), but the shared queue must prevent
reuse against any other outstanding/stale command as well. A software generation
tag alone cannot disambiguate a stale wire ACK after wrap or reset. After a
timeout, do not recycle that queue until the verified reset/drain boundary.

Compared with the source, this implementation deliberately fails closed on an
invalid scoreboard, BT admission timeout, late register read, LTE timeout,
missing firmware ACK, ignored grant write, or elapsed RFK watchdog. It does not
inherit upstream's local `is_bt_iqk_timeout` bypass, proceed with LTE writes after
timeout, or clear BT ownership while an RFK engine's state is uncertain.

## Verification and remaining integration

`tests/network_bt_rfk_test.cpp` checks golden policy/RFK/H2C bytes, a full
START/oneshot/STOP lifecycle, exact register-width and saved-state preservation,
W2B/B2W separation, 46 normal-path I/O failure positions (including writes that
take effect but report failure), failed delivery delays, transport rejection,
wrong/missing/rejected/stale ACK, sequence reuse, ignored writes during acquire
and restore, busy/racing BT, invalid scoreboard, bounded/frozen/late polling,
gate/cancel/clock failures, unpaused TX, unserviced watchdog expiry, mismatched
oneshot paths and reentrancy. It is a model test, not hardware evidence.

Local host verification:

```powershell
../toolchain/zig-x86_64-windows-0.15.2/zig.exe c++ -std=c++14 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all tests/network_bt_rfk_test.cpp -o build/network_bt_rfk_test.exe
./build/network_bt_rfk_test.exe
```

A separate `x86_64-macos-none -mkernel` explicit-template compilation with a
declaration-only I/O contract checks kernel-target compilation. It does not
provide a native I/O implementation or link the component into the driver.

Still required: the concrete native MMIO/queue adapter; shared H2C ACK routing;
controller async acquisition and synchronous lease consumption, including
binding its mandatory live RFK check to the acquired coordinator; actual
firmware/BB/RF power-cycle recovery; valid
coex initialization and policy snapshots; dynamic BT profile/antenna/AFH/TDMA
policy recomputation and actual controller binding. None is marked complete by
these component tests.
