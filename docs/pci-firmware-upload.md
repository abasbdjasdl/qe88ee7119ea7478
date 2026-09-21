# 0.0.12: bounded PCI firmware upload experiment

This is the first hardware upload candidate, not a network driver. Hardware
results are pending. 0.0.11 passed two ROM preparation/cleanup cycles, bank memory
verification, power-off and PCI restoration in one physical recovery boot.

The same native 165-page bank is prepared and retained. A first ROM/cleanup pass
must succeed, then a second ROM pass invokes the transfer core while WCPU is
ready. The backend checks inactive MSI/MSI-X, bus mastering off, an idle device
and an empty CH12 queue. It configures the 256-entry ring, 32-bit addresses,
CH12 BDRAM, 8852B burst/tag/descriptor settings, resets the CH12 index and BDRAM,
masks chip interrupts, and enables only CH12 TX with PCI INTx disabled. RXHCI,
HCI RX DMA, WPDMA and other implemented TX queues stay stopped. No CMAC/RF
registers are enabled. PHY/link calibration quirks and networking are not ported;
the experiment can fail at explicit readback/timeout gates on real hardware.

All register writes use fixed allowlists and all meaningful configuration is read
back before DMA. Each producer publication has a memory fence. The immutable
batch sends one header, waits for FWDL readiness, clears halt controls, then
sends 163 section packets, waits 5 ms and requires firmware state 7 and a drained
ring. No descriptor buffer is reused during the batch. This groups queue setup,
header acceptance, firmware checksum/startup and cleanup into one hardware boot.

Exit always attempts to stop HCI/channels and disable bus mastering, including
partial-start/publish failures. Memory release requires proven idle + bus-master
off; queue addresses are restored only after that proof. A callback actually
completes/unmaps/releases the native bank, so protocol release is not a dummy
success. If quiescence fails, buffers are retained until the outer verified
power-off (and bus-master-off) permits release; otherwise they remain allocated
until reboot. INTx remains disabled until WCPU and supply are off. Chip interrupt
masks remain zero throughout this diagnostic transaction and power-off.

The unchanged ROM first-pass result, second-pass result, upload status/failed
phase, producer count, control/index values, stop proof, PCI stage and failure
address/readback are all exported through IOReg. FirmwareUploaded requires the
full protocol to complete; WiFiOperational remains false. No scan, association,
WPA authentication, network interface or network traffic is implemented.

Portable tests run the real 164-packet fixture through the register backend,
inject all 199 write failures, active MSI/dirty queue rejection, partial bus-master
enable, lost header/firmware readiness, stuck clocks/reset, persistent DMA busy
and failed bus-master disable. The last two assert no release callback occurs.
Apple CI adds sanitizers and native kernel compilation. Mocks are not hardware
validation or a proof of complete rtw89 initialization equivalence.

Collector changes remove diskutil/direct-mount races, refuse duplicate FAT mounts,
reuse one output directory and unmount the directly mounted volume after sync.
This addresses a plausible allocation-cache hazard, not a proven attribution of
the observed EFI cross-links. A disposable FAT image on macOS CI verifies mount,
collection, persistence, unmount, and automatic error/timeout exit paths.

Reference: rtw89 pci.c/pci.h, mac.c/mac.h, fw.c and reg.h at
d1fced1b8a741dc9f92b47c69489c24385945f6e (BSD option).
