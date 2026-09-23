# Compile-only native startup prototype

Target KC SHA-256: `d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
This source is never linked or invoked. It produces real C++ factory, metaclass,
virtual getter and qualified superclass-start calls for an eventual native owner.
It creates no test OSObjects, supplies no dummy logger/reporter and touches no
production driver. The output remains an object file, not a kext.

The source uses the generated target `Airport/Apple80211.h`. Its narrow CC
declarations suppress old option/instance definitions. They describe enough
inheritance and virtual ordering to emit the known calls, not complete CC object
layouts. There is no `new`, object-size assumption, private-field access, `asm`
alias or call through a copied kernel address. Three uncalled entries precede
`CCPipe::startPipe`; the compiler's actual member-pointer constant must encode
vptr offset `0x868`, and its declared return is explicitly checked as `bool`.

`audit_startup_prototype.py` checks the pinned upstream and SDK revisions, compiles
at O0 and O2, compares the complete undefined-symbol set, and verifies five
compiler-emitted member-pointer values. They are `{vptr byte offset + 1, 0}`
under the pinned x86_64 Itanium ABI: pipe start `0x868`, logger `0xd40`, work queue
`0xc68`, fault wrapper `0xd80`, base start `0x5c0`. A compiling mutation that inserts a virtual before
startPipe must be rejected as slot drift. The audit hashes source, options,
manifest, script, builder, generated headers and output objects. It never links
or executes those objects. It complements the full seven-class overlay audit;
it does not replace that audit or verify uncalled CC slots.

Example after the normal native overlay audit has generated its output:

```sh
python tools/native_abi/audit_startup_prototype.py ../itlwm-reference ../MacKernelSDK build/native-sequoia-contract --zig ../toolchain/zig-x86_64-windows-0.15.2/zig.exe
```

## Concrete support sequence and references

The zero-initialized ledger belongs in durable controller-owned storage. All four
factory records remain there, avoiding roughly 3.4 KiB of temporary kernel-stack
options. The initializer explicitly clears each complete 0x350/0x358-byte record,
including all callbacks, optional OSData and the copied tail.

1. Retain the real controller and provider before entering any factory; record
   `constructing`. These are distinct owned references even if pointers coincide.
2. Obtain a real IO80211WorkQueue. Construct log pipe, check its actual metaclass,
   virtually start the pipe, then construct/check the log stream.
3. Construct/start a separate data pipe and construct/check its CCDataStream.
   Pass that actual data stream and native workqueue to the raw reporter factory,
   then construct the actual IO80211FaultReporter wrapper.
4. Keep one factory-owned raw pointer per result. Checked logger/data-stream
   aliases borrow those references; casts never acquire an extra owner.
5. Before base entry, call the real controller's logger/workqueue/fault getters
   and require their returned pointers to match the ledger. Future native owner
   callbacks must be wired to the borrowed ledger getters; this prototype does
   not provide an all-unsupported concrete controller.
6. Record `baseEntered` before the qualified `IO80211Controller::start` call.
   Record `baseReady` only on a true result. In this kernel compilation the
   qualified call references `__ZTV17IO80211Controller`; the ordinary C++ call
   suppresses an override, with no assembler alias.

Only step 6's superclass return is represented by `baseReady`. No interface
registration, BSD attach, WCL discovery, radio, DMA, packet path or Wi-Fi menu is
activated. The caller must still prove the runtime KC profile; compile-time hash
pinning is not runtime profile verification.

The callback-free log option values come from target family setup at
`0xffffff800221d94b..221da55`: capacity 0x10000, notification amount 0x6666,
threshold 1000, opaque words at +0x224/+0x228 set to 0x200000/2, and stream
log/console -1/-1 plus +0x150=0x96. Initial log kind/type/data/policy stay zero,
as the zeroed family producer does. Driver-owned names replace Apple identity.
The data profile uses kind/type/data 1/2/2 and capacity 0x80 from
`...1647058..16470a6`, with kind 1 and levels -1/-1 for the stream. Driver-specific
callback context/function remain null. The exact record sizes/offsets are checked
in `support_options.hpp`; these candidate values are not a tested production
configuration or a claim that opaque fields have known semantics.

Factory and subtype evidence is preserved in `support-symbols.json` and
`support-evidence.md`. `startPipe` is the bool virtual at raw slot 271 in all
three same-KC pipe tables; its function starts at `0xffffff80031a4454`. Logger,
workqueue and wrapper getter ordering comes from `darwin24_4.json`. WorkQueue's
actual IOWorkLoop inheritance is established by its metaclass constructor at
`...2253636..2253642`. MetaClass pointers and safeMetaCast imports are real
undefined symbols, never fabricated runtime objects.

## Failure containment and remaining lifecycle blockers

Any failed factory, metaclass check, pipe start, getter wiring or base start
records the exact failure and enters `quarantined`. There is no automatic retry,
reverse release, manual `free`, `stopPipe`, guessed partial base stop, or default
workqueue replacement. All returned factory references and the controller/provider
retains remain owned. This intentionally retains resources; it is not safe unload.

The later `native-support-lifecycle-findings.md` supersedes the earlier suggestion
to reverse-release on a pre-base failure. Factories can already attach services;
stream start publishes asynchronously. `startPipe` false can mean a successful
start followed by failed deferred capture (`...31a453a..31a4565`). `stopPipe`
clears state and detaches, without performing stop/terminate; after the start-fail
path clears the owner, concrete stop's unguarded owner dereference is unsafe.

The base start's failure branches do not perform complete rollback. It may have
borrowed the logger globally, retained the wrapper and created timers before
returning false. `free` does not duplicate stop's cleanup. WorkQueue can be held
as a bare default-global pointer, while PostOffice/TimerFactory also borrow it.
The prototype therefore keeps it alive in quarantine.

Unresolved before any live frontend:

- Valid real controller init/start prerequisites and all required callbacks;
  namespace/profile/kext symbol-set eligibility and successful kernel linkage.
- Safe normal teardown and partial-start recovery, including published services,
  timer/queue drain, PostOffice/TimerFactory/RNGAgent ownership and global logger
  interference with other controllers.
- IOKit's externally triggered stop/detach and provider removal. A self-retain
  protects memory only; it cannot stop these lifecycle operations or a panic
  inside an Apple factory/base method, and retaining a provider cannot preserve
  removed hardware.
- Native interface, packet queues, registration and BSD/WCL/data-path wiring.

All validation reported for this prototype is offline compilation/static evidence.
No factory, slot or support initialization has been executed on macOS.
