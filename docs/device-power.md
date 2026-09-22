# Complete RTL8852B chip power functions

`DevicePower<MacDevicePowerIo>` executes the complete chip power-on/off functions
from pinned rtw89 `d1fced1b8a741dc9f92b47c69489c24385945f6e`, not the diagnostic
supply-only sequence. `import_device_power_reference.py` imports the three source
functions and their constant closure, records original/source/generated hashes,
and retains the BSD option. Generated enum constants do not leak preprocessor
definitions into other modules.

Power-on covers system power readiness, AFE/SPS setup, the ONMAC handshake,
platform toggles, XTAL analog sequencing, isolation/PMC write protection,
calibration-dependent voltage adjustment, full DMAC/CMAC enables and BT log
pinmux. A validated calibration snapshot supplies cut, RFE and the PHY power-K
bit. With no snapshot the source's initial-probe voltage behavior is retained;
only cut B receives the calibrated HCI LDO adjustment. Power-off includes RF
shutdown, the OFFMAC handshake and software LPS. RFE 5 receives the source's
16-bit SPS adjustment only when the caller marks the initial probe shutdown.

This is a one-shot pair owned by a future device controller. It requires a live
RTL8852BE BAR2 mapping, the workloop gate, PCI memory decoding enabled and bus
mastering disabled. Startup rejects an already active MAC or an already-open
PMC write mask. Shutdown of an active MAC additionally verifies HCI DMA stopped,
no DMA busy bits and all three host interrupt masks cleared. The caller must
also drain host callbacks and leave firmware power-save before shutdown; these
conditions cannot be established by inspecting interrupt masks alone. Buffers,
interrupt sources and firmware command epochs are not released/reset here.

Each action has a one-second monotonic deadline and a finite operation budget.
Source polls retain their 20 ms/50 ms deadlines and reject readiness sampled
after expiry, even when an I/O call itself consumed the time. Frozen clocks
remain bounded by iteration counts. XTAL commands first drain a busy serial
port and then perform masked readback. The native opcode/address allowlist
excludes OTP/eFuse programming. Added readback is verified in software models;
physical chip timing and post-shutdown XTAL accessibility still need hardware
validation.

The first error is preserved. Cancellation stops normal startup immediately,
while explicit bounded shutdown and narrow PMC-mask cleanup can still run on
an accessible device. Cleanup may close only a mask this object attempted to
open, and cannot bypass PCI/gate checks. A partial or uncertain operation keeps
`requiresRecovery` set until successful power-off. `returnedOff` proves the
observed chip power state and sequence only; it is not evidence of drained host
callbacks, reclaimed DMA, reset firmware epochs, or a complete recovery path.

The model test covers all 166 power-on and 102 power-off read/write failure
positions, both cuts and calibration branches, RFE/probe distinctions, lost
device, cancellation, timeout/frozen/backward clocks, ignored register writes,
late readiness and active DMA/IRQ rejection. The native test compiles the actual
I/O implementation against IOKit fakes; kernel-target compilation checks SDK
compatibility. Neither establishes physical power-up, MAC/RFK readiness or
network connectivity. Top-level lifecycle integration is still required.
