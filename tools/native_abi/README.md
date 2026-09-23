# Darwin 24.4 native Wi-Fi declaration audit

`darwin24_4.json` records symbol/slot evidence from the local x86_64 macOS 15.4.1
recovery BootKernelExtensions.kc, SHA-256
`d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
It contains names and interface facts, not Apple executable code or credentials.
The target is one specific KC, not all macOS 15 versions.

The manifest was assembled from local `lifecycle-vtables.json`,
`lifecycle-controller-pure-map.json`, `infra-dispatch-map.json`, and concrete
AppleBCMWLANSkywalkInterface/AppleBCMWLANCore tables from that same KC. Pure
callback parameter signatures were independently checked against the concrete
implementations. `return-evidence.md` records separate return-value analysis.
The pinned upstream headers supply declarations whose returns remain explicitly
marked unverified unless there is separate target evidence.

`tools/build_native_sequoia.py` copies the pinned itlwm headers into its output
directory, preserves their notices, and replaces seven private class declarations
with target-ordered methods and opaque storage. It also replaces SDK network
controller reserved slots 6/7 with the target's two named methods. The original
reference checkouts and the driver build are not modified. Generated headers
require `R16_NATIVE_ABI_AUDIT_ONLY`; unknown returns are marked unavailable.
`IO80211FlowQueueHash` remains incomplete: its named mangling is known but its
full source layout has not been established.

The audit compiles seven concrete **offline probe classes** and compares every
emitted Mach-O vtable relocation against the target table, including inherited
slots, parameters, const qualification, and pure method ordering. The probe's
pure methods return unsupported/null/false as appropriate; they are not a Wi-Fi
implementation. Output is an object, never a kext or installation package.
Successful compile-time size assertions match the observed metaclass sizes.
Raw slot counts include the Itanium headers and trailing zero/vcall offset.

`registration_probe.cpp` separately emits calls to eight target registration,
work-queue and packet-pool/queue helpers. Their actual undefined Mach-O symbols
must exactly match `registration-symbols.json`; the builder checks this separately
because nonvirtual methods never appear in vtables. In particular Infra's
RegistrationInfo pointer is mutable, and the TX callback receives
`IOSkywalkPacket * const *`, not `const IOSkywalkPacket **`. The factory-only
probe makes no claim about queue object layout or runtime packet ownership.

`isCommandProhibited(int)` has a separately verified 32-bit status return, with
compile-time assertions on both controller and interface declarations. A matching
mangled name would not detect the old erroneous `bool` declaration.

```sh
python3 tools/build_native_sequoia.py build-itlwm MacKernelSDK
python3 tests/network_native_sequoia_audit_test.py
```

On Windows, add `--zig /absolute/path/to/zig.exe`. On macOS the builder uses
`xcrun clang++`. CI places the output under `build/native-contract/sequoia`.
Old object/success files are invalidated before compilation; failures stop the
builder. Metadata binds the manifest, builder, source, generated headers and
object hashes. Negative checks swap two pure callbacks, alter a class owner,
restore an obsolete reserved slot, remove table entries, change the mutable
registration pointer and swap the TX callback's pointer qualification.

A zero-mismatch report does **not** prove all ordinary return ABI, runtime export
eligibility, kernel linkage, constructor/teardown safety, RegistrationInfo field
semantics/alignment, actual helper execution, WCL messages, or native menu
functionality. Those remain separate integration checks. The known native
fault-reporter null path panics; none of the unsupported/null offline probe
bodies is suitable for loading just because its symbols match.
