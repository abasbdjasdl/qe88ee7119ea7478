# RTL8852B PCI link initialization

`PciLinkInitialization.hpp` implements the RTL8852B-applicable link configuration
at the start of `rtw89_pci_ops_mac_pre_init_ax`, plus the LTR and ADDR8B portion
of `rtw89_pci_ops_mac_post_init_ax`. `MacPciLinkIo` supplies real macOS BAR2,
extended PCI configuration, DBI and MDIO accesses. These files have compiled for
the macOS kernel target; execution on the RTL8852BE has not yet been verified.

All source references below are to `../rtw89` revision
`d1fced1b8a741dc9f92b47c69489c24385945f6e`. The reference driver offers a BSD
license option, retained in these source files. This is the AX/8852B sequence,
not the 8852C `ltr_set_v1` sequence.

## Controller interface and sequencing

```cpp
using namespace rtl8852be::network::pcilink;
MacPciLinkIo linkIo(pciDevice, bar2Mapping, workLoop);
Initialization<MacPciLinkIo> link(linkIo);
// Within the serialized device gate, after MAC power-on and BAR accessibility:
if (!linkIo.valid() || !link.preInit(actualHardwareCut)) { /* abort startup */ }
// Existing owner: stop/poll DMA, mode/rings/BDRAM setup, firmware download,
// DMAC/CMAC/firmware startup. Do not release ordinary TX queues here.
if (!link.postInit()) { /* abort startup */ }
// Existing owner: enable all initialized TX queues and release STOP_WPDMA /
// STOP_PCIEIO only after its own ring, firmware and interrupt prerequisites.
```

Keep the same object across both phases. The actual cut is a hardware-derived
value, not a default. Defined cut values 0 through 5 are accepted: the requested
source sequence has **no RTL8852B cut-specific differences**. The CAV tests in
`rtw89_pci_l12_vmain`/`rtw89_pci_gen2_force_ib` explicitly apply to RTL8852C,
and must not be transplanted to a B device. An out-of-range cut fails before IO.

The adapter borrows the device, exact BAR2 mapping and workloop; their lifetimes
must exceed the initializer's. It validates PCI ID `10ec:b852`, BAR identity,
minimum 64-KiB coverage, memory decoding and gate ownership for accesses. It
does not open/retain the device, set bus mastering, enable memory decoding,
allocate rings, set interrupts or start/stop DMA. Native writes to INIT_CFG1
permit changes only to the two KEEP_REG bits, preserving DMA/BDRAM ownership.
Caller must serialize DBI/MDIO use against all other device access and stop
using this object after teardown or a power/reset transition.

`preConfigured` means only this link pre-configuration succeeded;
`postConfigured` means only LTR/ADDR8B also succeeded. Neither asserts firmware
readiness, DMA readiness or a network connection. Repeating a completed phase
returns false without IO or destroying the completed result. Calling post
before pre fails. Reentry while running fails ownership; the outer phase then
stops because the error is sticky.

## Source-defined applicable writes

| Source function | Implemented 8852B action |
| --- | --- |
| `rtw89_pci_l1off_pwroff` | Clear `R_AX_PCIE_PS_CTRL` 0x1008 bit 5. |
| `rtw89_pci_aphy_pwrcut` | Clear `R_AX_SYS_PW_CTRL` 0x0004 bit 14. |
| `rtw89_pci_hci_ldo` | Set 0x0070 bit 15, then clear bit 14. |
| `rtw89_pci_dphy_delay` | Gen1 MDIO `RAC_REG_REV2` 0x1b bits 15:12 = `PCIE_DPHY_DLY_25US` (1). |
| `rtw89_pci_autok_x` | Gen1 MDIO `RAC_REG_FLD_0` 0x1d bits 3:2 = `PCIE_AUTOK_4` (3). |
| `rtw89_pci_auto_refclk_cal(false)` | Read config 0x82 bits 1:0 for Gen1/Gen2; save config 0x719; temporarily clear its L1 bit 3; clear `RAC_CTRL_PPR_V1` 0x30 `B_AX_CALIB_EN` bit 13 on that generation; restore saved L1 byte. |
| `rtw89_pci_power_wake(true)` | Set 0x0074 bit 5. |
| `rtw89_pci_set_sic` | Clear 0x13f0 bit 4. |
| `rtw89_pci_set_lbc` | 0x11d8 timer bits 7:4 = 8 (`MAC_AX_LBC_TMR_2MS`), set flag bit 1 and enable bit 0; perform the source's second OR write. |
| `rtw89_pci_set_dbg` | Set 0x11c0 bits 1:0. |
| `rtw89_pci_set_keep_reg` | Set 0x1000 bits 23:22 only. |
| `rtw89_pci_ltr_set(true)` | First validate 0x8410/14/18/1c; set CTRL0 enable bits 6,1,0, space bits 13:12 = 2 (`PCI_LTR_SPC_500US`), idle timer bits 10:8 = 7 (`PCI_LTR_IDLE_TIMER_3_2MS`); CTRL1 RX thresholds bits 11:0 and 27:16 = 0x28; idle latency 0x90039003, active latency 0x880b880b. |
| `rtw89_pci_ops_mac_post_init_ax` ADDR8B | Set 0x8810 bit 0 and clear 0x9a00 bit 1. |

Register/field definitions come from the pinned `pci.h` and `reg.h`; the enum
LTR selections are in `reg.h` as referenced by `pci.c`. The actual B
PCI descriptor `rtw8852be.c:rtw8852b_pci_info` selects LBC enabled with a 2-ms
timer, autok disabled and `rtw89_pci_ltr_set`. It has no BER quirk. Autok disabled
still requires the real calibration-disable transaction, rather than skipping
the entire function. Unlike the upstream unchecked DPHY call, our DPHY error
aborts the phase. Masked MDIO readback verifies the programmed control bits;
unrelated live counter bits need not remain equal.

