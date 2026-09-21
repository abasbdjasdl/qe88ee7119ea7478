# Supply-cycle experiment (0.0.5, hardware test pending)

Source ordering: rtw8852b.c `rtw8852b_pwr_on_func` / `rtw8852b_pwr_off_func`,
mac.c `rtw89_mac_write_xtal_si_ax`, reg.h and mac.h in
https://github.com/lwfinger/rtw89/tree/d1fced1b8a741dc9f92b47c69489c24385945f6e .
Realtek Corporation 2019-2022 copyright is retained; this adaptation selects the
BSD-3-Clause option of the dual-licensed source. The project's LICENSE contains
the BSD conditions/disclaimer; it applies together with that copyright notice.

The already tested 0.0.4 preflight must complete, report digital cut 1 and analog
byte 0x11, and have PCI bus mastering off. The power experiment additionally
requires IC_PWR_STATE.WLMAC_PWR_STE=0 and the PMC debug write-mask bit clear.
Active, transitional, invalid or unknown initial states cause a skip without a
power write. No already active MAC is shut down just to run this experiment.

The on table ports the supply/XTAL/isolation sequence up to (not including) the
reference `func_en` label. EFUSE power calibration is unavailable and its optional
voltage overrides are excluded, as in the reference's initial invalid-EFUSE path.
DMAC/CMAC function enables, DMA queues, firmware upload, radio calibration and
networking are not implemented. Register writes are limited by width and address
allowlists; PCI bus-master enable is never set. The purpose is to observe state 1
after the supply sequence, then shut the MAC down again in the same call.

Every attempted on sequence is followed by the reference shutdown sequence,
including partial on failures. It excludes the RFE-05 regulator special case
because EFUSE/RFE type is not known. No guessed RFE voltage adjustment is applied.
The temporary PMC write-mask bit is explicitly closed after the shutdown attempt.
Success requires the shutdown sequence to complete, MAC state 0, SWLPS set and
the write mask clear. This is a shutdown verification, **not** an exact restoration
of all register values or proof of later firmware compatibility.

Each poll has both an elapsed-time deadline and an iteration cap, each table
has a one-second overall deadline, and waits are at most 1000 us. Busy/invalid
XTAL controls stop that sequence; no command retry or forced reset is attempted.
Cleanup failure remains a distinct result and must not be treated as success.
The outer MMIO transaction still unmaps memory and restores its PCI command bit.
An unresponsive bus access or kernel fault cannot be recovered by these timers.

Tests emulate MAC on/off acknowledgements and the XTAL protocol, inject failure
at all 54 nominal register writes, inject on/off/XTAL timeouts, stalled clocks,
long scheduling gaps, invalid reads and forbidden initial states. They assert
cleanup outcomes and PCI restoration. These host tests cannot establish the
physical power sequence works; the installed collector must record the next boot.

Error integers: 0 none, 1 invalid read, 2 rejected write, 3 XTAL busy,
4 poll timeout, 5 overall deadline. SupplyOnStep/SupplyOffStep index their tables
from zero; on success they contain the completed table length.
