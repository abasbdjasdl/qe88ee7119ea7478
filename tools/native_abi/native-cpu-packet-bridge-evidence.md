# Offline CPU packet bridge contract

This is an executable host ownership model and an x86_64 Mach-O compile-only
probe. It is **not a Skywalk driver or an implementation of the native packet
API adapter**. No production source/build is changed. The Port methods in the
probe intentionally remain undefined; do not link or load this object.

Evidence is pinned to Darwin 24.4 recovery KC SHA-256
`d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
Addresses below describe observed code in this KC, never callable addresses.

| Boundary | Observed contract |
| --- | --- |
| TX array action, `legacyDequeue` at `0xffffff80029939bc`, call at `2993a3a` | `unsigned(owner,queue,packet* const*,offered,context)` returns the consumed **prefix count**, from zero through offered. EAX advances table indices and outstanding accounting. Zero stalls. An IOReturn error value is not a valid count. |
| TX capacity callback, `2993d1d` / `2993fb1` | Returns available packet count; third argument is an output byte budget initially `UINT32_MAX`. List mode changes the array interpretation and is excluded from this bridge. |
| Pool allocate, `0xffffff800299307e` | Full 32-bit IOReturn: success is zero; failures include `0xe00002be` and `0xe00002c2`. The old reference header's `bool` declaration is incorrect. Allocation acquires a handle but does not prepare its buffers. |
| Packet prepare / acquire / dispose, `297218e` / `29723f8` / `297240c` | Preparation records a borrowed source queue and prepares buflets. Acquisition/disposal manipulates a reusable packet wrapper and handle, not an OSObject ownership retain. Source queue/pool lifetimes must exceed held packet lifetimes. |
| TX completion queue, `2994d14` / `297dcd4` | Successful enqueue notifies the source, completes direction 1, optionally calls `kern_packet_tx_completion`, then deallocates all packets through their pool. Gate rejection consumes none. No second completion/free is permitted. |
| Direct pool deallocate, `299328a` / helper `29714ea` | For a still-prepared TX packet, completes preparation and notifies its source. This is the alternate reclaim route; do not first manually complete/notify, which can suppress or duplicate source accounting. |
| RX array enqueue, `29948ce` | Requires nonnull array, `0 < count < capacity`, workloop, enabled/initialized queue and no busy condition. Gate failures return nonzero and consume none. |
| RX networking handoff, `297a5b0` | Completes direction 2, disposes wrappers and passes handles to `kern_netif_queue_rx_enqueue`. Outer enqueue returns zero after handoff even if networking later drops the data. Zero means transferred ownership, not confirmed delivery. |
| TX/RX disable, `2977faa` / `297a2b4` | Stops the event source; does not drain packets already handed to the driver. TX purge at `2994171` only cancels and frees unconsumed internal table/list packets. |

`native_cpu_packet_bridge.hpp` uses only opaque packet tokens and an injected
Port. Its `PacketView` is the bridge's own checked API result; it is not a
declaration of Apple object fields. There are no private offsets or fabricated
packet/provider instances. The compile probe verifies the TX callback type and
instantiates the Port contract without including the obsolete pool header.

TX copies a bounded accepted prefix into independent storage. It retains native
packet ownership until the corresponding backend ticket completes, and returns
native packets outside the submission callback. Each ticket includes a slot
generation, so late or repeated completions cannot retire reused storage.
Queued copies are selected by generation to preserve FIFO across slot reuse. Bad
frames are consumed into recycle slots rather than permanently stalling the
framework queue. No native completion occurs before the framework receives the
consumed count. Queue-completion failure preserves ownership for retry or the
audited direct pool reclaim route. Hardware success/failure accounting remains
separate from native resource completion.

RX follows `empty -> allocated -> prepared -> transferring -> empty`. Every
failure before successful enqueue reclaims the owned packet once. A nonzero
allocation return with a nonnull output, or zero return without a packet, enters
quarantine instead of guessing ownership. The model deliberately has no
destructor rollback. After a successful enqueue it never accesses the packet
again. `rxDelivered` currently means handed to Skywalk, not end-to-end delivery.

All calls must be serialized and Port methods must not reenter the bridge. TX
action runs under the Skywalk workloop gate: copy only, then schedule backend
work later. Calling another workloop synchronously can create a lock cycle.
Stopping rejects new TX/RX, cancels queued copies and waits for outstanding
backend tickets, native returns and RX work. Only after external queues are
disabled and hardware is drained can `finishStop` authorize owner teardown.
Quarantine keeps the ledger, native packets, queues and pools alive.

For the initial adapter, accept one prepared writable RX buflet / readable TX
buflet and require checked `offset <= bufferSize` and `length <= size-offset`.
The first segment's virtual address does not include packet data offset.
Unsupported checksum/TSO/offload metadata must fail closed; this model cannot
validate a future adapter's metadata interpretation or fragmentation support.
Queue construction/registration, reference lifetimes, workloop ownership,
checksum/TSO policy, real Port declarations/slots/export eligibility, target
linkage and hardware execution remain separate required work.

Run with Python 3 and Clang, or on Windows with Zig:

```sh
python3 tools/native_abi/native_cpu_packet_bridge_test.py --zig /path/to/zig
```

The runner performs optimized host tests with undefined/bounds sanitizers,
cross-compiles an offline x86_64 Mach-O object, rejects an injected bool-return
allocation declaration, and rejects compilation without the audit-only guard.
It writes source/object hashes and results under `build/native-cpu-packet-bridge`.
Tests cover partial acceptance, independent copying, malformed/fragmented
frames, duplicate tokens, backpressure, completion retry, stale tickets, stop
with in-flight data, exactly-once reclaim, each RX failure stage, successful
transfer and ambiguous ownership quarantine. Host success does not establish
the safety or functionality of a loaded native interface.
