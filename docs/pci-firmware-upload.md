# 0.0.13: bounded PCI firmware upload experiment

This is an experimental firmware uploader, not a network driver. 0.0.12 reached
PCI stage 3 and stopped before submission; ROM passes, quiescence, power-off,
PCI restoration and memory release passed. Its 30 writes and lack of a readback
failure locate the stop at the BDRAM reset poll, but the last reset value was
not exported. 0.0.13 hardware results are pending.

The reference mac_partial_init opens HCI TX/RX internal gates before PCI pre-init
and BDRAM reset. 0.0.12 instead opened only TX after reset. 0.0.13 opens both
internal gates for reset with bus mastering/TXHCI/RXHCI disabled and all channels
stopped, then closes RX before bus mastering. This corrects an ordering mismatch;
whether it explains the physical timeout remains a hardware hypothesis.
The mock now requires these gates for reset completion and exercises a forced
closed-gate failure. Poll failures preserve register, mask, expected/last values
and reason across cleanup; reset gate/control values and PCI polls are exported.

The same native 165-page bank is prepared and retained. A first ROM/cleanup pass
must succeed, then a second ROM pass invokes the transfer core while WCPU is
ready. The backend checks inactive MSI/MSI-X, bus mastering off, an idle device
and an empty CH12 queue. It configures the 256-entry ring, 32-bit addresses,
CH12 BDRAM, 8852B burst/tag/descriptor settings, resets the CH12 index and BDRAM,
masks chip interrupts, and enables only CH12 TX with PCI INTx disabled. RXHCI,
WPDMA and other implemented TX queues stay stopped. HCI RX is enabled only for
the local reset with host DMA disabled, then closed. No CMAC/RF
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
inject all 200 write failures, active MSI/dirty queue rejection, partial bus-master
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
