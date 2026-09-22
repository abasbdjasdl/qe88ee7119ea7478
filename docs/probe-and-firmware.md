# Concrete two-cycle macOS firmware preparation

`network::MacProbeAndFirmware` owns heap-allocated concrete native power, firmware,
PCI-link, calibration, mailbox and MAC initialization components. It is one-shot;
`stop()` is terminal even before `prepare()`. PCI device, BAR2, workloop and
immutable firmware bytes are borrowed and must outlive `release()`.

`allocate(device, map, loop, bytes, length)` runs outside the workloop gate. A
short public `IOWorkLoop::runAction` reads PCI command and reads SYS_CFG1 `0xf0`
twice under the gate, requiring matching `[15:12]` cuts 0/1. Image selection uses
that actual cut. Two independent download banks are allocated before preparation,
outside the gate. Monotonic nonzero epochs are reserved for both physical firmware
incarnations, without recycling epochs between service objects.

`prepare()` runs inside the gate, with no active runtime IRQ/timer callbacks:

1. Recheck cut; real power-on without a calibration snapshot; stop/mask host DMA
   and prove BM-off; prepare DLFW DMAC; PCI-link pre-init; clear the stopped runtime
   ring bases/counts/indices; start CPU; download firmware; accept only firmware
   ready plus successful download DMA quiescence/release. CPU remains enabled.
2. Read DDV, DAV and physical PHY calibration through `MacDeviceCalibration`;
   send GET_FEATURE and validate PHY_CAP on the same live register mailbox.
3. Invalidate first-cycle capabilities, prove DMA stopped again, stop WCPU and
   power off using the probe-stage branch. A failed shutdown blocks cycle two.
4. Recheck the same cut; power on using the real first-cycle calibration snapshot;
   repeat pre-init/CPU/download with independent one-shot objects and DMA bank.
   Query PHY_CAP again with the second firmware epoch, so current capability
   evidence belongs to the live firmware rather than the stopped probe firmware.
5. Configure actual MAC cut; enable BB/RF and system; initialize SCC DMAC, CMAC
   and release-report registers; perform PCI-link post-init. Leave WCPU alive,
   host DMA stopped, BM-off, IRQ masks zero, and runtime ring bases/counts/indices
   zero for the controller's separate runtime allocation/publication.

Source order is `core.c:4533`/`mac.c:3894` for the probe cycle and
`mac.c:3931` for persistent startup, at rtw89 revision
`d1fced1b8a741dc9f92b47c69489c24385945f6e`. This port deliberately pauses DMA between
download and MAC/runtime initialization; it does not call the diagnostic probe
wrapper that unconditionally shuts the CPU down after its action.

`calibration()`, `capabilities()`, `firmwarePlan()`, `cut()` and `epoch()` provide
the actual results. `mailbox()` returns the **existing second-cycle mailbox**,
only after successful prepare, for scheduler/RFK users; do not create a competing
mailbox or host-counter owner.

After runtime CH12/RX dispatch is alive, call
`beginFirmwareConfiguration(sharedCommands)`. It sends source OFLD_CFG
`{category=1,class=9,function=0x14}`, payload `09 00 00 00 5e 00 00 00`, Done ACK
requested (`fw.c:3765`). `offloadConfigurationPending` becomes false only after a
matching successful Done ACK with the same command sequence and firmware epoch.
Submission failure, firmware rejection and the shared command owner's timeout
remain failures. The shared command owner must stay alive through `stop()`;
`stop()` invalidates it before the callback owner can be released. The controller
must continue servicing that command owner and must not interpret submission as
completion.

Other source post-init work is explicit:

- `mac.c:3698 rtw89_mac_feat_init` returns immediately for 8852B BACAM_V0;
  this chip needs no BA-CAM-V1 initialization command there.
- `fw.c:5572 rtw89_fw_send_all_early_h2c` sends a debugfs-injected list.
  `core.c:4348` initializes it empty; `debug.c:3328` is the injector. This port
  exposes no such injector, so the real list is empty, and `earlyH2cPending` clears
  after MAC/PCI preparation without claiming an unissued command completed.
- `radioCalibrationPending` remains true: this module does not claim RFK, BT,
  regulatory/channel readiness or working Wi-Fi. Its owner must complete those
  separate phases before enabling traffic.

`stop()` requires the caller to disable/drain runtime events first. It invalidates
capabilities/command callbacks, masks/stops DMA and disables BM, then stops CPU
and powers off only after the DMA-idle proof. A possibly device-visible download
bank stays retained on failure, even if a later stop seems successful. `release()`
is outside-gate destruction and refuses ambiguous live hardware or retained DMA;
the caller must retain the provider/BAR/service in that case. The destructor
does no emergency MMIO outside the gate and logs/retains ambiguous state.

Validation so far: native x86_64 macOS kernel cross-compilation of the concrete
composition with warnings-as-errors, plus component regressions. The entire
two-cycle native composition has not yet been run on physical hardware or a
combined end-to-end MMIO model; no end-to-end successful boot is claimed here.
