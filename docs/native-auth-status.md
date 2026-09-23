# Native Wi-Fi and authentication status — 2026-09-23

This is unfinished integration. The installed 120aae9 controller demonstrated
basic Internet connectivity on one WPA2-Personal/CCMP network. It presents an
Ethernet interface; the new components below do not make the macOS Wi-Fi menu
available. No new package was installed or reboot armed for this work.

## Source changes and evidence

- `WirelessUserClient` provides an administrator-only IOKit control connection
  (`0x52313601`) to the actual controller. It rechecks caller privilege for every
  operation and accepts only fixed-size pointer-free v1 status/join messages.
  Join and disconnect call the existing gated selection backend. Public control
  calls drain before controller teardown; retained stopped providers reject them.
  `r16-wireless-control` is the userspace endpoint (binary join input via stdin,
  never passwords or PMKs in argv/logs). This is a real driver control path in
  source, but it is not yet hardware validated or an Apple Wi-Fi registration.
  The new exclusive RX observer exposes bounded, session-tagged unencrypted
  authentication/association/EAPOL copies to an administrator. It preserves the
  existing protocol owner; it does not add TX, key installation, or a daemon.
  Overflow invalidates the transcript, disconnect invalidates its generation,
  and client close/interface shutdown releases ownership. Tests cover late
  readers during association hardware installation and obsolete tokens.

- Native Apple association requests: open, WPA-Personal and WPA2-Personal with
  explicitly selected CCMP/TKIP, binary SSIDs and optional BSSID. Protected
  requests require a 32-byte PMK. Unsupported AKMs fail without downgrading.
- `NativeWirelessDispatch.hpp` routes bounded kernel-owned requests to the
  selection/disconnection backend and a single gated wireless snapshot per
  status reply. It does not register a framework interface or accept userspace
  pointers. Real-header adapter tests passed in run 35797077262.
- `SaeSession` uses hostap's group-19 SAE H2E and hunting-and-pecking. Corrected
  an erroneous ordinary-SAE AKM selector requirement. Exchange, wrong password,
  modified confirmation and export-before-confirm rejection passed in macOS
  sanitizer run 35796749250. The wrapper deliberately rejects unhandled commit
  extensions. `SaeExchange` now supplies authentication-body framing, source
  checks, bounded retries, monotonic deadlines and cancellation for both methods.
  Confirm retries increment the counter and recompute the authenticator, including
  the case where an AP has accepted but its response was lost. Anti-clogging
  tokens and actual management TX/state-machine ownership remain pending.
- `OweSession` uses hostap's ECDH/hash primitives and RFC 8110 key ordering and
  derivation. Group-19 peer agreement, PMKID, invalid group/length/point/reflection
  rejection passed with SAE in macOS ASan/UBSan run 35796953546. Derived keys
  are not an authenticated data port or proof of a completed four-way handshake.
- Both userspace components use hostap commit
  `24c033de87759e3f8818507a60d873899658a7cf`, with key debugging disabled. They
  are testable authentication components, not firmware and not installed daemons.

## Work still required before claiming the requested functionality

1. Finish the actual macOS 15 IO80211/WCL interface registration, lifecycle,
   scan/result delivery, change notifications, and dispatch bindings. The old
   pinned Sonoma header layout does not match the recovery kernel's vtables;
   compiled adapter declarations alone do not prove compatibility.
2. Implement authenticated daemon/driver transport, management/EAPOL frame
   routing, cancellation/timeouts, RSN AKM negotiation and key installation.
   SAE/OWE PMKs must never be passed off as WPA2-PSK keys to bypass missing AKMs.
3. Complete mandatory PMF and four-way-handshake support for the added modes,
   then test with real APs. No WPA3 or OWE hardware success is claimed.
4. Integrate enterprise EAP/TLS/TTLS/PEAP and certificate verification/credential
   handling. These modes are not implemented by the new SAE/OWE components.
5. Implement and validate 5 GHz radio/channel/calibration policy and broader
   bandwidth. Current hardware policy remains 2.4 GHz channels 1–11, 20 MHz.

There is no defensible overall completion percentage. Offline peer tests do
not substitute for interoperability, kernel ABI or hardware validation.

References: https://git.w1.fi/hostap.git and
https://www.rfc-editor.org/rfc/rfc8110.html . CI evidence lives under
https://github.com/abasbdjasdl/qe88ee7119ea7478/actions .
