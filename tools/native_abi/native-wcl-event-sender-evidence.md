# WCL scan event delivery, exact Darwin 24.4 KC

This is a **compile-only** callsite and a bounded static audit of
`BootKernelExtensions.kc`, SHA-256
`d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
No native interface, PostOffice or Glue was instantiated and no event was
emitted. The installed Ethernet-style WPA2 driver is unaffected. A successful
object compile cannot make the macOS Wi-Fi menu appear.

## Exact event path

The Apple producer calls
`IO80211Controller::postMessage(IO80211SkywalkInterface*, unsigned, void*,
unsigned long, bool)` with the final argument **true**. Its implementation at
`0xffffff8002225d6c` loads the controller's PostOffice and tail-dispatches
through PostOffice vptr `+0x120`. The target method is
`IO80211PostOffice::sendMail` at `0xffffff80022ac922`. Its full 32-bit status
is zero after successful queue insertion and `0xe00002bd` if allocation
fails. The 201 and 237 producer calls at `0xffffff80016d6d5d` and
`0xffffff80016d725c` use the same route. The direct ordinary-method call in
`native_wcl_event_sender_probe.cpp` establishes only the compiler-mangled
symbol and calling convention; it does **not** reproduce the controller's
virtual dispatch or authorize bypassing it in a driver.

`sendMail` allocates `length+0x38` bytes, copies the payload into its own
entry before returning (`...22ac955`, `...22ac9c4`), and enqueues it. Thus a
caller-owned event buffer need only remain valid through that synchronous
copy. Its queued entry stores the interface pointer at `+0x10` without a
retain (`...22ac976`). `outForDelivery` later calls the interface virtual
`postMessage` at object-vptr `+0xb10` (`...22acac9..acc`) and frees the entry
(`...22acad2..add`). No corresponding interface retain/release was observed
on this path. A future owner must serialize scan emission, stop/detach and
queue drain so that the interface cannot disappear while mail is pending.

The pinned `IO80211InfraInterface::postMessage` target at
`0xffffff80022cf72a` can eventually call
`IO80211SkywalkInterface::postMessageInternal`, which routes to
`IO80211Glue::routeEventToWcl` only when the final bool is true and Glue is
present (`0xffffff800225ec8e..ecb4`). Glue then makes another owned payload
copy in `addEventToPendingQueue` (`0xffffff8002125db0..5e57`) and signals its
work source. **Glue allocation failure calls `panic`** at
`0xffffff8002125e9b..5eb0`, rather than reporting a recoverable status. Its
event dispatcher sends category-2 events to WCL. The 201 and 237 consumer
callbacks are `0xffffff800213175e` and `0xffffff80021317fe`.

A zero `sendMail` status proves queue insertion, not Glue consumption, WCL
state advancement or userland/menu receipt. The alternate IOUC route can
return zero with no open subscriber or after certain write failures. A sender
must not mark `NativeWclScanResults::Frame::emissionReady` true from this ABI
audit. Event 201 is a 64-byte `BeaconMetaData` plus 0..2048 raw IE bytes;
its inner count at offset 0 must match the outer length minus 64. Event 237
is exactly four bytes; the observed successful producer uses a zero `uint32`.
The existing `NativeWclBeacon.hpp` encoder and result bridge perform the
deeper IE and scan-token checks. This probe checks only outer bounds, inner
length and zero done status, and requires an independent payload-validation
proof before even its offline callsite becomes reachable.

## Offline audit result

`audit_native_wcl_event_sender.py` compiles the isolated callsite at O0 and
O2 for x86_64 Mach-O, requires the **one exact undefined method symbol**, and
rejects a Boolean instead of full 32-bit return declaration and an unguarded
build. With `--kc`, it hashes the exact KC and checks eight public nlist
symbols/addresses across controller, PostOffice, Infra, Glue and WCL. This
checks the selected KC's identity, not a changed macOS build or a future
kext's symbol-set/link eligibility. The method's concrete copy, queue and
panic behavior is established by the pinned instruction evidence above and
`outputs/R16-Native-WiFi/wcl-notification-disassembly.json` in the local
workspace, not by compiler import matching alone.

Local command:

```sh
python tools/native_abi/audit_native_wcl_event_sender.py \
  --zig ../toolchain/zig-x86_64-windows-0.15.2/zig.exe \
  --kc /path/to/BootKernelExtensions.kc
```

## Blocking proofs for a live sender

1. Construct and register the native IO80211 controller, Infra interface,
   protocol, Glue, WCL managers, device configuration and PostOffice using
   their **pinned runtime** class layouts. The current native prototypes are
   not instantiated. `IO80211Controller::start` has incomplete failure
   rollback, and PostOffice holds a borrowed work queue; see the local
   `native-support-lifecycle-findings.md`.
2. Tie 201 entries and one 237 completion to one accepted, fully drained
   foreground scan. Cancel, supersede or fail without posting a false success.
   The present `NativeWclScanResults.hpp` drafts deliberately set
   `emissionReady=false`; the controller result seam does not bind a sender.
3. Prove the exact interface lifetime through asynchronous PostOffice
   delivery, Glue pending work and stop/detach. A self-retain alone does not
   drain either queue. Establish a bounded failure policy for Glue's panic on
   allocation failure; a successful PostOffice return cannot prevent it.
4. Observe real WCL/userland scan notifications and the native menu on the
   target machine before claiming native scan delivery. Joining, key
   installation, link state and Skywalk data queues remain separate blockers.

None of these proofs can be replaced by setting the Boolean fields in the
offline `Proof` struct. It exists to make missing preconditions explicit and
starts entirely false.
