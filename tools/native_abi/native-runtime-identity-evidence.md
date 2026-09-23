# Loaded component identity: target load failure fixed

The current `native_runtime_identity.cpp` uses the exported `sysctlbyname`
KPI for `kern.uuid`, then reads each explicitly linked dependency's LC_UUID
through this bundle's loader-owned `kmod_info.reference_list`. It rejects a
wrong Darwin version, unavailable/malformed UUID, missing/duplicate dependency,
cyclic/oversized list, malformed Mach-O command range, or UUID mismatch before
allowing any native controller operation. The bundle must define its own
`kmod_info` and declare all three native families as direct dependencies.

The reader does not walk the global mutable kmod list or use private OSKext
methods. The [pinned XNU implementation](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.101.15/libkern/c%2B%2B/OSKext.cpp)
constructs and retains the dependency list during loading and uses a built-in
component's `kmod_info->address` for `copyTextUUID`. The
[Libkern export list](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.101.15/config/Libkern.exports)
provides `sysctlbyname` through the kernel implementation. Header reads are
bounded but assume the loader-supplied pointers are valid; this is not a
validator for arbitrary addresses. Run outside driver command gates.

## Actual 15.4.1 Recovery execution, 2026-09-23

The first VM bundle, commit `4097cc8`, was rejected at load because
`OSKext::lookupKextWithIdentifier`, `OSKext::isLoaded` and
`OSKext::copyTextUUID` are not exported to it. The old nlist/slot audit had
not established actual export permission. The old implementation is replaced.

The replacement in commit `ec658ea` passed host failure tests and cloud build
run 35834755322. Manual `kextload` then reached Recovery's unavailable
SPKernelExtensionPolicy service, so it was injected through a separate VM
OpenCore image. Its binary SHA-256 is
`2a507cb0d79920394303c7c7cef22bf1d9f1d938391a22198de1597bca0fabe7`.

The target kernel logged matches for IO80211Family, IOSkywalkFamily and
corecapture, then identity status 6/component 4 (all four components matched,
including the kernel UUID). The harness allocated its real IO80211-derived
controller; `init` returned true; Apple's `IO80211Controller::free` logged both
entry and completion; release returned. Recovery subsequently displayed the
harness's true init/release properties and false native-Wi-Fi property.

The early init emitted a `waitForSystemMapper` diagnostic backtrace and
resumed after ACPI enumeration. A subsequent kext-unload request was rejected
because removal is unsupported for this boot-cache-injected kext. That is not
a successful unload or proof of complete native start/stop cleanup.

The tested collector body is now shared in `native_runtime_identity.cpp`.
`network_native_runtime_identity_test.py` runs the actual body against fake
loader-owned dependencies; it covers version rejection, sysctl errors,
malformed text/binary UUIDs, all component UUID-byte mismatches, malformed
load commands, missing/duplicate/cyclic dependencies and truncation bounds.
The O0/O2 audit checks expected KPI imports and the pinned KC UUIDs. `_kmod_info`
is bundle-defined; `_sysctlbyname` uses the export alias recorded in the
contract. No OSKext object virtual calls remain.

This proves target build identification and successful init/free in the
isolated guest. It does not prove failure-injected init, full controller
start/stop, Infra registration, queue ownership, physical PCI operation,
menu scanning or networking. UUIDs are build identities, not byte-integrity
checks; deployment must still verify the pinned Boot KC hash.
