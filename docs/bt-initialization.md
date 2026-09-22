# RTL8852B initial Bluetooth coexistence setup

`BtInitialization.hpp` supplies the initial hardware configuration and firmware
commands needed to produce the **programmed** W2B scoreboard and **acknowledged**
TDMA/OFF-slot snapshot consumed by `BtRfkCoordination`. `MacBtInitializationIo`
provides the native BAR2 / RF-access / shared-command-queue adapter. This is an
initialization component; it does not implement the complete runtime Bluetooth
policy engine or prove that the adapter works on this laptop.

The native `MacRadioBoot` composition now explicitly selects `Board.wlanOnly`.
That optional mode follows source `BTC_MODE_WL`: INIT INFO includes WL_ONLY
bit 0; `_action_wl_only` programs ANT_WONLY (WL grant high, BT low, PLT_NONE)
while retaining the source OFF_BT policy and its actual acknowledgement.
Default standalone callers retain the original normal-mode WINIT behavior.
See [mac-radio-boot.md](mac-radio-boot.md) for lifecycle binding, source version
conversion, scan handling and the explicit lack of concurrent Bluetooth support.

The reference is Realtek `rtw89` commit
`d1fced1b8a741dc9f92b47c69489c24385945f6e`, BSD-3-Clause option. Source files are
unchanged at that commit. Relevant SHA-256 values:

| Source | SHA-256 |
|---|---|
| `rtw8852b.c` | `1ac54aff8acd780be4d525893b4c0f6a39370787c45b8625a990a45a44e31a16` |
| `coex.c` | `334cfd08b080301436053538f84c84a440d103794ab0c5e5d17eeb26390b575d` |
| `coex.h` | `036edcf603d32e3d14b691f1220953fb411e5930c7a8673f388829e52e11c6e2` |
| `mac.c` | `eff3ff31136b6accf7c6bcf3aaa6cb32e3a579bf5f117ae52f9782835094b728` |
| `fw.c` | `1b18b1ef3654493846e277229c51508824122ca2d9b1aec759574e386de66ea9` |
| `fw.h` | `2fabbea4858d620b9d0ef084bdaae84f93a9bda202456a389ea278abf82da202` |
| `core.h` | `270894eef74cd6b07fe0d67dd3daf8a1aca8f45c70e6493529c61b6cda80538b` |
| `reg.h` | `ed7a3500553a068ab7e56487e4d667c5f3cf5c32e67820742eede9640de796f2` |

## Board and protocol inputs

`Board` requires the actual loaded firmware version, decoded board RFE type and
read chip cut. `identityValid` must come from successful device/eFuse identity
validation. It is not a request to manufacture a supported identity. RFE `0xff`,
invalid cut values (`core.h` defines CAV..CFV as 0..5), unknown identity and
firmware outside the supported 0.27–0.29 family are rejected before mutation.

The antenna derivation is exactly `rtw8852b_btc_set_rfe`: RFE 0 or a positive
odd RFE yields two shared antennas and BT_BTG; a positive even RFE yields three
dedicated antennas and BT_ALONE. Isolation 10, internal switch, no diversity,
and zeroed unassigned module fields come from that function plus
`_reset_btc_var(BTC_RESET_ALL)`. They are upstream defaults, not measured board
properties. The normal-mode packet carries `WL_INITOK`, no DBCC, and chip AFH
guard channel 6. Other coexistence modes are not implicitly selected.

All three pinned RTL8852B version records select TDMA schema 3, slots schema 1,
driver-init schema 0 and control schema 1. Firmware 0.29.29.0 and later in this
supported family uses monitor schema 2; earlier supported versions use schema 1.
`RTW89_FW_VER_CODE` is major/minor/sub/index in descending bytes.

## Hardware sequence and wire evidence

The initial hardware sequence is `rtw8852b_btc_init_cfg` using Realtek PTA mode
and internal direction in `rtw89_mac_coex_init`:

- Enable ENBT, PTA WLAN TX, BT grant polarity, statistics and WLAN-active mask;
  pulse BT counter reset; clear response BTCCA check; enable BTCCA while
  disabling BTCCA TXOP breaking.
- Read LTE SW_CFG_2 (`0x3c`) and retain only `B_AX_WL_RX_CTRL` bit 8, matching
  upstream's intentional AND operation. Configure Realtek mode 0, enable RTK
  BT, sample rate 5, internal PTA direction.
