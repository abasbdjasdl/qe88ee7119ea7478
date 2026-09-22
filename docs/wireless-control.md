# Local wireless control v1

The exact ABI is `src/network/WirelessControl.hpp`. The x86_64 driver exposes
IOServiceOpen type `0x52313601` on `R16NetworkController`. Both opening and each
external method require administrator privilege; forwarding a connection port
to an unprivileged task does not grant access. Ordinary network-family user
client types continue through the superclass implementation.

Methods are synchronous and accept no scalar arguments, async references or
memory descriptors. IOKit structure buffers contain fixed-size local values:

| Selector | Input bytes | Output bytes | Meaning |
|---|---:|---:|---|
| 0 | 0 | 3936 | Read one gated snapshot and up to 60 cached observations |
| 1 | 104 | 0 | Queue a validated open/WPA/WPA2 personal join |
| 2 | 0 | 0 | Queue disconnection and clear pending selection |

Successful selector 1 means accepted by the queue, **not connected**. Read
selection generation, pending flag and link status to observe subsequent work.
Status flags are current-valid=1, selection-pending=2, scan-in-progress=4,
cache-truncated=8. Cached entries are not proof of a newly requested scan. No
fresh-scan operation is exposed yet. Status contains no PMK or stack pointers.
The 3936-byte output remains within IOKit's in-band message size; exceeding it
would make IOKitLib pass a memory descriptor, which this endpoint rejects.

Join security values are 0=open, 1=WPA2-PSK, 2=WPA-PSK. Cipher values are 0=CCMP,
1=TKIP. Protected requests require exactly 32 PMK bytes; derivation from a
password belongs to the trusted caller. SAE/OWE/EAP requests are rejected, not
relabelled as PSK. Reserved fields must be zero. SSID is binary, length 1–32.

`tools/wireless_control.cpp` builds alongside the kext. It offers status, join
and disconnect for an automated test/controller process. Join reads the exact
binary request from stdin, which avoids secrets in command-line arguments.
Never dump that request or redirect it into CI/public artifacts. It intentionally
does not print network names or key material in status diagnostics.

Concurrency: user-client calls serialize on the client lock, then the provider
control lock, then the hardware command gate. The provider rejects public
control calls made while already holding its hardware gate, preventing lock
order reversal. Teardown drains/blocks control calls before releasing hardware
state. Client close/stop blocks new calls; the provider reference survives until
client free. This needs on-device lifecycle testing as well as compile checks.

Future batched hardware check: verify a non-admin open is rejected; verify root
status succeeds and matches the capture; malformed selectors/lengths must not
mutate network state; reconnect to the existing profile and verify generation,
association, DHCP and target-interface HTTPS; test client close/provider stop.
The current change is not a native Wi-Fi menu or an authentication-frame tunnel.
