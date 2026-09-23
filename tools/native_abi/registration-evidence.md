# Offline registration callsite evidence

Target: x86_64 Darwin 24.4.0 recovery KC, SHA-256
`d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
This is one exact target. No helper was invoked or kernel extension loaded.

`registration_probe.cpp` includes generated `Airport/Apple80211.h` and emits
eight separate external C wrappers. Each wrapper calls one ordinary C++ helper
and produces its compiler-selected undefined symbol. There are no hard-coded
assembler aliases or function-address casts. `registration-symbols.json` gives
the exact expected undefined Mach-O names, target addresses and return evidence.
A set-equality check detects missing, altered and unexpected undefined references.
The `_r16_registration_*` wrapper names are defined symbols, not expected imports.

Local x86_64 kernel-target compilation passed at both `-O0` and `-O2`; each
object has exactly the eight expected undefined helpers, and all JSON symbol
addresses/instruction anchors match the pinned KC evidence. The callsite audit
uses `-fno-sanitize=all` because Zig otherwise inserts an additional UBSan helper
for member-call receiver checks. This flag applies only to the offline symbol
probe; it is neither a sanitizer test nor a driver-build policy change. A builder
should reject unexpected imports rather than silently filter them out.

The probe is compiled only, never linked into the functional driver or called.
Its wrappers would create or mutate kernel state if invoked; they are not safe
runtime probes. Object compilation cannot establish exported symbol-set
availability, successful kext linking/loading, registration, native menu display,
pool/queue virtual ABI, packet ownership or networking.

## Declarations intentionally kept narrow

`Apple80211.h` normally includes an old complete pool class. This translation
unit suppresses that header with its documented include guard, then declares
only the pool's static factory and its nested external options type. TX and RX
queue declarations likewise contain static factories only. The classes have no
asserted inheritance, instance size or virtual methods. Do not allocate them
with `new`, use `sizeof` on those classes, dereference an instance field or use
these declarations in a loaded driver. The actual factories construct Apple's
objects and return opaque pointers; this audit does not run them.

The 32-byte `PoolOptions` external record is six unsigned words and one pointer.
Fields 0x00/04/08/0c/10 are packet count, buffer count, buffer size, maximum
buffers per packet and segment size; they are passed to `kern_pbufpool_create`
at `0xffffff8002970ac9..2970af8`. Flags at 0x14 are transformed at
`29709e5..2970a75`, not blindly forwarded. The pointer at **0x18 is optional
DMA specification**, despite the old private header's reserved/pad name:
`2970cc2..2970cc6` copies it to the memory-segment descriptor; `297ae77..297ae9b`
uses it to construct an `IODMACommand` when non-null. The null branch explicitly
skips that operation. The pointed-to description remains opaque here.

The exact TX callback argument is `IOSkywalkPacket * const *`, encoded as `PKP`.
`const IOSkywalkPacket **` would encode as `PPK` and is a different C++ signature.
RX uses `IOSkywalkPacket **`. Callback return `unsigned` is encoded by `PFj`.
The seven-argument simple TX factory at `0xffffff8002977d5a` forwards to the full
factory with a null query callback and UINT_MAX service class; the full target
overload has an extra unsigned argument compared with the old header. The probe
deliberately selects the known simple overload.

RegistrationInfo is borrowed through its generated nested type. No info object
is allocated, byte fields written, alignment implied or capability flags guessed.
The initializer accepts version 1 and exactly 0x130 bytes and returns Boolean
(`0xffffff800297bb6e`). Ethernet registration accepts **const** info, whereas
Infra registration accepts **mutable** info and can overwrite its MAC bytes
(`0xffffff80022d68a0..22d68d3`). Both return 32-bit IOReturn with zero success;
the concrete producer checks full EAX at `0xffffff80015663c8`. Deregistration
likewise returns the base status. The probe's compile-time function-pointer
assertions catch declaration drift in these returns and qualifications, but C++
mangling itself does not encode ordinary function return types.

## Lifetime boundaries from the same target

- WorkQueue/pool/TX/RX factories return owned object pointers or null after failed
  initialization and release. Their virtual layouts are not validated by this
  callsite-only check. The WorkQueue factory has a first-default global pointer
  side effect whose final-release lifetime is unresolved.
- Registration helpers allocate their own private expansion storage. Array
  registration builds a QueueSet/LogicalLink and retains real pools/queues.
  QueueSet validates count 1..256 and non-null members. The probe supplies no
  fabricated objects, empty queues or success callbacks.
- Deregistration releases retained registration objects. It is not a DMA drain
  operation or proof that interrupt, timer and packet callbacks have quiesced.
- Registering an interface successfully does not alone complete deferred BSD
  attach or WCL discovery. These offline checks cannot be called native Wi-Fi
  integration or a functional menu implementation.

Full local reproducible instruction evidence is in
`outputs/R16-Native-WiFi/native-registration-inspect.py`,
`native-registration-disassembly.json/.txt`, `native-registration-linkage.json`
and `native-registration-findings.md/.json` in the task workspace. The generator
pins the KC hash and verifies twelve groups of evidence instruction addresses.
The repository JSON contains symbol names and derived ABI facts, not Apple code.
