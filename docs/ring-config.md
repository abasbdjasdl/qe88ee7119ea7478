# Stopped FWCMD queue configuration experiment (0.0.7)

Target remains PCI 10ec:b852 / 1a3b:5470, digital cut 1, analog revision 0x11.
Hardware evidence for 0.0.6: both 4096-byte mappings were page aligned, single
segment and under 4 GiB. Allocation, prepare, synchronization, CPU verification
and cleanup passed; PCI bus mastering remained disabled. This is not DMA evidence.

0.0.7 keeps those mappings alive while running the already tested supply-on
sequence, performs the following queue configuration experiment, then executes
supply-off, unmaps BAR2/restores PCI Command, and releases the DMA mappings.
An allocation failure skips all supply/queue operations. A supply-on failure
skips the queue callback but still attempts supply-off. Queue failure also returns
through the same supply-off path. No callback retains the mapping after return.

Register facts and layout come from `pci.h`, `pci.c:rtw89_pci_reset_trx_rings`
and `rtw8852be.c` at reference commit
`d1fced1b8a741dc9f92b47c69489c24385945f6e`:
https://github.com/lwfinger/rtw89/tree/d1fced1b8a741dc9f92b47c69489c24385945f6e

| Register | Offset | Access |
|---|---|---|
| PCIE_INIT_CFG1 | 0x1000 | Clear TXHCI/RXHCI bits 11/13 temporarily |
| PCIE_DMA_STOP1 | 0x1010 | Set CH12 stop bit 18 temporarily |
| PCIE_DMA_BUSY1 | 0x101C | Read only; any defined busy bit rejects the test |
| CH12_TXBD_NUM | 0x1038 | 16-bit access; low 12 bits = 256 |
| CH12_TXBD_IDX | 0x1080 | Read only; both indices must be zero |
| CH12_TXBD_DESA_L/H | 0x1160 / 0x1164 | Live ring I/O address / zero high word |
| CH12_BDRAM_CTRL | 0x1228 | Low 24 bits: start 28, max 4, min 1 |

All configuration values are captured first. Invalid reads, nonzero indices,
busy engines or unsafe PCI Command skip writes. HCI-disable/CH12-stop are read
back before writing the queue. Reserved count/layout bits are preserved. The
configured values and unchanged index are verified. All six restoration writes
are attempted, including after partial write/readback failure, and configuration
is compared with the initial snapshot before supply-off. Busy status is recorded
but not treated as a restorable configuration value.

No index/doorbell, pointer-clear, BDRAM-reset, interrupt or PCI bus-master-enable
write exists in this experiment. The mapped buffer contains the same synthetic,
unsubmitted descriptor from 0.0.6. No real firmware is bundled or uploaded.
DMA engines, DLE/HFC initialization, firmware CPU handshake and queue ownership
tracking are still unimplemented. Successful readback does not establish any of
those or that the queue can transfer packets.

Host tests cover every one of 12 writes failing, lost configuration/restoration
writes, invalid MMIO, bad DMA mapping, active PCI bus mastering, busy state,
nonzero indices, register widths and reserved fields. Callback tests confirm DMA
buffers are live during the extension, rejected prepares never call it, and the
power extension only runs after an observed active state with shutdown afterward.
CI adds Apple kernel compilation and ASan/UBSan. 0.0.7 hardware test is pending.
