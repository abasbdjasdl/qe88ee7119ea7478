# RTL8852B initial PHY hardware programming

`MacPhyInitialization.cpp` implements actual native BAR2, XTAL SI and indirect RF operations used by the RTL8852B/AX initial branches of pinned rtw89 commit `d1fced1b8a741dc9f92b47c69489c24385945f6e`. It is composed by `MacRadioBoot`; it is not a success-only backend or a replacement for channel programming/RFK.

The order is:

1. BB table, `powerUnit()`, then BB gain table. `rtw8852b_init_txpwr_unit` (`rtw8852b.c:1770`) programs the three MAC power-control words and UL-TB offsets.
2. `reset()` after the gain table: `rtw8852b_bb_reset` / `rtw8852b_bb_reset_all` assert both-path tracking/manual controls, SI triggers and BB reset, then release them.
3. RF tables, then `beforeRfk(calibration, capabilities)`: initial real thermal sampling, `rtw8852b_bb_sethw` (128 MACID-power words cleared, signed gain bases captured), CCX/IFS monitoring registers, PHY-status bitmap, initial DIG/CCKPD, crystal-cap/CFO, EDCCA collision threshold, and ULTB band-edge capture. This follows `rtw89_phy_dm_init` (`phy.c:6103`) through its RFK boundary.
4. NCTL and initial RCK/DACK/RxDC are owned by `MacRadioBoot`/RFK. Then `powerReference()`, actual radio power trim, and `receivePath({0,0,1})` (alias `afterRfk`) follow source `set_txpwr_ctrl -> power_trim -> cfg_txrx_path` (`phy.c:6124–6126`). Power reference is the exact source calculation, including the TSSI code 172 and RF power code 312. RX paths use the real firmware antenna/NSS overrides and board/PHY gain calibration.

The initial `{2 GHz, 20 MHz, channel 1}` entity is source initialization: `rtw89_get_default_chandef` (`core.c:280`) selects the first 2 GHz channel and NO_HT, with default entity initialization in `chan.c:196–207`. This is only the initial band/path register context. It does not claim a channel was tuned, calibrated, legally permitted or available for transmission. Subsequent channel and power programming remains mandatory. The full RX-path initializer is one-shot: repeating it after TSSI would reset tracking. The source channel programmer clears BT-sharing on 5 GHz; later switching back to 2 GHz requires the runtime coexistence callback to restore BT-sharing. That runtime callback is outside this initial 2 GHz bring-up.

Actual source no-op branches are explicit: RTL8852B `rfe_gpio` is NULL, the AX `bb_wrap_init` and `ch_info_init` are NULL, and this dual-RF-path chip does not enable antenna diversity. `support_igi=false` skips forced-IGI hardware programming. These are not fabricated successful operations. Periodic DIG/CFO/environment tracking and their host-side history are not implemented by this module.

The caller must hold the workloop gate and supply a live exclusive scheduler/BT/radio ownership guard. Every native access checks the guard and PCI memory decoding before and after access; firmware H2C bus mastering may remain enabled. BAR identity and physical mapping must match the RTL8852BE device. A phase has a monotonic 1-second deadline and a finite operation bound. SI idle uses a real 50-ms deadline including register-read time, plus an iteration bound for frozen clocks. Only XI/XO SI addresses 4 and 5 are writable; no OTP/eFuse programming command exists here. Readback checks cover persistent fields, including crystal-cap data. An error latches and prevents later success; a partially touched device requires reset, with the scheduler kept paused by its owner. Readback cannot prove that an unobservable hardware strobe physically executed.

Calibration identity, cut 0/1, firmware epoch, MAC, RFE, NSS and chip feature flags must agree before calibration-dependent access. Blank thermal samples remain explicitly absent. Signed gain calculations avoid undefined negative shifts and use source clamping; unrelated register fields remain intact.

`tools/import_phy_init_constants.py ../rtw89` verifies the pinned source and imports 144 numeric register fields. The adjacent provenance JSON records source hashes (including `core.h` enum definitions), the generated-file hash, license and untested-hardware status. Implementation functions are source-adapted C++; the importer does not claim to generate those functions.

Validation runs the actual `MacPhyInitialization.cpp` and `MacRadioIo.cpp` against a native MMIO model which implements only SI/RF completion and injected failures. It checks 500 individual read faults, 252 dropped writes, 1,531 ownership boundaries, cut/path/NSS variants, gain signs/clamps/neighbors, absent thermal readings, PCI/gate/BAR/cancellation rejection, stalled and corrupt SI, frozen/backward time, and slow ready reads beyond the SI deadline. Successful dropped-write cases must preserve the complete final register image; write-only strobe execution cannot be established by this model. These tests are not evidence of physical radio readiness or networking.

```text
zig c++ -std=c++14 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all -I tests/network_rfk_fakes tests/network_phy_initialization_native_test.cpp src/network/Rtw8852bRadioTables.cpp -o build/network_phy_initialization_native_test.exe
build/network_phy_initialization_native_test.exe
zig c++ -target x86_64-macos-none -std=c++14 -Wall -Wextra -Werror -Wno-unused-parameter -mkernel -DKERNEL -DKERNEL_EXTENSION -fno-stack-protector -mno-red-zone -fno-exceptions -fno-rtti -I ../MacKernelSDK/Headers -c src/network/MacPhyInitialization.cpp -o build/MacPhyInitialization.o
```
