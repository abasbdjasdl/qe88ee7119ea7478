# Sequoia startup support factories

This compile-only companion to `registration-evidence.md` targets recovery KC
SHA-256 `d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
It does not create or publish any service. The normal Ethernet driver does not
include these declarations. Four emitted compiler references must match
`support-symbols.json` exactly, independently of the vtable audit.

The controller needs two separate, real object chains before its base start:

* `CCLogPipe` → `CCLogStream`, returned as a borrowed pointer by `getLogger()`.
* `CCDataPipe` → `CCDataStream` → `CCFaultReporter` →
  `IO80211FaultReporter`, with the final wrapper returned by
  `getFaultReporterFromDriver()`.

Broadcom deferred start calls the raw factory at `0xffffff80014ec235`, puts its
result into the bus setter at `...14ec243`, calls the wrapper factory at
`...14ec28a`, and puts that result into a different setter at `...14ec298`.
The raw and wrapper fields are bus-private `+0x20` and `+0x28`, respectively;
Core's getter at `0xffffff800158f0de` returns `+0x28`. The wrapper retains the
raw reporter at `...2248c19` and releases it at `...2248c4b`. Base controller
start retains the wrapper at `...221dfc8`; a null result reaches panic at
`...221e07d`. An arbitrary non-null OSObject is not a substitute.

The logger factory path safe-casts a stream to `CCLogStream` at
`0xffffff8001646f4d`, stores it at bus-private `+0x228`, and bus start retains
an alias at `+0x10`. Core copies/retains that alias at private `+0x3748`
(`...158f29a..2ad`); its getter returns that pointer at `...1598775`.
Base start borrows it in a global at `...221d3ec`, cleared in stop at
`...221e779`. The logger must outlive that base cleanup.

## Argument buffers

`support_options.hpp` expresses only factory argument records, with compile-time
checks for the whole size and every interpreted field offset. Unknown fields
remain opaque; this file does not select a production configuration.

The pipe record is 0x350 bytes. Its first four values are separate u32 fields
(kind, log type, log data type, log policy), not two u64 values. Log init checks
`+4 < 2` at `...31b5ecb` and `+0xc < 2` at `...31b5ed2`. `+0x230/+0x238`
are a callback context/function pair consumed at `...31a5acf..aed`, not ordinary
integer logging flags. The function-pointer type remains deliberately opaque.

The stream record is 0x358 bytes. `+8/+0xc` are independent signed 32-bit
log/console levels; the old single u64 label conceals that distinction.
The initializer copies the complete 0x200-byte tail at `+0x158`
(`...31ba60c..618`), so passing a short prefix would be an out-of-bounds read.
`+0x148` is an optional retained OSData pointer (`...31baa49..5f`), distinct
from the optional callback pointers. Buffers must be fully initialized and
heap-backed or owner storage when used in a future kernel startup path.

## Outstanding runtime work

Static factory declarations make no claim about instance inheritance/vtables,
safe casts, `startPipe`/teardown dispatch, service attachment, workqueue default
lifetime, native linking, or superclass partial-start rollback. Factories
perform real attachment and start operations; they are not plain allocations.
In particular a vptr call at `+0x5c0` is IOService::start, not registerService
(`+0x5b0`). Do not infer that constructing the records publishes a Wi-Fi service.

The raw reporter retains the data stream and workloop, owns a timer, and removes
that event source during cleanup. The controller separately holds its wrapper
retain. Startup and stop must respect all of those owners; factory-symbol tests
alone do not establish successful cleanup after every base-start failure.
