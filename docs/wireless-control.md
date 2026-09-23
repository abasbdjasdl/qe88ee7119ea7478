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
| 3 | 0 | 0 | Begin exclusive RX observation (`captureBegin`) |
| 4 | 0 | 0 | End this client's RX observation (`captureEnd`) |
| 5 | 0 | 2120 | Read one bounded `authevents::Event` (`captureRead`) |

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
The `probe` operation batches a real status read with unknown-selector,
empty-join, invalid-version, short-output and unexpected-scalar rejection checks.
It also checks malformed capture inputs/output sizes, read/end without ownership,
same-client repeated begin, and a second client's exclusive-access rejection.
It verifies that an unauthorized second-client end does not release the owner,
that explicit end releases ownership, and that closing an owning connection
without calling end releases ownership for the other connection. Every expected
return code is checked exactly; `NotReady`, an already occupied capture, and a
failed close are failures rather than substituted successes. The final main
connection closes on every normal error path, including failures that left it
owning the observation queue.

The probe sends no valid join/disconnect or frame TX. Successful capture reads
are checked against the event ABI, including lengths, reserved bytes, flags,
peer/session consistency, and zeroed unused payload bytes. Output includes only
non-secret status/event metadata and RPC results; it never prints event bodies,
network names, or MAC addresses. Its local event buffers are wiped after use.
The probe deliberately does not wait for new authentication traffic or cause a
reconnection, so an empty queue is a valid result. Run it as part of the next
scheduled hardware capture, not as proof from a cloud build.

## RX observation contract

The event layout is defined in `src/network/AuthenticationEvents.hpp`; the
2120-byte output fits the same bounded in-band transport. One user-client owns
the queue at a time. Begin by its existing owner is idempotent; another client's
begin returns `kIOReturnExclusiveAccess`. Read or end by a non-owner returns
`kIOReturnNotOpen`. An operational controller is required for begin/read;
closing the owner also ends its capture. Capture affects only the observation
queue and does not change the network connection.

Each event includes:

- `version=1`; `kind=0` for an empty read, `1` for authentication, `2` for
  association/reassociation response, or `3` for EAPOL.
- A queue `generation`, firmware `epoch`, station `operation`, channel, and RX
  sample time. Queue generation is separate from the connection-selection
  generation in `Status`; consumers must not mix observations across bindings.
- `own` and `peer` identify the bound station and AP. `source` is the original
  frame source: transmitter/address 2 for management frames, address 3 for
  downlink EAPOL. An EAPOL source can differ from the bound AP transmitter.
- `length` and up to 2048 body bytes. Management bodies start with the fixed
  authentication or association fields, without the 802.11 header or FCS.
  EAPOL bodies begin with the EAPOL header, without LLC/SNAP. Raw data must be
  treated as sensitive and untrusted, not written into routine/public logs.
- Flags: active binding `1`, overflow `2`, observed retry bit `4`. `dropped`
  reports records discarded when the bounded stream was invalidated by loss.

The queue holds eight records. A full queue or an oversized matching body clears
the queued transcript and latches overflow instead of silently presenting a
partial handshake. The rejected frame and discarded queued records count
toward `dropped`; it is not a continuously increasing count of every later RX
frame. An empty overflow read still reports the loss. Repeating begin for the
same owner does not clear overflow; explicit end/begin or a new protocol binding
starts a new generation. Disconnect/cancellation invalidates the binding and
clears observations. Controller disable, failure, and shutdown also release the
capture owner.

Admission is restricted to the current own address, peer transmitter, and
channel. Only unencrypted, unfragmented infrastructure authentication,
association responses, or downlink EAPOL are observed; four-address, HT-control,
and unsupported aggregate/encapsulation forms are excluded. This is a filtered
copy of RX before the normal protocol handler, **not proof of cryptographic
authentication or acceptance by net80211**. The original RX handling remains
unchanged. There is no external supplicant ownership, arbitrary frame TX, key
installation, port authorization, fresh-scan request, or native macOS Wi-Fi UI
registration in these capture RPCs. `USE_APPLE_SUPPLICANT` remains disabled.

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
The probe covers sequential client-close release, but does not establish
unprivileged inherited-port rejection, abrupt process death, concurrent
provider-stop safety, actual over-the-air event contents, forced overflow, or
disconnect/reassociation generation changes. Those require separate on-device
tests; userspace compilation and a mock transport do not establish them.
The current change is not a native Wi-Fi menu or an authentication-frame tunnel.
