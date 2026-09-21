# 0.0.11: native packet bank and download-ROM preparation

0.0.10 hardware stopped at phase 1: HCI 0x1000 stayed 0x15f00 after clearing
mask 0x2800. Early cleanup, supply-off, PCI restore and the bank passed; no ROM
start or clock-cleanup test occurred. Initial MAC function state was not recorded.
The supply diagnostic omits the reference power-on func_en tail, and HCI writes
were incorrectly placed before the minimal MAC enable. Reference mac.c runs
mac_dmac_pre_init before PCI mac_pre_init. 0.0.11 first writes/verifies minimal
0x8400=0x60440000 and dispatcher clock 0x8404=0x40000, with PCI bus mastering off.
It then captures the accessible HCI/stop baseline, pauses HCI, and continues DLE/
HFC/ROM initialization. HCI restoration is checked before shutting the MAC window.
No CMAC/RF or PCI bus-master enable is introduced. Initial and enabled access
values are logged. This is a source-supported correction; physical verification
is pending, not a confirmed diagnosis of an unrecorded initial gate bit.

One successful first pass with verified cleanup triggers one confirmation pass
from disabled MAC access in the same supply cycle. There is no retry on failure.
Both outcomes are published; the analyzer requires both to pass. Tests cover
initial MAC access disabled/enabled, plausible gated HCI shadow reads, silently
ignored gated writes, restore timing, and every first/second-pass write failure.

0.0.9 reached ROM_H2C_READY on hardware (control 0x23), with the complete bank,
WDE/PLE/HFC, DMAC and clock checks passing. Cleanup alone failed at 0x8404:
it returned 0xffffffff after 0x8400 MAC_FUNC_EN was cleared. CPU stop, outer
power-off, PCI command restoration and memory release all passed. This does not
prove the earlier clock-clear write took effect. 0.0.10 clears and reads back
CLK_EN while MAC register access remains enabled, then disables/reads FUNC_EN.
The all-ones value remains a failure. If the initial MAC enable failed, cleanup
does not reopen it solely to check clocks and retains an unresolved status.
CleanupClockChecked, CleanupClock and CleanupDmac record the actual verification.
Register failure values are published as zero-extended 64-bit IOReg numbers.
Tests model the post-MAC-disable inaccessible window and silently dropped clears.

0.0.8 hardware passed all bank checks, but stopped at the phase-1 stop-control
readback (BOOT_WRITE_FAILED); its unconditional cleanup also failed verification.
Supply-off, PCI restoration and memory release succeeded. No ROM or firmware
upload ran. The log lacks the failed register value, so the exact rejected bits
are not established. Source review identified use of the generic TX channel mask
plus STOP_RXQ/RPQ, whereas rtw8852be uses TX V1 (0x70f00) and RXHCI_EN.

0.0.9 uses that chip-specific TX mask with global TX/RX HCI disabled, preserves
other register bits, and checks busy bits for the implemented channels. Cleanup
only touches blocks whose initialization was attempted, and verifies HFC before
disabling its parent DMAC clocks. It publishes HCI/stop snapshots, first failed
readback address/mask/expected/actual, and a cleanup-failure bitmap (HFC=1,
DMAC=2, clock=4, CPU config restore=8, HCI=16, stop=32, write=64,
CPU stopped=128, PCI command=256). The operation result remains distinct from
cleanup. Simulations cover absent-channel/RX-stop bits and reserved bits latched
as in the earlier 0.0.7 hardware snapshot; this is not physical proof of the fix.

This is a combined hardware experiment, not a working Wi-Fi driver or an upload.
0.0.7 established stopped queue configuration readback and restoration. 0.0.8
uses the pinned immutable firmware container to prepare its complete 164-packet
batch in native IOKit memory (165 pages including the ring), then initializes
the chip's download packet-buffer engines and asks the ROM for H2C readiness.
It shuts those blocks down before the existing supply-off and memory cleanup.

## Native memory