- Enable high-priority TX responses and beacon queue. Clear both RF WLSEL
  (`0x02`) debug controls. For both RF paths, program SS group 0 and TX group 2
  using LUTWE `0xef=0x20000`, LUTWA `0x33`, LUTWD0 `0x3f`, then disable LUTWE.
  Shared antennas use SS `0x5ff` on both paths and TX `0x5ff/0x55f`; dedicated
  antennas use SS `0x5df` on both and TX `0x5ff/0x5ff`.
- Program PTA break table `0xda2c=0xf0ffffff`; enable counters; remove BT force
  power overrides via `0xd200` mask `0x3ff` and `0xd220` mask `0xffa`. These are
  the `rtw8852b_btc_set_wl_txpwr_ctrl(WL_TX_POWER_NO_BTC_CTRL)` operations; they
  do not supply by-rate power, regulatory limits or RF calibration.

Persistent MAC bits, LTE configuration/grants, RF WLSEL and RF LUTWE are read
back. The counter-reset strobe is excluded from persistent-bit comparisons
because it may self-clear. RF LUT data-port readback semantics are not invented:
the real `RadioAccess` indirect write-completion polls and final drain verify
transaction completion, while physical LUT contents remain a hardware-test
limitation. A failed operation leaves outputs invalid and requires actual device
recovery; there is no fake cleanup that asserts the RF LUT is safe.

The firmware sequence is taken from `rtw89_btc_ntfy_init` and its callees:

| Command | Payload | Exact source |
|---|---|---|
| category 2/class 0x10/function 2 | 130 bytes: monitor version, count 16, 16 packed `{le16 type, le16 bytes, le32 offset}` records | `btc_fw_set_monreg`, `rtw89_btc_8852b_mon_reg`, `rtw89_btc_fbtc_mreg` |
| category 2/class 0x10/function 1 | 146 bytes: version 1, count 18, all `s_def[]` entries in `CXST_*` order; each slot is le16 duration/le32 table/le16 type | `rtw89_btc_fw_set_slots`, `BTF_SET_SLOT_TABLE_VER`, `rtw89_btc_btf_set_slot_table` |
| category 2/class 0x10/function 5 | 14 bytes: CXDRVINFO_INIT header plus actual RFE/cut-derived module fields | `rtw89_fw_h2c_cxdrv_init`, `rtw89_h2c_cxinit`, `RTW89_H2C_CXINIT_*` |
| category 2/class 0x10/function 5 | 6 bytes: CXDRVINFO_CTRL header, IGNORE_BT bit 1, no trace_step for fcxctrl=1 | `rtw89_fw_h2c_cxdrv_ctrl`, `RTW89_SET_FWCMD_CXCTRL_*` |
| category 2/class 0x10/function 3 | 26 bytes: TDMA OFF v3 TLV and OFF-slot v1 TLV, duration 100, table `0xe5555555`, MIX=0 | `_action_wl_init`, `rtw89_btc_set_policy_v1/BTC_CXP_OFF_BT`, `_append_tdma`, `_append_slot_v1` |

After the control packet's ACK, WINIT grant ownership is programmed as in
`_set_ant(BTC_ANT_WINIT)`: if the B2W scoreboard says BT enabled (bit 1), BT is
software high and WLAN low (`0xdd00dd00`); otherwise WLAN high and BT low
(`0x77007700`). Non-grant LTE bits are preserved. WLAN selects the control path
(`0x73` bit 2), and CMAC0 TX/RX PLT monitors BT TX/RX (`0xc67c=0x166`).

The final W2B shadow is `_reset_btc_var`'s zero plus ACTIVE|ON|BTLOG (`0x4003`).
The scoreboard write uses the actual B2W read only to preserve FW upper bits,
sets toggle/TP-major as `rtw89_mac_cfg_sb`, and waits the required 1 ms. It never
copies B2W lower bits into W2B software state or tries to compare a W2B write
against the unrelated B2W read direction.

## ACK and lifetime contract

`begin()` submits only the first firmware command after hardware configuration.
Each matching successful Done ACK advances exactly one phase. No timer, queue
submission or DMA completion substitutes for ACK. Both output `valid` fields
remain false until all five commands acknowledge, the scoreboard is written and
its delivery delay completes, and TX is still paused.

