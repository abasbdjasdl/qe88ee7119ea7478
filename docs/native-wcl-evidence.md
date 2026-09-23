# Native WCL message evidence and observation-only decoder

This document describes a static inspection of one local Sequoia 15.4.1 kernel
collection. It does not define a complete private C++ ABI, prove a native Wi-Fi
join, or authorize passing arbitrary kernel/user pointers to the decoder.

Target: `outputs/R16-Native-WiFi/BootKernelExtensions.kc`, SHA-256
`d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
Paths under `outputs/` are relative to the workspace root, outside this repository.
Re-run `outputs/R16-Native-WiFi/wcl-assoc-analyze.py` with the bundled Python
runtime; it verifies this hash and emits `wcl-assoc-disassembly.json` and `.txt`.
The script reads local KC symbols, segments, chained vtable pointers, and the
existing component inventory. It neither downloads nor loads kernel code.

## Message length and object identity

`WCLJoinManager::handleSendCandidateToDriver` allocates zeroed `0x3dc` (988)
bytes at `0xffffff8002205cc8` / call `0xffffff8002205ccd`. The returned pointer
is passed to `WCLJoinRequest::fillAssocCandidatesList` at
`0xffffff8002205d14` / `0xffffff8002205d1a`. The same pointer reaches
`WCLFsmManager::cmdIouc`, with operation `0x1ba` and input length `0x3dc`,
at `0xffffff8002205e9d` through `0xffffff8002205eb3`; it is freed with the
same size at `0xffffff8002205ec0` through `0xffffff8002205ec6`.
This proves the allocation and command-input length on this path, not the
`sizeof` or layout of every version of an Apple private class.

Register identity matters when interpreting offsets:

| Function | Object/state registers | Message register |
| --- | --- | --- |
| `fillAssocCandidatesList` at `0xffffff800222c582` | Entry RDI / saved RBX is the request object; `[this+0x10]` is separate internal state | Entry RSI / saved R14 |
| `addAssocCandidates` at `0xffffff800222c9dc` | Entry RDI / R13 is the request; RSI / R14 is a candidate object | Entry RDX / RBX |
| `AppleBCMWLANJoinAdapter::performJoin` at `0xffffff800157fece` | Entry RDI / R14 is the adapter; R13 later points at adapter+0x10 | Entry RSI / saved RBX, until register reuse on later paths |

Object/state offsets are not message offsets. In particular, the consumer's
internal candidate copy uses a different stride from the input message.

## Candidate bytes supported by instructions

The message contains a little-endian 32-bit count at `+0x214`, followed by
records beginning at `+0x218`, with an observed input stride of 18 bytes.
`fillAssocCandidatesList` clears count at `0xffffff800222c5a0` and invokes
`addAssocCandidates` once on its successful producer path at
`0xffffff800222c944`. The latter reads count at `0xffffff800222c9f6`, computes
`9 * count` at `0xffffff800222c9fc` and uses scale 2 for byte addressing, then
increments count at `0xffffff800222cb0c`.

| Message offset for record 0 | Size | Evidence and deliberately limited interpretation |
| --- | --- | --- |
| `0x218` | 1 | Call through vtable byte offset `0x378` at `0xffffff800222ca9d` resolves to `IO80211BSSBeacon::isBSSSAEPKCapable`; store at `0xffffff800222caa3`. Retained as a raw capability byte. |
| `0x219` | 1 | No meaning established. The decoder neither exports it nor requires zero. |
| `0x21a` | 2 | Call through `0x2b0` at `0xffffff800222ca86` resolves to `getEncryptionMode`; word store at `0xffffff800222ca8c`. Opaque security value, not a net80211 enum or proof of authentication support. |
| `0x21c` | 6 | Destination at `0xffffff800222ca42` / `0xffffff800222ca46`; call through `0x170` at `0xffffff800222ca50` resolves to `getAddress`. Getter writes four plus two bytes. BSSID. |
| `0x222` | 6 | Destination at `0xffffff800222ca5e` / `0xffffff800222ca62`; call through `0x190` at `0xffffff800222ca6f` resolves to `getOWETransAddress`. Getter writes four plus two bytes. OWE transition address, not evidence of completed OWE negotiation. |
| `0x228` | 2 | Source `getChanSWSpec` call at `0xffffff800222ca0b`, `ChanSpecGetPrimaryChannel` at `0xffffff800222ca17`, mask/add sequence through `0xffffff800222ca33`, word store at `0xffffff800222ca75`. Consumer calls `AppleBCMWLANChanSpec::getBCMChannelSpec` at `0xffffff80015813d4`. Opaque Apple channel specification, not a plain channel number. |

The stored channel word follows the observed calculation, truncated to 16 bits:
`(source & 0xffffc000) + uint8_t(primaryChannel(source)) + 0x1000`.
The byte-view decoder preserves the word without interpreting its bit layout.

The consumer starts at message `+0x21a` at `0xffffff800158120e` and advances
input by `0x12` at `0xffffff800158127a`. Its separate internal copy advances
by `0x44` at `0xffffff8001581276`. A subsequent loop also advances an input
pointer by `0x12` at `0xffffff8001581562`. These are independent confirmations
of the input stride, not evidence that our driver can use the Broadcom object's
layout or dispatch ABI.

## Count and bounds contract

No fixed maximum candidate count was established by an explicit producer or
consumer validation. The first consumer copy loop reads the supplied count
without a demonstrated maximum check. Separate checks on a later Broadcom
temporary buffer do not validate the original input count.

Known non-candidate fields start at `+0x2cc`: a word store occurs at
`0xffffff800222c70c`, and a 257-byte copy to `+0x2ce` is prepared at
`0xffffff800222c724` and executed at `0xffffff800222c73a`. The 180-byte gap
from `0x218` to `0x2cc` fits ten record strides, but this is not proof of an
Apple protocol maximum. Calculating 25 candidates from the entire 988-byte
message tail is invalid because it would interpret those other fields as records.

`src/network/NativeWclCandidates.hpp` therefore has this deliberately narrow
contract:

- The caller first verifies the target KC profile above and provides readable,
  locally owned bytes. A 988-byte length alone cannot identify a version or make
  a pointer valid. Unvalidated kernel/user pointers must not be passed directly.
- Only exact length 988 and counts 0 or 1 are accepted. Every count above 1
  returns `unsupportedCount`; this is a decoder restriction, not an Apple limit.
- Count 0 produces an empty observation. In `performJoin`, the branch at
  `0xffffff8001581204` skips the first copy loop, and the branch at
  `0xffffff8001581387` skips the next candidate loop. Neither observation proves
  cancellation or successful association.
- Every field read has an explicit span check using subtraction to avoid size
  overflow. Only the fields in the table, excluding the unknown byte, are copied.
  Prefix credentials, SSID/key material, and trailing fields are not decoded.
- Results own their bytes and contain no borrowed pointers. All failures clear
  the complete output, including padding. A temporary observation delays output
  writes until input reads finish, including when output overlaps count/record
  input storage.
- No decoded result reaches join, scan, key installation, or native UI dispatch.

Host regression tests cover the proven fields, a nonzero unknown byte, count 0,
unsupported counts including integer extremes, exact-length failures, null input,
overflow-safe span checks, owned output, and success/failure with output aliasing
the actual count and candidate bytes. Passing these tests validates the local
decoder; it does not establish real macOS dispatch or successful Wi-Fi access.

## Remaining native ABI questions

The observed setter `apple80211setWCL_ASSOCIATE` at `0xffffff80021fa71a`
performs an `IO80211InfraProtocol` metaclass check and dispatches through vtable
byte offset `0x1288` at `0xffffff80021fa755`. This alone does not establish a
safe subclass layout, ownership/lifetime contract, request version negotiation,
or complete input validation for our driver. Those remain separate work.

For scan, the independent local artifacts `wcl-scan-inspect.py`,
`wcl-scan-findings.json` / `.md`, and `wcl-scan-disassembly.json` / `.txt`
under the same outputs directory describe the same pinned KC. They show
`WCLScanManager::sendRequest` at `0xffffff8002132dea` allocating, passing to
command `0x1b9`, and freeing a 5456-byte (`0x1550`) request. The
`getScanRequestForDriver` unsigned-reference argument is read into request `+0x08`
and used by `setScanHomeAwayTime`; it is not an output length. Producer value 1
at request `+0x04` enables the consumer's private scan MAC behavior and is not a
proven version field. No scan decoder or complete native scan layout is claimed.