`FirmwareBank.hpp` prepares one descriptor-ring page plus one distinct 4096-byte
page per firmware packet. All segments must be contiguous, aligned, under 4 GiB
and pairwise distinct. Native allocation/mapping uses `KernelPacketMemory.hpp`
and the IOKit APIs already exercised in 0.0.6. Metadata is heap-allocated; no
165-page array or firmware blob is placed on the kernel stack. Every page is
zeroed, encoded, synchronized and CPU-verified. Packet pages synchronize before
the ring. No device register receives these addresses in this experiment.

Release completes/unmaps in reverse acquisition order. A failed completion or
unexpected PCI bus mastering retains unresolved objects until the automatic
reboot instead of releasing potentially referenced pages; BankCleanupOK is false.
This diagnostic-only release method must not be reused for active DMA. A future
uploader must preserve the bank owner until stop/idle is independently proven.

The unchanged 1,245,944-byte container is embedded in the kext binary. The bundle
contains its full Realtek license, source URL, size and SHA256 in Info.plist.
The validator checks the exact embedded bytes and license. Firmware packets are
generated in memory; no modified firmware file is distributed. This version does
not upload them. The original fixture SHA remains
5b68415e3bfe72715d63a70703d4471b04b5475b8ff69cfdc8cdf48233cd5d3a.

## Chip initialization and exit

`FirmwareBoot.hpp` runs only inside the existing exact-device/cut/analog-revision
and supply-active gates, with PCI memory decoding on and bus mastering off.
It stops HCI/channels and checks DMA idle before touching DMAC. It follows the
8852B `rtw89_mac_dmac_pre_init` DLFW configuration: MAC/DMAC/dispatcher/packet
buffer functions, WDE and PLE clocks, 64-KiB WDE and 128-KiB PLE layout, the
reference DLFW quotas (including SCC-derived WCPU minimum 48), WDE/PLE ready
polls, HFC H2C preallocation 40 and channel-12-only flow control.

It then follows `rtw89_mac_disable_cpu_ax` / `rtw89_mac_enable_cpu_ax` for 8852B:
stop WCPU and CPU clock, reset APB wrapper/platform, clear mailboxes, enable CPU
clock, clear prior download/readiness state, select IDMEM size 2, set boot reason
0 and start WCPU. The download-ready state must be freshly reset before start.
H2C readiness is bounded at 400 ms / 8001 polls. WDE/PLE and DMA-idle polls are
bounded at 2 ms. A stuck or backward clock cannot cause an unbounded loop.
ROM error states 2/3/4 and stale firmware-ready state 7 are rejected.

Every attempted path runs stage-scoped cleanup: stop WCPU, clear download/path bits, disable
CPU clock/HFC/DMAC functions/clocks only if attempted, restore security sizing, boot reason and HCI
controls, and verify shutdown. Internal DLE quotas/mailboxes are initialization
state, not a restorable snapshot; the outer tested supply-off always follows.
Neither CMAC/RF functions nor PCI bus mastering are enabled. No ring index,
doorbell, EFUSE programming or flash write is exposed by the adapter.

ROM_H2C_READY means the on-chip ROM accepts a header, not that external firmware
was uploaded or that Wi-Fi is initialized. Upload still requires PCI pre-init,
DMA submission/ownership, and verified stop/idle; radio and macOS network stack
integration follow that. The bank, DLE/HFC and ROM are grouped in one boot because
their real behavior cannot be established by host tests.

## Evidence

Host tests inject every packet allocation/prepare/sync failure (165 slots),
overlapping/misaligned mappings and corruption. ROM tests inject all 60 writes
failing, stuck timers, missing WDE/PLE/H2C readiness and ROM errors, and verify
the configured layout/shutdown. CI adds ASan/UBSan and Apple kernel compilation.
Physical 0.0.11 results are pending.

Reference source: rtw89 mac.c/mac.h/reg.h and rtw8852b.c at
d1fced1b8a741dc9f92b47c69489c24385945f6e, BSD license option.
https://github.com/lwfinger/rtw89/tree/d1fced1b8a741dc9f92b47c69489c24385945f6e
