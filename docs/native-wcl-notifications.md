# Offline WCL scan-result byte draft

`src/network/NativeWclBeacon.hpp` encodes one bounded **offline, known-partial** scan-result payload. It never calls `postMessage`, never starts a radio scan, and never claims that a native interface, WCL subscription, or menu works. Its returned `emissionReady` is always false. An encoded result is not association success, key installation, or authorization.

The only admitted target profile is Darwin 24.4.0 KC SHA256:

`d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`

The caller must actually verify that profile before relying on the draft. An enum supplied by the caller cannot detect which KC is running. Neither a payload size nor compilation against private headers establishes compatibility with another OS build.

## Interface and bounds

The input is a borrowed `const nativescan::Entry &`; output storage belongs to the caller. No large scan Store, Entry, or whole output is copied to the kernel stack. No allocation occurs, and no pointer is retained. The caller must keep the Entry readable and stable while holding the relevant scan-cache/controller gate and provide accurately sized writable destination storage.

`encode(profile, entry, destination, capacity)` returns `Status::knownPartial` and the exact payload byte count on success. The maximum is 2112 bytes: 64 bytes of metadata plus at most 2048 raw IE bytes. The source cache can hold 2304 bytes, but the encoder rejects 2049..2304 rather than truncating them.

The Entry must represent one complete, admitted observation, without dropped/truncated IEs. The cache's capture-completeness checks are a prerequisite; they cannot be reconstructed from an Entry alone. The encoder independently revalidates:

- A nonzero unicast BSSID, binary SSID length 0..32, and valid signal-unit/value combination.
- Band 2.4 GHz and channel 1..11 only, represented as 20 MHz. This is a byte-format limit, not regulatory authorization or a change to radio policy.
- Fully bounded IE TLVs, exactly one SSID IE, and an exact byte-for-byte match between that IE and the Entry's SSID.
- A hidden SSID requires an actual zero-length SSID IE. An absent SSID IE is rejected even when the Entry has no name; it cannot justify the supplied-SSID validity bit.
- The inner IE count and outer length always agree: `payloadBytes == 64 + ieLength`.

Only explicitly marked dBm samples set the RSSI field and its validity bit. Percent and unknown values remain unconverted, with RSSI-valid clear. The input validator accepts dBm -127..0, percentage 0..100, or unknown with value 0; invalid enum values are rejected.

All source/destination overlap is rejected before reads or writes. On every failure, `payloadBytes=0` and the destination's writable prefix `min(capacity,2112)` is zeroed. If a caller illegally aliases the Entry, that overlap is wiped too; callers must not reuse that Entry after such an error. No bytes beyond that bounded prefix are touched. A null destination returns failure without access. Success also clears the unused part of the bounded prefix, preventing older longer payload data from surviving.

The draft intentionally has no scan request identifier or complete-snapshot token. A future live bridge must bind snapshot generation/epoch and the WCL request outside this encoder and must not feed single-channel completion as a whole scan.

## Exact-KC producer and consumer evidence

The source evidence is preserved under workspace `outputs/R16-Native-WiFi/wcl-notification-*`. `wcl-notification-inspect.py` verifies the KC SHA256 and produces disassembly, event-name/callback maps, and `wcl-notification-beacon-metadata.json`; that JSON includes individual producer and consumer instruction addresses.

The event-name table identifies Apple event **201** as `APPLE80211_M_WCL_SCAN_RESULT`. Its WCL callback accepts total payload lengths 64..2112 inclusive (`0xffffff8002131778`). The producer calls `getBeaconMsgFromWLBSSInfo`, adds 64 to metadata's IE count, and posts that exact length (`0xffffff80016d6d3c..0xffffff80016d6d5d`). The converter copies the raw IE list from the source IE offset, capped at 2048 (`0xffffff800164cdc9..0xffffff800164cddd`). Our draft rejects an oversized list instead of inheriting that truncation.

The consumer `IO80211BSSBeacon::setBeaconDataFromMsg` trusts metadata's IE count when allocating/copying the tail (`0xffffff8002129dc8..0xffffff8002129dd5`). Merely passing the outer range check is therefore insufficient; strict inner/outer consistency is required.

All integers below are little endian bytes, not a C++ private-ABI declaration:

