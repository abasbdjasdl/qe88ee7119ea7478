# Return ABI deltas and new-method status

KC SHA-256: `d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
The reference headers are from itlwm commit
`53c51c2cdd6e4b69beb91f310d74c53422b0f8bd`. These are mismatches with the
current local declarations, not proof that Apple's historical return type changed.
Non-template C++ mangling omits return types; matching symbol names are insufficient.
All addresses below have prefix `0xffffff800`. Full instructions and symbols are
in `native-interface-disassembly.json` / `.txt`, reproduced by
`native-interface-inspect.py`. `IOReturn` below means the observed 32-bit status
ABI; a typedef name cannot itself be recovered from assembly.

## Corrections supported by nontrivial implementations

| Method / reference declaration | Target declaration recommendation | Evidence |
| --- | --- | --- |
| Controller / Skywalk `isCommandProhibited(int)`; reference returns `bool` | `virtual IOReturn isCommandProhibited(int);` (Controller remains pure) | Concrete Core sets EAX to `0x3a` at `15e90e3` and returns it at `15e9223`; Skywalk `2260244` tail-dispatches to Controller vptr+`0xc50`. Wrappers such as `21f151f` test full EAX and return the same nonzero status. It is not a normalized boolean. Target declaration probe explicitly asserts both returns independently of mangled slot names. Detailed call evidence: `native-required-callbacks-*`. |
| Skywalk `getSelfMacAddr()`; header line 149 returns `ether_addr*` | `virtual ether_addr getSelfMacAddr();` | Function `225eaa2` packs the six address bytes into RAX; caller `160403c` extracts EAX and high-word AX into six bytes at `1604049` / `160404f`. No pointer dereference. |
| Skywalk `getSupportedMediaArray(UInt*,UInt*)`; line 82 returns `void*` | `virtual IOReturn getSupportedMediaArray(UInt*,UInt*);` | `225f5b4` / `225f5bc` produce `0xe00002f0` / `0xe00002bd`, successful path sets zero, `225f5cb` returns EAX. Synchronize `IOSkywalkNetworkInterface.h:52` too. |
| Skywalk `findOrCreateFlowQueue(IO80211FlowQueueHash)`; line 132 returns `bool` | `virtual IO80211FlowQueue* findOrCreateFlowQueue(IO80211FlowQueueHash);` | `225f761` calls Database::find, saves RAX in R14, returns R14 at `225f782`; later path treats the same queue object as a virtual object and passes it to `insert(IO80211FlowQueue*)`. Returning only boolean loses the object. |
| Skywalk `createEventPipe(IO80211APIUserClient*)`; line 176 returns `UInt64` | `virtual IOReturn createEventPipe(IO80211APIUserClient*);` | Error paths `225d307` / `225d36d` return `0xe0000001` / `0xe00002bc`; `225d344` sets success zero. |
| Skywalk line 141 and Infra line 70 `createLinkQualityMonitor(IO80211Peer*,IOService*)` return `UInt64` | `virtual IOReturn createLinkQualityMonitor(IO80211Peer*,IOService*);` | Infra `22d4947`, `22d4e34`, `22d4e57` set 32-bit errors `0xe00002c9` / `0xe00002c7`; success clears R15D and `22d4eb0` returns EAX. Base `225fd78` returns `0xe0000001`. |
| Infra `getWCL_TX_RX_LATENCY(apple80211_wcl_tx_rx_latency*)`; line 78 returns `void*` | `virtual IOReturn getWCL_TX_RX_LATENCY(apple80211_wcl_tx_rx_latency*);` | `22d5dcc` starts with `0xe00002bc`, success `22d5e50` clears it, and `22d5e53` returns EAX. Wrapper dispatches through vptr+`0xe78`. |
| Skywalk `findExistingFlowQueue(IO80211FlowQueueHash)`; line 134 returns `UInt64` | `virtual IO80211FlowQueue* findExistingFlowQueue(IO80211FlowQueueHash);` | `225f98e` tail-calls Database::find with original RSI. Pointer return classification agrees with the nontrivial find/create path. Current UInt64 has the same register width but erases the pointer contract. |

The inherited names must have consistent declarations throughout the overlay.
This table does not make the old slot ordering valid. In particular, correct
argument types, const qualifiers, and named `IO80211FlowQueueHash` must also match.

## Newly requested methods: enough evidence for declaration

| Class / declaration recommendation | Evidence |
| --- | --- |
| Controller `IO80211IORecursiveLock* allocIO80211RecursiveLock()` | `2226693` tail-calls `IO80211IORecursiveLock::allocWithParams`; factory `21268ae` allocates/constructs that class and returns object-or-null at `2126911`. |
| Controller `IO80211PostOffice* CreatePostOffice()` | `2225f7b` tail-calls `IO80211PostOffice::allocWithParams`; factory `22ac7c6` allocates/constructs that class and returns object-or-null at `22ac829`. |
| Controller `IO80211PostOffice* getPostOffice()` | Start calls vptr+`0xdc0` (CreatePostOffice) at `221d6cb`, stores result in private state+`0xb08` at `221d6d8`; getter returns exactly that field at `2225f8b`. |
| Controller `virtual CCLogStream* getLogger() const = 0` | Raw slot426/vptr+`0xd40` is pure in Controller. Concrete Core entry is `__ZNK16AppleBCMWLANCore9getLoggerEv` at `159876a`; Core association calls that slot and passes result to `CCLogStream` methods. Const is in mangling. |
| Controller `virtual IO80211FaultReporter* getFaultReporterFromDriver() = 0` | PCIe deferredStart obtains a CCFaultReporter at `14ec235`, stores it through `setFaultReporter(CCFaultReporter*)` at `1589789` in bus-private +0x20. It wraps it with `IO80211FaultReporter::allocWithParams` at `14ec28a`, then `setIO80211FaultReporter(IO80211FaultReporter*)` stores that result at bus-private +0x28 (`158979b`). Core getter `158f0e2..158f0f7` follows its bus pointer and returns exactly +0x28. The wrapper retains the raw CC at `2248c19`, releases it at `2248c4b`; Controller separately retains the wrapper at `221dfc8`. A generic `void*` obscures this required object type. Null reaches panic at `221e07d`. Detailed evidence: `native-support-objects-*`. |
| Skywalk `IO80211Peer* createPeer(const unsigned char*,IO80211PeerManager*)` | `225e281` tail-calls `IO80211Peer::withAddressAndManager`; factory `21cea6e` allocates/constructs IO80211Peer and returns pointer-or-null at `21cead8`. |
| Skywalk `IO80211Controller* getController()` | Getter `225fbfc` returns private state+`0x30`; e.g. Infra `22d5e0a` calls this virtual slot, then passes the returned object as `this` to `IO80211Controller::getTxLatencyClearOnRead` at `22d5e16`. |
| Skywalk `CCLogStream* getLogger() const` | Mangling includes `K`; `225fdae` returns private logger pointer. Concrete interface calls getter slot+`0xd08` and passes result to `CCLogStream::shouldLog` / logging methods, e.g. `1565a73` / `1565a86`. |
| Skywalk `IOReturn setUserBufferInfo(IOMemoryDescriptor*,unsigned long long)` | `225d4a6` returns `0xe00002c7`; full parameter encoding is `EP18IOMemoryDescriptory`. |
| Skywalk `IOReturn getDataPathInterfaceStats(apple80211_data_path_interface_stats*)` | `22602e0` returns `0xe00002c7`. |
| Skywalk `IOReturn getDataPathPeerStats(apple80211_data_path_peer_stats*)` | `22602ec` returns `0xe00002c7`. |
| Skywalk `IOReturn updateInterfaceDataStats(apple80211_data_path_interface_stats*)` | Nontrivial body clears EBX on success at `2260373`, error path sets `0xe0000001` at `22603f0`, returns EBX in EAX at `2260394`. |
| Skywalk `IOReturn updatePeerDataStats(apple80211_data_path_peer_stats*)` | Nontrivial body success clears EBX; errors `0xe0000001` / `0xe00002f0` at `22607a4` / `22607d3`; returns EAX at `2260748`. |
| Skywalk `IOReturn getNClearTxRxLatency(apple80211_latency_all_ac*,apple80211_latency_all_ac*)` | `22607fa` returns `0xe00002c7`; both pointer arguments are explicit in mangling. |
| Skywalk `IOReturn getLastTxTimeStamp(unsigned long long&)` | `2260806` returns `0xe00002c7`; reference parameter is `ERy`. |
| Skywalk `IOReturn getLastRxTimeStamp(unsigned long long&)` | `2260812` returns `0xe00002c7`; reference parameter is `ERy`. |

## MAC programming callback correction

For the pinned 15.4.1 KC, Skywalk raw slot 417 (`setMacAddress(ether_addr&)`)
returns `IOReturn`, not `void`. `IO80211MacAddressAgent::setMacAddress` dispatches
through vptr+0xcf8 at `0xffffff800225b213`, tests EAX at `225b219`, and branches
on failure at `225b21b` before updating its stored address. Independently,
`AppleBCMWLANSkywalkInterface::setMacAddress` calls `setCurEtheraddr` at
`1567fdf`, preserves its 32-bit status in R14D, and returns it in EAX at
`1568034`. A void override leaves the caller's status undefined.

The prototype now explicitly returns `kIOReturnNotReady` until a hardware
programming callback is bound. Its member-pointer static assertion prevents a
return-type regression. This does not prove address initialization, interface
registration, or hardware programming works.

The separately named raw slot 335 `setHardwareAddress(ether_addr*)` is also
`IOReturn` throughout the inheritance chain. The IOSkywalkEthernetInterface
base sets EAX to `0xe00002c7` at `297c01a`. The IO80211SkywalkInterface override
returns `0xe00002c2` for null input at `225eb2e`, `0xe00002bc` for a missing
MAC agent at `225eb4f`, and otherwise tail-calls the above status-returning
MacAddressAgent at `225eb0f`. Both declarations are corrected together. The
existing private expansion must already exist before invoking this setter;
its pointer is dereferenced before the MAC-agent null check.

### Address initialization order (same pinned KC)

`IO80211InfraInterface::init()` at `22cddaa` calls the no-argument Skywalk
initializer, then allocates its separate expansion at object+0x118. The
Skywalk `init(IOService*,ether_addr*)` overload at `225bd9e` calls `initIvars`
even when the Skywalk expansion at +0x110 already exists. In that case
`initIvars` zeros all 0xf0 bytes at `225b900..225b913`; it does not preserve
an existing role, provider, logger or MAC-agent pointer. The overload copies
six initial address bytes to expansion+0xe4 at `225be00..225be14`.

Skywalk `start()` subsequently passes that initial address to
`IO80211MacAddressAgent::withOptions` at `225c3df`. Consequently the MAC
initializer must never be used as a post-start address setter. A candidate
initialization sequence must finish address initialization before assigning
role/id, attaching, or starting the interface, and verify the real agent's
address after start. This is ordering evidence, not a tested initialization
recipe or permission to write any private field directly.

## Return types not established; do not silently guess

| Method / verified argument list | What the current evidence proves |
| --- | --- |
| Controller `debugStateInit()` | Empty frame/ret at `221e41e`; no returned value is initialized. Void is plausible, not independently proven by a caller. |
| Controller `getActionFramePoolCapacity()` | `2225fa4` sets EAX to `0x100`; no signedness/exact return width is recovered. |
| Skywalk `getDataQueueDepth()` | Tail-dispatches to Controller `getDataQueueDepth(OSObject*)` at `225e957`; base returns `0x400`, Core override loads a 16-bit configured capacity into EAX at `163c09b`. Integer result supported; source return width/signedness unresolved. |
| Skywalk `findPeer(ether_addr&)` | Only zero-return stub at `22607e8`; pointer return remains unproven. |
| Skywalk `attachPeer(ether_addr*)`, `detachPeer(ether_addr*)` | Empty bodies `22602b8` / `22602be`; argument types established, return types not. |
| Skywalk `setDebugTrafficReport(bool)` | Empty body `22602c4`; argument established, return type not. |
| Skywalk `getLastQueuePacketTime(ether_addr*)`, `getLastRxUnicastLinkActivityTime(ether_addr*)` | Only zero-return stubs at `22602f4` / `22602fc`; cannot infer a 64-bit timestamp solely from the names. |
| Skywalk `logTxLatency(unsigned char*,unsigned int,unsigned long long)`, `logRxLatency(unsigned int,unsigned long long)` | Empty bodies `22607e2` / `22607f0`; arguments are encoded in mangling, returns unresolved. These differ from Controller's methods of the same short names. |
| Infra `createLQMData()` | Only zero-return stub at `22cdf58`; do not infer bool, pointer, or status. |
| Controller old `UInt64 getAVCAdvisoryInfo(IO80211InterfaceAVCAdvisory*)` | Empty body `2225f64` does not establish a valid UInt64 result. Void is plausible but no independent caller proof in this audit. |
| Skywalk / Infra old `SInt64 getWmeTxCounters(unsigned long long*)` | Base tail-calls logging; Infra fills outputs and on an early branch retains a private pointer in RAX, otherwise a counter. No stable numeric return demonstrated; do not consume the old declared result. |
| Skywalk / Infra old `void* getLQMSummary(apple80211_lqm_summary*)` | Base returns `EAX=0xffffffff`, Infra writes outputs and returns zero. Strong suspicion of signed 32-bit status, but exact contract not established by a nontrivial error path/caller. Do not interpret result as a valid pointer. |
| Skywalk old `UInt64 findOrCreateFlowQueueWithCache(hash,bool*)` | Only logging plus zero in this implementation. Pointer likely by naming but not independently established here. |

## IO80211FlowQueueHash

The three local headers `IO80211SkywalkInterface.h:24`, `IO80211Interface.h:32`,
and `IO80211VirtualInterface.h:7` use `typedef UInt64 IO80211FlowQueueHash`.
Actual symbols encode a named type `20IO80211FlowQueueHash` for value arguments,
and `P20...` / `PK20...` for pointers. A typedef to an integer cannot reproduce
these exported names. Mangling itself does not distinguish class, struct, or union.

The target value calling convention is well evidenced: `findOrCreateFlowQueue`
at `225f73f` copies RSI into RBX as the hash; its database lookup takes that value
as `unsigned long long`. `findExistingFlowQueue` at `225f98e` tail-calls
`Database::find(unsigned long long)` without changing RSI. The const-pointer
`removePacketQueue` path loads exactly one qword from the hash pointer at
`225fa22` / `225fa3b`, then calls the same database's lookup/remove functions.

Thus the observed active payload is eight bytes and the value travels in one
integer register. A named trivially-copyable opaque eight-byte aggregate is a
reasonable **compile-probe candidate** for these observed call sites; it is not
proof of Apple's full source declaration, `sizeof`, alignment, bitfields, or
padding. Do not infer an address-field layout or use this to access arbitrary
embedded structures. Compare emitted call/return code with the target before
promoting the candidate into a loadable ABI. This audit did not build or load it.
