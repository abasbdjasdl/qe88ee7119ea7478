# Native lifecycle recovery guest

This guest is a test path for real IO80211 initialization/registration/teardown
before deploying anything to the physical recovery EFI. It does **not** emulate
the RTL8852BE and cannot prove real scanning, association, DHCP or Wi-Fi menu
connectivity. Those still need the actual single-owner PCI driver and hardware.

The VM output directory in this task is
`C:/Users/Work/Documents/Codex/2026-09-21/wu/outputs/R16-Native-VM`.
No physical disks, PCI devices, host directories or NIC are exposed. Every
guest drive uses a disposable snapshot. QMP listens on loopback port 51916;
the helper verifies the guest name before issuing commands. Launch refuses
an occupied port; a missing PID record or delayed response is not a restart
instruction. `quit` terminates only this named guest, never Windows.

Inputs:

- Existing QEMU 11.0.50 installation in `C:/Program Files/qemu`, with its
  bundled x86_64 EDK2 firmware and copied variable template.
- [OSX-KVM OpenCore image](https://github.com/kholia/OSX-KVM/tree/4c378a4b5e0b219783683012bec680325eb40719/OpenCore)
  at pinned commit `4c378a4b5e0b219783683012bec680325eb40719`, 19,398,656 bytes.
  The source QCOW2 has no backing file. This is guest firmware, not a driver
  implementation or proof of compatible private kernel APIs.
- Original Apple recovery 15.4.1 / 24E263:
  `R:/recovery15-original-autolog/BaseSystem.dmg`, SHA-256
  `7314eb401f5e84087f621b3599f0ad21ca3cdcc2685ea2da7f76806792328e20`.
  This was rehashed and matches the existing Apple chunklist verification.
  `qemu-img convert -f dmg -O qcow2` creates the VM's `BaseSystem.qcow2`.

`prepare_debug_boot.py` uses a local raw conversion and the extracted first
GPT partition (`inspect/0.primary.img`). It changes only the unique contiguous
config.plist extent in a new image, preserving the FAT file length and metadata.
It refuses missing/ambiguous extents or a replacement that does not fit.
Original recovery, original boot image, physical EFI and firmware settings
are not written. The recorded patch manifest identifies the changed fields.

The initial WHPX boot reached XNU but stopped before normal serial output.
QMP guest-memory inspection captured `Unsupported CPU @%s:%d` and `cpuid.c`
in the early panic arguments (`whpx-early-strings.json`). This predates any
RTL8852BE driver load. The `whpx` boot variant therefore applies the same
CPUID compatibility value (`55060A00` with a first-word mask) already used
by this task's physical-machine OpenCore configuration. The ordinary debug
variant changes only boot diagnostics and picker mode. Neither variant
patches IO80211 method bodies or vtables.

Example commands, using the Python 3 runtime (Windows' default Python is 2.7):

```text
python3 tools/native_vm/recovery_vm.py launch --boot-name OpenCore-whpx.qcow2 --root <output>
python3 tools/native_vm/recovery_vm.py status --root <output>
python3 tools/native_vm/recovery_vm.py screenshot --root <output>
python3 tools/native_vm/recovery_vm.py key --key 2 --root <output>
python3 tools/native_vm/recovery_vm.py quit --root <output>
```

Only send menu keys after inspecting the guest screenshot. `launch.json`
records the exact process arguments, PID and image hashes; future launches
archive preceding logs. A running QEMU process alone is not lifecycle
validation or completion of the native Wi-Fi goal; specific execution evidence
is recorded below.

## Observed guest, 2026-09-23

The WHPX guest reached Recovery and Terminal. Guest `sw_vers`, `uname -r`
and `sysctl kern.uuid` reported 15.4.1 / 24E263 / 24.4.0 and kernel UUID
E6326809-88F4-3ECC-93BB-D2CBDF235588. Initially `kextstat` showed only
IOSkywalkFamily. `kextload -b com.apple.iokit.IO80211Family` returned normally;
the subsequent list showed these loaded libraries:

| Component | Version | Loaded text UUID |
| --- | --- | --- |
| IO80211Family | 1200.13.1 | B193F4B7-5A7F-33B7-A667-87112588AAE7 |
| IOSkywalkFamily | 1.0 | 9AF084D1-884F-38C5-B07D-4B362219FDC6 |
| corecapture | 1.0.4 | 4DBEBF77-F63C-3A84-B8D4-50C8E510BCA3 |

The screenshot `loaded-native-dependencies.png` in the output directory is
the evidence, not a mock. These are Apple's libraries, not our controller.
The UUIDs match the pinned profile. Writing to `/dev/cu.serial1` blocked and was interrupted,
so use screenshots or kernel IOLog, not that device, for collection.

`guest_keyboard.py` sends ASCII using the verified QMP guest keyboard; inspect
the guest Terminal first. Ctrl-F2, four Right arrows, Down, Down, Enter opens
Terminal from the Recovery menu. Mouse pointer motion worked, but menu clicks
did not open Utilities in the observed guest.

`build_lifecycle_probe.py` generates an independent `R16NativeVMProbe.kext`
only with a macOS linker. Its IOResources harness requires `r16vmtest=1` and
all four runtime UUIDs to match before allocating an IO80211-derived object.
It calls init/release only, has no PCI match and cannot claim radio operation.
The generated vtable is checked against all 466 pinned entries. The Windows
`--zig` mode currently performs compilation/table checking only.

The separate `lifecycle` boot-image variant adds that test argument and
`csr-active-config=0x3` only to disposable VM NVRAM, allowing its unsigned test
kext. It is not suitable for copying to the physical EFI. Even successful
init/free will not prove controller start/stop, station registration or Wi-Fi.

## Native controller init/free actually executed

The first load attempt (`4097cc8`, run 35833569850) was rejected because three
OSKext private methods were not exported. The replacement (`ec658ea`, run
35834755322) uses exported sysctl and retained dependency Mach-O headers. A
manual recovery load then reached a missing SPKernelExtensionPolicy service;
it was not counted as execution. `inject_probe.py` therefore inserted the
VM-only bundle into a new FAT image copy, `OpenCore-probe-init-v2.qcow2`.

With that image, the real target kernel logged all three dependency UUIDs
matching, identity status 6/component 4, controller init result 1,
`IO80211Controller::free start`, `IO80211Controller::free end`, and release
returning. It subsequently reached Recovery. A `waitForSystemMapper`
diagnostic backtrace occurred during early init before ACPI enumeration;
init resumed after enumeration. This is not a tested full start/stop path.

The running probe binary SHA-256 is
`2a507cb0d79920394303c7c7cef22bf1d9f1d938391a22198de1597bca0fabe7`.
The replacement collector body is now shared in
`tools/native_abi/native_runtime_identity.cpp`; its local host failure tests
and O0/O2 exact-KC/import audit passed. No native station, Skywalk data queues,
menu SSIDs, association or hardware networking were exercised.

`init-free-execution.json`, its log snapshot, and
`init-free-registry-and-unload.png` preserve this run's evidence. Registry
properties confirmed both init and release, and explicitly reported native
Wi-Fi as false. `kextunload -b local.r16.nativevm` was rejected with
`unsupported by cache`; the harness stop callback did not run. Keep that
limitation separate from the controller object's observed `free` completion.

For injection, install `pyfatfs==1.1.0`, `fs==2.4.16`, and `setuptools==80.9.0`
into the dedicated VM Python package directory. The script only edits a new
raw VM-image copy, reads back all inserted bytes, then converts it to QCOW2.
The earlier incomplete `OpenCore-probe-init.raw` failed before conversion;
do not boot or reuse that raw copy. The verified image is the `-v2` QCOW2.
The optional `make_probe_iso.py` transport uses `pycdlib==1.20.0` and contains
only the probe bundle, not a shared host directory.

## Contained base lifecycle experiment

`build_lifecycle_probe.py --start-probe` additionally compiles the audited
startup support factories and gives the generated controller an embedded,
durable StartupLedger. The normal `r16vmtest=1` still runs init/free only.
`inject_probe.py --stage 2` selects `r16vmtest=2` in a new VM image; this
attaches the test controller to the IOResources harness, prepares its queue,
logger and fault reporter, and enters the actual IO80211 base start. Only
after a successful base start does it call base stop once.

Every stage is logged. Support objects and controller/provider references are
deliberately retained after the experiment, including on ambiguous failures.
This lets the disposable VM observe the base calls without freeing an owner
that may still have callbacks. It is not safe rollback, leak-free shutdown,
PCI removal handling, or a deployable native driver. The result explicitly
marks complete cleanup and native Wi-Fi as unverified. No station or fake
SSID is registered. A successful base call must be followed by real support
ownership/teardown work before any physical deployment.

The first stage-2 boot (`b2f2313`) created all startup support objects, logging
`support prepared=1 phase=2 failure=0`, then entered base start without a
return observed. The exact KC's IONetworkController::start at
`0xffffff8002624fb4..2624fc7` waits for the resource string `IOBSD` (at
`0xffffff80026315df`). The early IOResources/IOKit personality was therefore
changed to IOResources/IOBSD for the start experiment (`0c4b289`). This moves
the experiment after its superclass prerequisite instead of overriding or
bypassing the system's resource wait. The earlier log is preserved as
`start-before-iobsd.log`; it proves successful support creation, not base start.

The IOBSD-triggered run (`0c4b289`) passed support preparation but panicked in
`IONetworkController::start`, before its return. `start-iobsd-panic.log`
preserves the evidence: `ml_thread_policy` called `IOLockLock` with address
`0x648`. The exact KC shows the controller comparing its workloop with its
provider's at `0xffffff800262512e..2625144`; when different, it calls
`getThread` and then `ml_thread_policy` at `2625146..2625163` without a null
check. This KC's `IO80211WorkQueue::getThread` at `2253a20` explicitly returns
zero. It is not a failed queue allocation or an uninitialized thread.

The VM harness now exposes the same borrowed queue as its attached controller
before base start, and verifies the equality. This matches the provider
wrapper relationship in the pinned reference's
`AirportItlwm/IOPCIEDeviceWrapper.cpp::getWorkLoop`. The retained ledger owns
the queue throughout this contained experiment. The physical frontend must
also establish this provider relationship; the harness fix alone does not
implement it in the hardware driver or prove teardown safety.

The corrected run (`9758d6e`, Actions run `35837610171`) logged
`provider/controller same workqueue=1`, `base start result=1 phase=4 failure=0`
and `base stop returned`. Evidence is saved in
`shared-provider-start-stop.log` and its JSON record with binary/log hashes.
This establishes the base calls returning in the exact Recovery kernel. The
experiment still retains support objects and does not call controller free
after base stop, publish a station or test the physical network. Complete
cleanup, provider removal and native Wi-Fi remain unproved.