| Offset | Bytes | Draft field | Evidence |
| --- | --- | --- | --- |
| 0x00 | 4 | IE count | Producer 0x164cdd5, consumer 0x2129dc8 |
| 0x04 | 2 | AppleChannelSpec | Producer 0x164cca9, consumer 0x2129e14 |
| 0x06 | 32 | Binary SSID storage | Producer 0x164cc7d..0x164cc85 |
| 0x26 | 1 | SSID length | Producer 0x164cc70, consumer 0x2129e43 |
| 0x27 | 1 | Primary channel | Producer 0x164ccca, consumer 0x2129e19 |
| 0x28 | 1 | Unknown, zeroed | Not assigned semantic meaning |
| 0x29 | 6 | BSSID | Producer 0x164cc64/0x164cc68, consumer 0x2129dde |
| 0x2f | 1 | Unknown, zeroed | Not assigned semantic meaning |
| 0x30 | 4 | Signed RSSI, when valid | Producer 0x164cccd/0x164ccd2; consumer 0x2129eb6; getRSSI 0x212bdfe |
| 0x34 | 2 | Noise, zero and invalid | Actual optional field; no fabricated measurement |
| 0x36 | 2 | SNR, zero and invalid | Actual optional field; no fabricated measurement |
| 0x38 | 2 | Beacon period | Producer 0x164cca5; getBeaconPeriod 0x212c5e4 |
| 0x3a | 2 | Capability bits | Producer 0x164cc9c; getCapabilities 0x212c614 |
| 0x3c | 4 | Only justified flags | SSID 0x2; real dBm additionally 0x4000 |
| 0x40 | IE count | Unmodified complete IE list | Producer 0x164cddd; consumer 0x2134e2d |

Addresses in the table abbreviate the common `0xffffff8000000000` prefix. The complete addresses are in the evidence JSON.

Consumer flag bit 1 supplies the metadata SSID to `setBeaconDataEx`; bit 14 stores RSSI and its timestamp. Optional bits 12 and 13 gate noise and SNR and remain clear. Other flags are not fully interpreted and are not synthesized. Ordinary producer and consumer paths do not read/write metadata bytes +0x28/+0x2f; zero initialization matches the observed ordinary producer. This bounded observation does not prove all optional paths or future versions ignore them.

For channels 1..11, `WCLDeviceConfiguration::fillLegacy20MHzChanSpec` produces `channel + 0x4000 - 0x3000`, thus `0x1000 | channel` (`0xffffff8002201072..0xffffff8002201089`). Independently, `ChanSpecGetPrimarySpec` builds band bits + primary + 0x1000; `ChanSpecGetPrimaryChannel` decodes these eleven values, and `ChanSpecConvToApple80211Channel` yields version 1, the same channel, and flags 0x0a. The evidence script checks all eleven examples. Wider channels and other bands remain outside this encoder.

## Notification and lifecycle boundaries

Driver input scan-done is event **237**, with exactly four status bytes. WCL processes it and emits outward event **10**. Authentication/association input **211** requires 28 bytes; connect-complete input **213** requires 164 bytes; WCL's outward ASSOC_DONE **9** has a separate 460-byte join-status payload. A copied old `apple80211_message` enum or old payload cannot substitute for these WCL input contracts.

`IO80211Controller::postMessage` sends through its PostOffice. The payload is copied synchronously into the queue, but the interface pointer is stored without retain/release in the inspected path. Before a live bridge frees its interface, it must prevent new events and drain the proper queue. Glue presence, peer-manager gating, device configuration, WCL state, and userland event subscriptions also matter. A zero return can occur without an open IOUC pipe or after fallback, so it is not delivery proof.

`setLinkStateInternal(UP)` affects peer state, key-completion handling, Skywalk link status and IPv6. It must not be used to force a Wi-Fi icon before actual authentication/key completion. This offline encoder implements none of these lifecycle operations.

## Verification

`tests/network_native_wcl_beacon_test.cpp` covers field values, all eleven supported channels, binary and hidden SSIDs, duplicate/missing/mismatched SSID IEs, malformed and randomized TLVs, 2048/2049/2304/65535 lengths, signal units and their endpoints, invalid addresses/profiles, exact/tiny output allocations, alias rejection in both directions, stale-output clearing, and untouched guard bytes.

Local Zig 0.15.2 C++17 tests pass with `-Wall -Wextra -Werror -O1 -fsanitize=address,undefined -fno-omit-frame-pointer`. A separate x86_64-macOS freestanding object containing the encoder compiled with `-O2 -Wframe-larger-than=512 -Werror -fno-exceptions -fno-rtti -fno-stack-protector -mno-red-zone`. These checks establish bounded byte generation and compilation only; no native menu behavior was tested.
