# Bounded WCL scan-result sequencing draft

`src/network/NativeWclScanResults.hpp` is a pure, gate-owned bridge from one
completed foreground scan and its published observation cache to candidate WCL
result and completion messages. It does **not** instantiate IO80211 classes,
call `postMessage`, register an interface, or display the macOS Wi-Fi menu.
Every returned `Frame` has `emissionReady == false`. The installed driver is
still the Ethernet-style `en1` implementation.

The target is one exact Darwin 24.4.0 KC, SHA-256
`d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
A caller-supplied profile enum or `profileVerified=true` does not establish the
runtime kernel hash. A future native owner must verify it independently before
admission. The request must already have been decoded from a safely copied,
exactly 5456-byte WCL input using `NativeWclScan.hpp`; this bridge does not
accept an arbitrary kernel pointer or supply missing private-ABI dispatch.

## Request and snapshot binding

`Bridge::begin` requires a completed, drained, successful foreground request,
its exact scan token and cache generation/epoch, and the same two-gate monotonic
completion timestamp. The timestamp equality matches the current
`MacNetworkController::advanceForeground` sequence: it passes one `timestamp`
to `scanObservations->finish(...)` and then
`foregroundScan.complete(snapshot.token,timestamp)`. The channel count and
ordered channels must match the decoded request exactly, along with active or
passive mode and its selected dwell time. Nonzero home/away timing remains
unsupported by the disconnected backend. A one-channel dwell is not a full
scan. A stale, cancelled, timed-out, or replaced snapshot cannot be sequenced.

Only the decoded infrastructure and any-BSS request types are accepted. For an
infrastructure request, capability bit 0 (ESS) must be set and bit 1 (IBSS)
clear. An any-BSS request retains both types. This filter does not synthesize
new networks or security capabilities. The bridge uses only the completed
cache's sorted entries, never the mutable in-progress bank.

Before preparing the first message, it encodes **every selected** entry into
caller-owned, full-size 2112-byte scratch storage. A malformed, oversized, or
unsupported entry rejects the whole attempt before any candidate result is
reserved; it is never silently truncated or skipped. The only intentional
exclusion is the decoded infrastructure-vs-IBSS filter. No whole cache Entry or
payload is placed on the kernel stack. The scratch and per-result buffers must
be accurately sized, disjoint from the Store, bridge and input objects, and
stable under the controller gate. The helper checks these overlaps before it
writes. `reserve` also rejects a `Frame &` that aliases the Store, foreground
status, bridge, or payload buffer **before** clearing the Frame; an alias error
leaves the overlapping object unchanged. Other reserve errors clear a disjoint
Frame to prevent reuse of stale event metadata. The caller's scratch is wiped
after validation and on ordinary errors.

`reserve` yields event **201** (`APPLE80211_M_WCL_SCAN_RESULT`) for each
selected cache entry, using the bounded 64+IE byte draft in
`NativeWclBeacon.hpp`. There is at most one outstanding reservation. A future
sender must keep the output stable until its synchronous copy is established,
then call `commit` with the exact request token, snapshot, delivery sequence,
event number, ordinal and payload length. Any rejected send aborts this scan;
the bridge never emits a success completion after partial failure. A duplicate
or stale commit cannot advance it. After all result reservations are committed,
it yields one event **237** (`APPLE80211_M_WCL_SCAN_DONE`) with the pinned KC's
observed successful four-byte zero status. This includes an empty but genuinely
completed scan. Its commit moves the bridge to `completed`; further reserves
or commits cannot repeat it. `retire` permits a later foreground request, but
the previous token remains remembered so retiring cannot replay that request.

The exact event numbers, result bounds, four-byte done length, and observed
zero successful status come from the pinned KC analysis in
`docs/native-wcl-notifications.md` and workspace
`outputs/R16-Native-WiFi/wcl-notification-findings.md`. Other completion/error
status values have not been mapped and are not fabricated here.

## Live boundary still missing

`commit(accepted=true)` is only a state-machine input for a future validated
sender. It does not prove that WCL or userland received anything.
`IO80211Controller::postMessage` can return zero without an open IOUC pipe, and
its inspected PostOffice queue stores a non-retained interface pointer. A live
implementation must establish the exact KC profile, native IO80211 controller
and interface registration, matching request callback and event subscription,
PostOffice and Glue ownership, synchronous-copy/queue acceptance semantics,
failure reporting, shutdown cancellation and queue drain before releasing the
interface. It also needs the real Skywalk packet path for a useful native
interface. None of those operations occurs in this helper.

## Verification

`tests/network_native_wcl_scan_results_test.cpp` runs 192 assertions over
decoded synthetic WCL requests, a real `foregroundscan::Controller` and
`nativescan::Store`: sequential results, ESS/IBSS filtering, an empty scan,
four-byte one-time completion, duplicate and forged commits, failure abort,
same-generation request staleness, cancellation, replaced cache, buffer bounds,
alias rejection (including all four `Frame` overlap classes) and oversized-IE
preflight. Local Zig 0.15.2 C++17 builds
passed with `-O2 -DNDEBUG -Wall -Wextra -Werror -Wconversion
-Wsign-conversion -fsanitize=undefined,bounds -fno-sanitize-recover=all`.
This is UBSan/bounds coverage, **not** proof of local ASan instrumentation.

`tests/network_native_wcl_scan_results_kernel_compile.cpp` also compiles to
an x86_64-macOS freestanding object at `-O2` with no C++ exceptions/RTTI,
`-Wframe-larger-than=512 -Werror`. It is compile-only: neither test validates
the running KC, loads a kext, emits an event, or demonstrates the native Wi-Fi
menu on the real machine.
