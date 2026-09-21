# DMA memory preparation experiment (0.0.6)

This stage does not submit DMA. BAR2 is unmapped and the PCI command restored
before allocating memory. Bus master enable is checked before and after and
never written by this component. The previously validated power-cycle code is
retained in source/tests but not executed by this version.

Two 4096-byte IOBufferMemoryDescriptors are allocated with a 32-bit page-aligned
physical mask. Each memory descriptor is prepared, then bound to an IODMACommand
using OutputHost64, 32 address bits, kMapped, a 4096-byte maximum segment and
4096-byte alignment. IOMapper::copyMapperForDevice supplies the PCI device mapper
when present; a null mapper uses IODMACommand's documented system mapper default.
The current OpenCore configuration has DisableIoMapper=true, but this code does
not change that configuration or substitute a CPU pointer for an I/O address.

gen64IOVMSegments must return exactly one segment covering the entire page, advance
its cursor by 4096 and report an aligned, nonzero address with the complete range
below 4 GiB. The two mappings must not overlap. This verifies API output bounds;
it does not establish that the device can access the addresses.

The first page reserves a 256-entry, 8-byte TXBD ring. Entry zero is encoded as
length 2044 (24-byte descriptor plus maximum 2020-byte firmware payload), LS bit
14 and the second page's DMA address in little endian. This byte layout comes
from pinned rtw89 pci.h:rtw89_pci_tx_bd_32 / pci.c:rtw89_pci_fwcmd_submit. The
second page holds synthetic data, not an executable firmware image or complete
TXWD header. No descriptor address, producer index or doorbell is written to the
device. Both mappings are synchronized outbound and CPU bytes are checked.

The slots are cleared in reverse acquisition order, including partially prepared
ones. IODMACommand::clearMemoryDescriptor(true) completes/unmaps an active command;
the explicit memory descriptor prepare is paired with complete, then the objects
are released. Cleanup failure is distinct from allocation/mapping/test failure.
Allocation and mapper operations may block; no work-loop gate or interrupt context
is used. A CPU-content check is not a DMA or IOMMU hardware test.

Host tests cover exact TXBD bytes, 4 GiB boundaries, partial/unaligned/overlapping
segments, both slots failing allocation/prepare/synchronize/cleanup, corruption
and a bus-master-active skip. The Apple build validates kernel API declarations.
Only a subsequent physical boot can validate runtime symbol resolution and
allocation/mapping behavior in the recovery kernel.

SDK: https://github.com/acidanthera/MacKernelSDK/tree/05094e5e88cec7caedbfb35e8449ed0db94bf95b

Hardware format: https://github.com/lwfinger/rtw89/tree/d1fced1b8a741dc9f92b47c69489c24385945f6e
