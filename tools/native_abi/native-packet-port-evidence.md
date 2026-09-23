# Offline Skywalk packet Port callsites

The probe in `native_packet_port_probe.cpp` implements the Port surface used by
`native_cpu_packet_bridge.hpp` against deliberately incomplete, opaque C++
declarations. It compiles only for x86_64 Mach-O with
`R16_NATIVE_ABI_AUDIT_ONLY=1`. It is **not** a linkable or loadable driver.
`ready` and `metadataProven` default false; no code in this probe promotes them.
The ordinary-call declarations force compiler-generated symbols without
pretending that the old private headers describe the live class hierarchy.

The exact Darwin 24.4 recovery KC SHA-256 is
`d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
`native-packet-port-contract.json` pins 15 method symbols and 10 target virtual
slots. `audit_native_packet_port.py` compiles at `-O0` and `-O2`, compares the
entire undefined method set, rejects a Boolean allocation return and an
unguarded build, and optionally verifies every symbol and slot against the
actual hashed KC. It allows only compiler-generated `_memcpy`/`_memset` outside
the 15 packet API imports. Such an import list is **not** a kext link, export
eligibility or execution test.

The pinned KC has pool `allocatePacket(unsigned,packet**,unsigned)` at
`0xffffff800299307e` / virtual `+0x130`, returning full 32-bit IOReturn:
success clears EAX at `...2993107`, while `...2993116` and `...2993120`
return distinct nonzero errors. `setDataLength(unsigned)` at `...2993604`
rejects a multibuffer packet with `0xe00002c7` and is virtual `+0x138`.
RX enqueue array overload at `...29948ce` and TX completion enqueue array
overload at `...2994d14` are both virtual `+0x2a0` on their respective queues.
The latter returns nonzero `0xe00002c2` / `0xe00002d7` on gate errors, clears
R12D on completion at `...2994da4`, and copies full R12D to EAX at
`...2994daf`. Zero consumes the whole packet array; nonzero does not transfer
ownership. TX submission's earlier callback has a different **unsigned count**
return contract, covered by `native-cpu-packet-bridge-evidence.md`.

For a packet view, `getPacketBufferCount()` is virtual `+0x128` and the probe
accepts exactly one. `getBufferSize()` returns the pool's 32-bit size.
`getDataOffset()` at `...2993792` zero-extends a 16-bit value into EAX at
`...29937a9`. `getDataVirtualAddress()` at `...29720d0` returns the first
segment base **plus segment offset**, not packet data offset. The Port therefore
checks `offset <= bufferSize`, `length <= bufferSize-offset` and the pointer;
the CPU bridge adds that separate packet offset exactly once during copy.
No Apple object field is accessed by the probe.

RX preparation passes direction **2** to `prepareWithQueue` at `...297218e`;
the target forwards it to buflet preparation at `...2972206` and tail-dispatches
the packet prepare method at virtual `+0x190`. Its return remains a full EAX
status; `IO80211NetworkPacket` at `...22bfb1a` preserves a target virtual
return, and the concrete Broadcom override at `...14d4dde` checks full EAX.
The RX completion path calls `completeWithQueue` with direction 2 at
`...297a644`. Preparation does not update the separate transfer-direction
field, so the probe calls `setTransferDirection(2)` after successful preparation;
that setter at `...2972462` writes one 32-bit field. This direction write does
not by itself prove a complete RX metadata contract. The base preparation path
also does not visibly consume every buflet preparation status, so the Port
requires an independent `metadataProven` precondition and stays disabled here.

The direct pool `deallocatePacket(packet*)` callsite at `...299328a` is the
prepared-packet reclaim route. Its null path returns without initializing EAX;
the probe never reads a return value. Pool `+0x140` is pinned, but exactly-once
reclaim after a source-queue failure still needs a runtime ownership ledger.
The current compile-only Port has no quarantine storage for provenance mismatch
or teardown races; a real adapter must supply that before it may replace the
injected opaque Port in the CPU bridge.

Local reproducible audit with the available KC:

```sh
python tools/native_abi/audit_native_packet_port.py \
  --zig ../toolchain/zig-x86_64-windows-0.15.2/zig.exe \
  --kc /path/to/BootKernelExtensions.kc
```

On a macOS CI runner with Clang, omit `--zig` and `--kc` to perform the two
cross-target object and negative checks. The exact-KC slot check requires the
separately supplied local KC and its `components.json`. Host model tests remain:

```sh
python tools/native_abi/native_cpu_packet_bridge_test.py --zig /path/to/zig
```

No loaded native service, actual packet pool, workloop, WCL callback, native
Wi-Fi menu or association follows from this probe. The installed Ethernet
frontend is unchanged.
