# Exact-KC InfraProtocol frontend prototype

Scope: compile-only C++ and portable admission-state tests. No instance was
allocated, initialized, attached, registered, loaded, called by macOS or used to
scan a radio. This is not a working native Wi-Fi menu. The audit-only guard must
remain; passing the audit is not permission to remove it.

The target KC SHA-256 is
`d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
`darwin24_4.json` pins this KC, the upstream and SDK commits, exact class sizes,
the inherited table and each pure callback's mangled parameter signature.
No old Airport header is assumed compatible: compilation consumes the generated
target overlay from `build_native_sequoia.py`.

## What the compiler proves

`infra_frontend_prototype.cpp` derives `R16InfraFrontend` directly from the
generated `IO80211InfraProtocol`. Its base is exactly 288 bytes; the bridge is
ordinary derived-class storage after that base. No Apple private field or
expansion pointer is accessed. The out-of-line destructor emits the full table
without a factory, `new`, fake OSObject, or runtime invocation.

The subclass implements all **191 remaining pure callbacks**: 190 protocol
callbacks plus inherited `setMacAddress(ether_addr&)`. Some earlier ancestors
declare additional pure methods, but the target Infra/Skywalk classes already
provide concrete `enable`, `disable` and BSD initialization implementations.
The prototype inherits those, which does **not** prove their lifecycle is safe
with this subclass. `!__is_abstract` alone is never treated as a runtime test.

`audit_infra_frontend.py` checks all **658 raw table entries**, including the two
Itanium prefix entries. Native inherited targets match exactly; each pure or
destructor replacement must be owned by the subclass and preserve the target
method identity. `setWCL_SCAN_REQ(apple80211ScanRequest*)` occupies raw slot
**594**. O0 and O2 objects passed. An extra virtual method compiles, grows the
table to 659, and is correctly rejected by the same audit.

Three wrappers emit ordinary compiler-mangled calls, using borrowed arguments:

| Helper | Exact-KC return and input contract |
| --- | --- |
| `IOSkywalkEthernetInterface::initRegistrationInfo` | `bool (RegistrationInfo*, unsigned, unsigned long)`; the caller still needs proved size/version and owned writable storage |
| `IO80211InfraInterface::registerInfraEthernetInterface` | full 32-bit `IOReturn (RegistrationInfo*, IOSkywalkPacketQueue**, unsigned, IOSkywalkPacketBufferPool*, IOSkywalkPacketBufferPool*)`; registration info is mutable |
| `IOSkywalkEthernetInterface::deregisterEthernetInterface` | full 32-bit `IOReturn (unsigned)`; teardown is not automatic or proven merely by calling it |

Helper symbols and addresses come from `registration-symbols.json`; detailed
input/lifecycle limitations remain in `registration-evidence.md`. Wrappers do not
construct a queue list, fill capability constants or create registration data.
They do not call each other or pretend to be a complete registration sequence.

Every undefined symbol is checked against the inherited target table, these
three helpers, and the fixed extra-symbol set in `infra-frontend-contract.json`.
The latter includes the base destructor, OSObject delete and compiler-generated
stack protection references where emitted. Their same-KC nlist addresses were
re-read locally and are included in the contract. O0/O2 import sets are recorded
separately and checked exactly; a compiler emitting a different set fails for
review. A symbol's presence in the KC is **not** proof of kext symbol-set export
eligibility or successful linking. Ordinary return ABI is covered by the pinned
overlay's existing return evidence, not inferred from mangled names alone.

## Scan callback and ownership boundary

`setWCL_SCAN_REQ` receives a pointer with **no length**. A 5456-byte expected
shape does not establish that the argument is readable, live, from the expected
operation, or from this KC. The callback never dereferences that pointer. With
no bound backend it returns `kIOReturnNotReady`; no trusted live adapter is
implemented or bound in this prototype.

The missing `Backend::copyValidated` must prove the dispatch operation, exact
readable length, target KC and source lifetime, and copy into the bridge's own
5456-byte storage. It must also supply a trusted policy snapshot and independent
dispatch identity. Filling these facts by assertion or memcpy of the raw
callback argument would violate the interface contract. No raw user/kernel
pointer may be passed straight to the decoder or production backend.

The owned bridge must be heap/IOKit-object storage, never a kernel-stack local.
It decodes `NativeWclScan`, maps `NativeWclScanPlan`, and submits a small
`CopiedScan` by reference only for the duration of the adapter call. The adapter
must copy the values before returning; the bridge retains no request pointer.
The raw scratch buffer is cleared before and after every attempted copy.
Directed SSID/BSSID filters and unknown fields are rejected; there is no
credential input, saved-network access, or logging.

Known active/passive selection, exact channel order, allowed-channel masks and
dwell values are preserved. Nonzero home/home-away timing is rejected because
the disconnected backend cannot honor that scheduling. Ordinary native requests
with home 45 ms or home-away 100 ms therefore remain unsupported; this cannot be
presented as support for every menu-generated scan.

All bridge operations and the adapter require one serialization domain. Recursive
scan, synchronous terminal delivery during admission and unbind while entering
or in flight are rejected. A valid dispatch generation is consumed even when
decoding or backend admission fails. Bind epochs must increase strictly across
unbind; history is preserved, and a late old-epoch completion cannot clear a
new scan. The wire header at +0 is opaque and never used as a generation.

`terminal(identity)` is only bookkeeping **after a trusted adapter has observed
actual matching terminal status and hardware drain**. It does not publish a
native scan event, call a completion handler, or assert success. No destructor
rollback is attempted; outstanding storage/context must be drained or retained
in quarantine. Stop/provider removal/unload are not yet solved.

## Intended production backend adapter (not implemented)

After a trusted copy and successful decode/map, `submitCopied` should call
`R16NetworkController::beginNativePlannedForegroundScan(value.channels, status)`.
The bool-only foreground API uses a fixed dwell and is not suitable. The planned
API already copies the plan and rechecks channel flags and readiness under the
hardware gate. It accepts only idle, unconnected, enabled operation without
pending join or legacy credentials; a Busy/Unsupported result must propagate.

The adapter must maintain an owned mapping from frontend
`Identity {epoch,generation}` to the **returned**, nonzero backend
`foregroundscan::Token {epoch,request}`. These are different identity spaces;
they must not be fabricated from each other or from the WCL header. It must poll
`copyNativeForegroundScanStatus(token, status)`, validate matching identity and
`drained`, and use the complete snapshot token to copy results. Cancellation
requires `cancelNativeForegroundScan` followed by actual drain; failed undrained
requests require hardware recovery/quarantine, not `terminal()`.

Calling production APIs also requires their `controlLock -> hardware gate`
ordering. The native framework's callback gate, future frontend queue and backend
adapter must be designed to avoid reverse lock acquisition. The portable bridge
does not prove that lock topology or cross-queue request lifetime.

Alternatively, a future trusted dispatch adapter can pass its owned, verified
raw copy directly to `beginNativeWclScanRequest(copy, 5456, true, status)`, which
derives policy and decodes inside the backend gate. That is an **alternative**
admission route, not a second call after the planned API. The current bridge
wipes the raw message before returning and its submission contract contains no
raw bytes, so it is intentionally designed for the planned route. Neither route
makes the original no-length pointer trusted. The current prototype links to
neither production entry point.

## Remaining callbacks and lifecycle needed for real native operation

Static evidence cannot prove a sufficient minimal callback list for menu
visibility; that needs an attached target interface and actual WCL/framework
traces. The following are concrete missing prerequisites, rather than promises
that enabling them alone will make the menu work:

| Area | Current prototype | Required real data/operation |
| --- | --- | --- |
| Native controller/interface construction | No allocation/start path; existing native base methods merely inherited | Proved controller callbacks and support objects, init/attach/start order, provider/service lifetime, controller-interface binding |
| Station MAC | `setMacAddress` is void, so it poisons scan admission instead of claiming success | Validated hardware/current address, actual programming and framework/BSD address synchronization |
| Radio and channel description | `getCHANNEL` (467), `getOP_MODE` (471), `getSUPPORTED_CHANNELS` (473), `getHW_SUPPORTED_CHANNELS` (492), `getCOUNTRY_CHANNELS` (485), `getWCL_CHANNELS_INFO` (526) all Unsupported | Verified exact output layouts and actual hardware/regulatory channel, band, width and mode snapshots |
| Power/capabilities/metrics | `getPOWERSAVE`, `getTXPOWER`, `getRSSI`, `getRATE`, HT/VHT getters and corresponding setters Unsupported | Implement only supported hardware features with validated data units and policy; never fill plausible constants |
| Skywalk/BSD registration | Only three independent compile callsites | Real pool and queue owners, correct RegistrationInfo fields/flags and callback context, successful register result, BSD attachment and safe unwind |
| Scan request | Decoder/plan/copied-backend contract only; unbound returns NotReady | Trusted dispatch copy and identity provenance, actual backend adapter, applicable native timing support |
| Scan abort/results/events | `setWCL_SCAN_ABORT` (589), `getWCL_BSS_INFO` (522), cached results (532) Unsupported; no event calls | Identity-bound cancellation/drain, real cache generations, verified beacon/result encoding and correct result/completion notifications |
| Association/key/leave | `setWCL_ASSOCIATE` (595), `setCIPHER_KEY` (538), `setWCL_LEAVE_NETWORK` (583), `setWCL_JOIN_ABORT` (592) Unsupported | Verified credential/key ownership, native association state and security policy, real join and link event ordering |
| Link and enumeration | `setWCL_LINK_UP_DONE` (597), `setWCL_LINK_STATE_UPDATE` (600), `setINFRA_ENUMERATED` (632) Unsupported | Actual backend link state plus proved WCL event/enumeration semantics; backend publication alone is not native event delivery |
| Data and shutdown | No traffic adapter or lifecycle implementation in this class | Real Skywalk TX/RX queue data bridge, frame ownership, cancel/drain, reverse teardown only where proven safe |

All other protocol pure callbacks likewise return Unsupported without reading
or modifying their argument. `postMessage`, `postMessageIOUC`, `setLinkState` and
`setLinkStateInternal` exist in the inherited target table, but their presence
does not establish message IDs/payloads, readiness ordering, or safe call sites.
No event is emitted in this prototype.

## Reproduction and results

From the repository root, after generating the target overlay:

```text
python tools/native_abi/audit_infra_frontend.py ../itlwm-reference ../MacKernelSDK build/native-sequoia-contract --zig ../toolchain/zig-x86_64-windows-0.15.2/zig.exe
../toolchain/zig-x86_64-windows-0.15.2/zig.exe c++ -std=c++14 -O2 -fsanitize=undefined,bounds tools/native_abi/native_infra_scan_bridge_test.cpp -o build/native-infra-prototype/bridge-test.exe
build/native-infra-prototype/bridge-test.exe
```

The audit also supports `xcrun clang++` by omitting `--zig`; that compiler path
has not been run locally, and exact compiler-generated imports remain checked.
For a POSIX host sanitizer run, compile the same portable test with clang++ and
`-fsanitize=address,undefined`; local Windows testing used UBSan/bounds only.

Local results: **120,342 checks** passed with O2 UBSan/bounds, including unreadable
opaque argument forwarding, exact copied length, stale/reused generation and
epoch rejection, backend failure, unsupported timing/policy, reentrancy guards,
scratch clearing and backend value-copy independence. The test uses generated
fixtures, never real network data. The large check count includes per-byte
scratch-clear assertions, not that many independent scenarios.

O0/O2 exact 658-entry table and undefined-symbol audits passed, as did the
compiling extra-slot rejection. `build/native-infra-prototype/infra-audit.json`
records object, source, decoder, plan, manifest, helper evidence and generated
overlay hashes. No Mach-O object is executed by the audit. There has been no
target-machine frontend test, deployment or reboot.
