# Offline IO80211 controller shape, Darwin 24.4

The prototype in `native_controller_prototype.cpp` is the smallest concrete
controller callback shape derived from the **pinned** x86_64 macOS 15.4.1
Recovery `IO80211Controller` table. Its constructor is deleted; it has no
metaclass definition, PCI personality, start/stop, backend binding, radio
operation or registration. It is excluded from `tools/build_network_stack.py`.
The current working RTL8852BE driver remains `IOEthernetController` based.

The vtable has 466 raw entries. This prototype overrides all 12 pure
controller callbacks and the nonpure `getWorkQueue() const` slot 399. Its
logger, work queue and fault-wrapper getter bodies call the three borrowed
getter functions defined by the separate `startup_prototype.cpp` ledger. No
ledger is created or bound here; this establishes only the compiler's callback
ABI and cross-object symbol names. Returning unsupported from the other pure
callbacks is an audit stub, not useful Wi-Fi behavior.

`audit_native_controller.py` compiles the class at O0/O2 using the generated
exact-profile overlay, compares every emitted vtable entry to
`darwin24_4.json`, and rejects any missing/extra undefined import. It permits
only 448 inherited table imports, the three project-local borrowed getters,
the IO80211Controller complete destructor, and OSObject delete. An extra
virtual-slot mutation and a missing-logger-import mutation must fail their
respective checks. With `--kc`, the two additional Apple imports must occur
once as defined external symbols at the pinned component/address in the
matching `BootKernelExtensions.kc`.

The direct reference is `AirportItlwm/AirportItlwmV2.cpp` and
`AirportItlwm/AirportItlwmSkywalkInterface.hpp` at pinned itlwm v2.3.0: it uses
an IO80211Controller with a role-1 IO80211InfraProtocol station interface.
AirPort_RTW88's `src/kext/AirportRTW88.cpp` instead uses the older
IO80211Interface/`apple80211Request` route and an rtw88 hardware backend;
neither its ABI nor its RTL8822 hardware path can simply replace this RTL8852BE
rtw89 service on the inspected Darwin 24.4 kernel.

The next code boundary is a single durable owner of support objects,
IO80211Controller base-start state, the real PCI/backend session and the
InfraProtocol instance, with failure injection and quiescent stop for every
partially completed stage. `native-service-lifecycle-map.md` records the
unresolved external stop/partial-base-start hazards. Only after a loadable
symbol/dependency check and safe lifecycle can the interface be registered
using mutable 0x130-byte RegistrationInfo and real queues/pools. Registration
then needs BSD/WCL discovery, status/MAC/channel/power callbacks, actual scan
events/results, association/security requests and packet ownership before the
native Wi-Fi menu is functional. This offline object alone proves none of
those effects and has not been tested on hardware.