In the same source pre-init call list, `disable_eq`, `autoload_hang`,
`l12_vmain`, `gen2_force_ib`, `l1_ent_lat`, `wd_exit_l1` and `set_io_rcy` are
8852C-only; `rxdma_prefth` and `l2_rxen_lat` are 8852A-only; deglitch applies to
A/C; BER is quirk-gated and absent for this B PCI descriptor. These are excluded
by their actual source predicates, not represented by successful empty stubs.
The A-only extra debug bit and A-only post-init software LTR trigger are also
excluded.

## Access protocols, completion and recovery

`rtw89_pci_check_mdio`, `rtw89_read16_mdio` and `rtw89_write16_mdio` define
0x10a0 control, 0x10a4 write data and 0x10a6 read data. Address low five bits go
to the control byte, bits 13:12 select pages 0/1 for Gen1 or 2/3 for Gen2, bit 9
triggers reads and bit 8 writes. Thus PHY address 0x30 uses page 1 or 3, not
address 0x10 on page 0. Each operation waits for the existing engine to be idle
before issuing the source transaction, then polls completion with a 2-ms
deadline, 10-us intervals and an independent 201-read maximum.

`rtw89_pci_read_config_byte`/`rtw89_pci_write_config_byte` first attempt native
configuration then use `rtw89_dbi_read8`/`rtw89_dbi_write8` on failure.
The macOS adapter uses **extendedConfigRead8/extendedConfigWrite8**: the SDK's
ordinary byte-config APIs take an 8-bit offset and would truncate 0x719. Native
read all-ones is treated as inconclusive and falls back. DBI flag/address is
0x1090, write data 0x1094 and read data 0x1098; addresses are dword-aligned and
the byte lane is retained. Write byte enables occupy flag bits 15:12. Trigger
byte at 0x1092 is 2 for read or 1 for write. Zero completion is polled with a
200-us deadline, 10-us intervals and 21-read maximum (pinned source's
`RTW89_PCI_WR_RETRY_CNT` = 20). No transaction is considered complete just
because its command was written. Configuration writes also require readback.

Each phase has a separate monotonic 100-ms/20,000-operation upper bound. A long
firmware startup between phases does not consume the post-init budget. Elapsed
time is checked after IO, so an overdue successful read cannot authorize a
phase; iteration bounds also terminate a stuck engine with a frozen model
clock. Native polling uses IODelay, does not wait for a C2H callback or an event
queued on the same workloop. Platform IO primitives must themselves return;
these bounds cannot preempt an OS call that never returns.

Every attempted write marks `modified`, including writes that might take effect
before a transport error. Failed modified phases set `requiresRecovery`; errors
are sticky and no automatic retry of the full phase is allowed. L1 restoration
is attempted even if its temporary clearing reported an ambiguous failure. It
has a separate bounded 5-ms cleanup allowance, keeps the first error, and
reports `l1RestoreAttempted/l1Restored/l1RestoreFailed`. It does not bypass lost
gate ownership, cancellation or a backwards clock. If L1 was originally clear,
all three flags remain false because no restoration was needed. The original
source can overwrite an earlier MDIO error with the cleanup return; this
implementation deliberately preserves it. Cleanup of L1 is not rollback of
the preceding PHY/MAC configuration. The startup owner must handle recovery or
power-cycle without interpreting that cleanup as permission to proceed.

MMIO reads reject all-ones, the source LTR error value 0xeaeaeaea, and the
additional fail-closed 0xdeadbeef sentinel. Ordinary register writes require
readback; LBC's flag bit is excluded from equality because clearing it can be
write-one-to-clear. No defaults are substituted for inaccessible hardware.

## Verification and remaining integration

`tests/network_pci_link_test.cpp` emulates the byte/word MDIO and DBI engines,
including generation pages and DBI lane enables. Tests cover 48 cut/speed/L1/
native-versus-fallback combinations, preservation of unrelated controls,
source register values, W1C LBC behavior, 580 read/write failure points (both
before and after possible write effects), initial busy engines, stalled
completion, frozen-clock iteration bounds, elapsed-time expiry during reads,
L1 restoration after calibration failure, independent phase budgets,
invalid ordering, repeat-call behavior and ownership/cancellation errors.

Validation commands from the repository directory:

```powershell
& ../toolchain/zig-x86_64-windows-0.15.2/zig.exe c++ -std=c++14 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-trap=undefined tests/network_pci_link_test.cpp -o build/network_pci_link_test.exe
& ./build/network_pci_link_test.exe
& ../toolchain/zig-x86_64-windows-0.15.2/zig.exe c++ -target x86_64-macos-none -mkernel -DKERNEL -DKERNEL_EXTENSION -fno-stack-protector -mno-red-zone -fno-exceptions -fno-rtti -std=gnu++14 -Wall -Wextra -Werror -I ../MacKernelSDK/Headers -c src/network/MacPciLinkIo.cpp -o build/MacPciLinkIo.o
```

The kernel compile explicitly instantiates `Initialization<MacPciLinkIo>` and
therefore checks the entire native/template interface, not only declarations.
It is not a hardware smoke test. Controller integration must call these phases
at the boundaries above and keep startup failure cleanup consistent with the
separate DMA/ring owner. Source pre-init steps after KEEP_REG (DMA stop/poll,
mode setup, index reset, ring addresses, BDRAM reset and firmware channel start)
and source post-init steps after ADDR8B (all-channel enable and PCI IO release)
remain owned by that existing runtime/download path; this module does not
duplicate them. This component also does not implement runtime PCI ASPM policy,
suspend/resume, RF calibration, firmware commands, association or data traffic.