Upstream `_send_fw_cmd` requests Done ACK for monitor, slot and policy commands.
The INIT/CTRL driver-info functions originally request neither ACK. **This port
explicitly requests Done ACK for these two as a stricter completion condition.**
The loaded firmware must actually provide it; compatibility is not established
by a model test. Missing ACK times out with invalid outputs; do not introduce a
fabricated ACK fallback. This change does not alter their payload format.

The component uses a 300 ms deadline per command and 2 seconds overall, chosen
as bounded host initialization limits. LTE polling also has source-defined
50 us / 50 ms and finite iteration bounds, including frozen/late clocks.
Unexpected ACKs are not consumed; duplicate wire sequences, rejection, clock
regression, cancellation or reentrant callback invalidates the firmware command
epoch. The shared `FirmwareCommands` allocator still owns cross-client sequence
uniqueness and the real post-reset C2H/DMA drain boundary.

The native adapter verifies RTL8852BE PCI identity, a matching BAR2 mapping of
at least `0x20000`, PCI memory access, workloop gate, cancellation, command-bus
availability and a one-use two-second native access window. Its MMIO and RF
allowlists contain only initialization operations. RF access uses `MacRadioIo`
and `RadioAccess`, including checks inside indirect polling and drain. Stop
servicing this initialization object after transferring its successful outputs
to the RFK/controller owners; runtime RFK uses `MacBtRfkIo`, not this init-only
adapter.

Before beginning, the controller must have established power/clocks, firmware,
CMAC0, RF tables, the shared command queue/RX path and an **acknowledged firmware
scheduler pause**, and hold exclusive radio/coex ownership and an awake lease.
CMAC1/DBCC and active/requested BT RFK are rejected; no partial mode is silently
routed to CMAC0. The register TX-zero check does not manufacture a pause lease.

## Verification and remaining work

`network_bt_initialization_test.cpp` checks golden wire structures and H2C header,
both RFE branches, 124 I/O failure positions including writes that take effect
before returning error, all five transport failures/ACK rejection positions,
wrong/stale/receive-only/short ACKs, sequence reuse, ignored persistent writes,
RF write-enable failures, self-clearing reset pulses, late/frozen LTE polling,
overall/per-command deadlines, BT/TX conflicts, workloop/cancel/clock failures,
and valid-before-ACK prevention. Existing valid output objects are not silently
destroyed when an accidental second initialization is rejected, including a
repeat `begin()` on the same successfully completed instance after its native
access window expires or the workloop gate is left. That repeat performs no
I/O/H2C and preserves completion; an in-flight repeat still faults its owner.

The same file with `BT_INITIALIZATION_NATIVE_TEST` compiles the actual native
adapter and RF implementation against fake IOKit and tests widths, allowlists,
RF access and actual shared command client copying/ACK routing. Both modes passed
locally with Zig C++14, `-Wall -Wextra -Werror`, UBSan:

```powershell
../toolchain/zig-x86_64-windows-0.15.2/zig.exe c++ -std=c++14 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all tests/network_bt_initialization_test.cpp -o build/network_bt_initialization_test.exe
./build/network_bt_initialization_test.exe
../toolchain/zig-x86_64-windows-0.15.2/zig.exe c++ -std=c++14 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all -DBT_INITIALIZATION_NATIVE_TEST -I tests/network_rfk_fakes tests/network_bt_initialization_test.cpp src/network/Rtw8852bRadioTables.cpp -o build/network_bt_initialization_native_test.exe
./build/network_bt_initialization_native_test.exe
```

The actual `MacBtInitializationIo.cpp` explicit template instantiation also
compiled for `x86_64-macos-none -mkernel` against MacKernelSDK. No native execution
or physical firmware ACK has been observed.

Remaining: controller lifecycle binding and shared ACK callback routing; proven
firmware/RF startup; `rtw89_btc_ntfy_init`'s `_action_common` work (BTG/pre-AGC,
TX constraints, AFH, BT RX gain/scan priority, RF TRX parameters and report-enable
commands); report consumers and runtime profile policy changes; verified error
recovery/power cycling. The source sets the host `igno_bt` state false on exiting
`_run_coex`; this component sends the original initialization CTRL packet and
does not pretend to implement that subsequent dynamic host state machine.
These initial snapshots authorize only subsequent guarded RFK coordination,
not normal transmit, association or a claim of complete coexistence.
