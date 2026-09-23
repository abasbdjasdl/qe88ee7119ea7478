# Pinned WCL scan request decoder

`src/network/NativeWclScan.hpp` extracts a narrow scan-request subset from a
locally owned buffer. This is an offline parser, not native dispatch or radio
admission. Every return, including `Status::knownSubset`, has
`emissionReady == false`. It has no driver, firmware, IO80211, user-client,
credential, or event-emission dependency.

The only supported target profile is the local macOS 15.4.1 / Darwin 24.4.0
x86_64 KC whose SHA-256 is
`d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
The caller must independently establish this profile. A caller-supplied enum
does not discover or verify a running kernel. Success is not a compatibility
claim for another macOS release, build, architecture, or KC.

## Evidence and interpretation

The reproducible local evidence is under
`outputs/R16-Native-WiFi/wcl-scan-admission-*`, alongside the earlier
`wcl-scan-*` and `wcl-notification-*` disassemblies. The instruction addresses
below belong to the pinned KC, not the public Broadcom source.

| Field / property | Pinned-KC evidence | Decoder treatment |
| --- | --- | --- |
| Exactly 5456 bytes | `WCLScanManager::sendRequest` allocates `0x1550` at `0xffffff8002132e1e`, supplies `r8d=0x1550` at `0xffffff8002132ee6` to command `0x1b9`, then frees that size | Reject every shorter or larger buffer before reading it |
| `+0x00`, u32 | Producer copies an upstream header; no version gate proved | Retain as `opaqueSourceHeader`; never treat it as a version or scan ID |
| `+0x04`, byte | Producer writes 1 at `0xffffff80022bccfa`; consumer tests it at `0xffffff80016d37ce` and calls private-MAC refresh | Accept 0/1 only when trusted private-MAC policy is `unsupported` |
| `+0x08`, u32 | `setScanHomeAwayTime` call at `0xffffff80016d37c9`, with firmware IOVAR at `0xffffff80016d35f3` | Preserve `homeAwayMs`; this is not a length or whole-scan timeout |
| `+0x0c`, u32 | Additional low-power/retry policy | Require zero; no inferred semantics for nonzero values |
| `+0x10`, u32 | Consumer at `0xffffff80016d3ce0..3cf5` maps 2 to infrastructure, 1 to IBSS, other values to firmware any | Accept and preserve 2 (infrastructure) or 3 (any); reject IBSS and unknown source values |
| `+0x14`, 6 bytes; `+0x1c`, u32; `+0x20`, 32 bytes | BSSID and single SSID filter; zero BSSID takes broadcast branch at `0xffffff80016d3999..3cd4` | Require all zero, including unused name storage |
| `+0x40`, u32 | Selector jump tables at `0xffffff80016d67fc`, `0xffffff80016d680c`, `0xffffff80016d681c` | 1 remains active; 2 remains passive. Reject high-accuracy, low-span and unknown modes |
| `+0x44`, u16 | WCL bit `0x08` becomes firmware bit `0x04` at `0xffffff80016d4389..438d` | Accept exactly 0 or 8, retain the requested permission without expanding the trusted channel mask |
| `+0x48`, `+0x4c`, u32 | Active and passive dwell, with producer defaults at `0xffffff80022bce50..ce5f` | Preserve both timings; accept 1..1000 ms |
| `+0x50`, u32 | Home time; consumer converts zero to firmware default `-1` at `0xffffff80016d44d4..44e6` | Preserve value and set `homeRestDefault` when zero; accept 0..60000 ms |
| `+0x54`, u32; `+0x58`, 400 entries of 12 bytes | Producer copy at `0xffffff80022bcd55..cd68`; consumer reads channel and flags at `0xffffff80016d451a..4524`, increments by 12 at `0xffffff80016d4582` | Require 1..11 distinct entries, preserve order, canonical `{1, channel, 0x0a}` only |
| `+0x1318`, u32; `+0x131c`, 560 bytes; `+0x154c`, u32 | Additional SSID-list and short-SSID filters | Require all zero |

The byte ranges `+5..7`, `+0x1a..1b` and `+0x46..47` are also required to be
zero. No metadata is extracted from unused channel slots. Their contents do not
increase the count or alter the requested channels. Array endpoints are
compile-time checked against the exact message size.

### Default flag 8 and allowed channels

The WCL producer normally ORs bit 8 at `0xffffff80022bce83..ce89`, except its
restricted-country branch. The consumer translates that bit to firmware bit 4.
Google's published Broadcom `bcm4390` source, pinned at commit
`b71905f4720059d52bca727b2f6004664850d196`, names firmware bit 4
`WL_SCANFLAGS_PROHIBITED`: permission to include otherwise prohibited scan
channels. Firmware bit 8 is the distinct off-channel-results flag. These must
not be confused. This interpretation combines the KC's bit conversion with
the published firmware interface definition; it does not import the source's
entire firmware ABI into our driver.
([Pinned firmware definitions](https://android.googlesource.com/kernel/google-modules/wlan/bcmdhd/bcm4390/+/b71905f4720059d52bca727b2f6004664850d196/include/wlioctl_defs.h))

The decoder takes separate `permittedActiveChannels` and
`permittedPassiveChannels` from a trusted driver policy snapshot. Bit 0 means
channel 1 and bit 10 means channel 11. Both default to zero; bits outside that
range reject the policy. Every requested channel must be in the mask for the
actual requested mode, even if flag 8 is present. No channel is dropped,
substituted, or admitted because the request asks for prohibited-channel
permission. This allows a flags-8 request whose explicit channels already meet
the stricter driver policy, without adopting broader scanning permissions.

Channel 1..11 is the current implementation boundary, not a declaration that
those channels always permit active scanning in every region. The caller must
derive the masks from actual hardware, regulatory and current radio policy;
neither the payload nor a test fixture is an authority for that policy. Unknown
policy must supply an empty mask or reject before parsing. Policy changes
between parse and execution require fresh validation by the eventual dispatcher.

`AppleBCMWLANCore::getChanSpec` tests 20 MHz and 2 GHz flags at
`0xffffff800160b147..150` and `0xffffff800160b208..20b`.
`ChanSpecConvToApple80211Channel` at `0xffffff800213065d` independently produces
the canonical `{1, channel, 0x0a}` entry from the known 2 GHz, 20 MHz chanspec.
The parser requires those exact words rather than masking unknown flags.
Count zero can request default channels in Apple's consumer, including a
13-channel fallback at `0xffffff80016d45c6..4605`; it is deliberately rejected
instead of being reinterpreted as our 1..11 whitelist.

### Private scan MAC and mode preservation

`generateAndApplyNewPrivateMACForScans` checks feature flag `0x33` at
`0xffffff800166b406..b412` and returns success without changing the MAC when
that feature is absent. A separate firmware-disabled path also returns without
refreshing it. The decoder models only the unsupported path, via an explicit
trusted `PrivateMacPolicy::unsupported`. `unknown` and `enabled` are rejected
even when the request's refresh byte is zero. It does not invent a public
IO80211 capability bit for the Apple driver's internal feature flag, does not
claim randomized MAC support, and must not be used to override an enabled
privacy setting.

For ordinary selectors the three consumer jump tables agree: selector 1 takes
the active path (firmware scan mode 0), and selector 2 takes the passive path
(firmware passive bit 1). The parser preserves that distinction. No examined
capability branch permits transforming an ordinary active request into a
passive one. A passive-only backend must therefore reject an active decoded
request; parsing it does not justify reporting it as a completed scan.

### Timing units and bounds

Pinned public `wl_scan_params_v2` and `v3` declarations identify the active,
passive and home dwell fields consumed by the KC. Published driver constants
and their assignment paths explicitly use milliseconds for active dwell,
passive dwell, home rest and the `scan_home_away_time` IOVAR. This cross-check
supports the decoder's `Ms` names. It does not establish every legal upper
bound or special value in Apple's private request.
([Firmware structures](https://android.googlesource.com/kernel/google-modules/wlan/bcmdhd/bcm4390/+/b71905f4720059d52bca727b2f6004664850d196/include/wlioctl.h),
[Timing constants](https://android.googlesource.com/kernel/google-modules/wlan/bcmdhd/bcm4390/+/b71905f4720059d52bca727b2f6004664850d196/dhd.h),
[Active dwell assignment](https://android.googlesource.com/kernel/google-modules/wlan/bcmdhd/bcm4390/+/b71905f4720059d52bca727b2f6004664850d196/wl_cfgscan.c),
[Home timing assignment](https://android.googlesource.com/kernel/google-modules/wlan/bcmdhd/bcm4390/+/b71905f4720059d52bca727b2f6004664850d196/wl_android.c))

The parser's 1000 ms dwell and 60000 ms home bounds are deliberately local
limits, not claimed Apple ABI maxima. Both dwell values must be positive,
including the one not selected by this request. Home rest and home-away may be
zero. Home-rest zero is retained as a default sentinel rather than replaced
with an arbitrary duration. Timings are never clamped or replaced by the
backend's fixed dwell time.

The WCL manager's whole-request timeout is separate object state, not a
5456-byte payload field: `0xffffff8002133908` sets 20000, later branches may
change it, and `0xffffff8002132f1f` dispatches to the verified
`IOTimerEventSource::setTimeoutMS` virtual slot. A future driver needs its own
bounded, token-aware cancellation and completion policy; this parser cannot
promise the scan will fit the native caller's watchdog.

The later public-source cross-check resolves flag-8 semantics and timing
units left open in the earlier local `wcl-scan-admission-findings.md` audit.
Source URLs, downloaded-file hashes and relevant excerpts are recorded in
`wcl-scan-admission-primary-sources.json`; this document is the current parser
contract. No credentials were used in that research or in the tests.

## Storage, failure and future integration contract

The caller supplies stable, locally owned readable bytes and a writable live
`Request`. Arbitrary kernel or userspace pointers must be validated and copied
by the owning boundary before reaching this helper. The parser does not retain
input pointers, allocate storage, mutate policy or touch the radio.

Its single temporary is a small bounded `Request` (compile-time limit 128
bytes), not a 5456-byte wire copy or scan snapshot. Policy is passed by value.
All input reads finish before output writes, so a properly aligned live output
object may alias input storage. Every failure zeroes the entire output,
including padding. Success copies an initialized representation and clears the
temporary; unused output channel slots remain zero.

`NativeWclScanPlan.hpp` is a separate, still offline adapter from a known-subset
decoded request to the backend's new explicit ordered channel/dwell plan. It
preserves selector 1 as active and 2 as passive, uses the selected mode's dwell,
and clears its output on failure. It currently rejects nonzero home rest/away
timings because the disconnected-only backend has no associated home-channel
schedule for them. The backend independently rechecks the plan against its
actual channel policy under its gate. The adapter does not retain the WCL BSS
filter or send any result/terminal event; a future dispatcher must keep the
owned request and apply that filter. Test coverage is in
`tests/network_native_wcl_scan_plan_test.cpp`.

Before any live admission, the native dispatcher must additionally establish
runtime profile, controller/interface lifetime, current request and operation
generation, radio/regulatory policy, truthful private-MAC capability, actual
active/passive backend support, dwell/home scheduling semantics, and correct
scan completion/cancellation/error notifications. An associated scan also
needs a valid home-channel policy. These are not implemented or bypassed by
this parser, and it does not call `NativeForegroundScan` or emit native events.

The supported subset excludes directed SSIDs/BSSIDs, saved-network filters,
short SSIDs, zero/default channel lists, channel 12+, 5/6 GHz, other bandwidths,
duplicate channels, low-power/high-accuracy/low-span modes and other flags.
Those can be legitimate native requests; `unsupported*` does not claim they
are malformed. No claim is made that every menu request fits this subset.

## Validation

`tests/network_native_wcl_scan_test.cpp` uses synthetic data only. It covers
exact lengths (all 5456 short lengths plus oversized inputs), every supported
mode/BSS/private-MAC/flag combination, unsupported bits, all filter bytes,
canonical channels and duplicates, separate active/passive masks including
flag-8 attempts to bypass them, timing boundaries, unaligned byte input,
aliased output, failure clearing/canaries, 12000 deterministic mutations and
512 wholly random messages. There are 101626 always-on checks.

Verified locally with Zig 0.15.2 on Windows: the test executable passes with
strict warnings (`-Wall -Wextra -Werror -Wconversion -Wsign-conversion`) and
UBSan (`-fsanitize=undefined -fno-sanitize-recover=all`). A second build requested
`-fsanitize=address,undefined` and passed the same tests, but inspecting emitted
LLVM IR showed 138 UBSan references and zero ASan references. Therefore **ASan
has not been verified locally**; the accepted compiler option is not evidence
of address-sanitizer coverage. A genuine Clang ASan CI run remains necessary.

A standalone fixture calling the helper also compiles to an x86_64 macOS
object with `-ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector
-mno-red-zone -Wframe-larger-than=512 -Werror`. This checks compilation and the
small stack-frame bound, not kernel loadability, a live scan, or native-menu
operation. No deployment or reboot was performed for this parser.
