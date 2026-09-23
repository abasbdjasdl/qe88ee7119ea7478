# Native service lifecycle boundary (Darwin 24.4, offline)

This map targets only the x86_64 macOS 15.4.1 recovery
`BootKernelExtensions.kc`, SHA-256
`d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
The addresses below are unslid KC addresses. Nothing here has been linked into
the network kext, loaded, registered, or shown in the native Wi-Fi menu.

## Current boundary

The working `Info-Network.plist` matches the RTL8852BE PCI device as
`R16RTL8852BE` and depends on IONetworkingFamily, but has no native
IO80211/Skywalk personality or dependency. `tools/build_network_stack.py`
compiles `src/network/*.cpp` and deliberately does not compile
`tools/native_abi/*.cpp`. It links with `-undefined dynamic_lookup`, so a
successful Mach-O build alone would not establish that every private symbol is
available through a permitted kext dependency on the running system.

`startup_prototype.cpp` checks support-object factories, the base controller
entry and five virtual slots; `infra_frontend_prototype.cpp` checks the whole
InfraProtocol vtable and three registration helper signatures. They are
separate offline objects. In particular, the Infra constructor, dummy
callbacks, and unbound scan bridge must not be used as a loadable class.

## Dependency graph for a real station service

The arrow means the item on the left is a prerequisite of the item on the
right. “Compiler checked” describes only the emitted object, never execution.

| Prerequisite -> operation | Fixed-KC evidence and local artifact | State |
| --- | --- | --- |
| Runtime KC/profile and permitted dependency exports -> any private call | `darwin24_4.json`; registration helpers are external symbols in `registration-symbols.json`, but the current kext has no IO80211/Skywalk/CoreCapture library declaration and has not been linked/loaded against this profile | **Unproved** |
| Real controller `init` and durable owner -> support factories | Controller superclass size `0x128` and pure slots in `lifecycle-findings.md`; `startup_prototype.cpp` owns the ledger and retains controller/provider before factory entry | Compiler checked, runtime init and external stop unproved |
| WorkQueue, CC logger, data stream, raw reporter and wrapper -> `IO80211Controller::start` | WorkQueue raw slot 399/vptr `+0xc68`; logger raw 426/`+0xd40`; fault wrapper raw 434/`+0xd80`. Null wrapper reaches panic at `0xffffff800221e07d`; `startup-contract.json` validates factories and slots | Compiler checked, live construction unproved |
| Successful controller base start -> native station creation | `IO80211Controller::start` `0xffffff800221d268..221d845`; ten explicit failed stages do not roll back the whole state; `native-support-lifecycle-findings.md` | **Unsafe to promote** without partial-start containment and stop proof |
| Valid InfraProtocol instance and controller provider -> Infra `init/start` | Metaclass chain `IO80211InfraProtocol -> IO80211InfraInterface -> IO80211SkywalkInterface`; provider must dynamically cast to IO80211Controller at `0xffffff800225beee`; station requires real controller work queue at `...225bf23` | Full vtable compiler checked; instance initialization not exercised |
| Infra role/id, provider attach and controller attach -> registration | Concrete station factory/init at `0xffffff80015658c5..1565a3c`; controller `attachInterface` at `0xffffff8002222430`; role 1 is the station path | Call route statically observed, no local runtime owner |
| Correct `0x130` registration description, real pools/queues/callbacks -> `registerInfraEthernetInterface` | `initRegistrationInfo(info,1,0x130)` `0xffffff800297bb6e`; Infra register `0xffffff80022d675e` returns full 32-bit IOReturn (zero success), uses **mutable** description; queue/pool factories and signatures in `registration-symbols.json` | Calls compiler checked; values, packet ownership and work-source lifecycle unproved |
| Successful registration and service publication -> BSD/WCL discovery | Role-1 concrete start calls `deferBSDAttach(true)` at `0xffffff8001565f16`; register path at `...15663c0` builds a QueueSet/LogicalLink but does not by itself prove deferred BSD attach completion or WCL discovery | **Unproved** |
| WCL request reader, real event sender, power/MAC/channel/link callbacks and packet path -> usable menu/connection | `infra_frontend_prototype.cpp` currently has an unbound trusted-copy scan adapter and unsupported native callbacks; scan result drafts do not call the real WCL sender; the current hardware-tested WPA2 link is Ethernet-style | **Not integrated** |

The actual native station class in the fixed KC is
`AppleBCMWLANSkywalkInterface` through `AppleBCMWLANInfraProtocol`; the
separate APSTA-named class is not evidence for station mode. Registration must
use Apple's helper-owned private expansion; writing offsets of the private
interface object or passing placeholder queues is not a substitute.

## Failure and shutdown rule before any loadable attempt

`IO80211Controller::start` may retain its provider, publish CoreCapture
services, borrow a global logger, retain the fault wrapper and create timers
before it returns false. A false `CCPipe::startPipe` can also follow a successful
underlying start plus failed deferred capture. There is no general reverse
release sequence proved for those states. The existing startup ledger records
`baseEntered` before the call and quarantines failed results while retaining
its dependencies; that prevents *our own* premature release but does not
prevent IOKit from calling `stop`/`detach` after a failed start.

Controller `stop` at `0xffffff800221e62e` dereferences private state without
checking whether its initialization completed. `free` at
`0xffffff800221e424` does not duplicate the provider, wrapper, global logger,
CC service and legend-timer cleanup in `stop`. PostOffice `+0xb08`,
TimerFactory `+0xb10`, and RNGAgent `+0xb18` have additional unresolved
ownership edges. Direct `stopPipe` merely detaches and does not perform
`stop`/`terminate`; concrete pipe stop can dereference an owner that the
failed-start path cleared. A self-retain does not prevent external
stop/detach or preserve a removed PCI provider.

Therefore the next code checkpoint is **not** to link either prototype. It is
to establish an exact-profile, driver-owned controller/Infra start and stop
transaction with failure injection for each factory/base/registration stage,
one owner for every retain/queue work source, a proved quiescence sequence for
radio interrupts/DMA and native callbacks, and evidence of native kext
dependency/linkability. Stage tests can use synthetic objects, but successful
synthetic teardown cannot be reported as a safe real-KC teardown. Only then can
a guarded live registration test be justified; native menu appearance requires
separate BSD/WCL event and data-path observations.

The detailed instruction evidence lives in the task workspace under
`outputs/R16-Native-WiFi/`: `native-support-lifecycle-findings.md`,
`lifecycle-findings.md`, `native-registration-findings.md`, and their pinned
inspectors/JSON. These are local evidence files, not compiled into the kext.
