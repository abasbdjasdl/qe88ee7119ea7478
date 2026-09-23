# Authentication transport and integration boundaries

This is a source audit of the pinned protocol stack, not a claim of SAE, OWE,
enterprise authentication, or native macOS Wi-Fi operation. The current work adds
an **observation-only RX queue**. Observing a frame does not consume it, authorize
a port, install a key, or transfer protocol ownership to an external supplicant.
The existing net80211 state machine remains the protocol owner. Do not enable
`USE_APPLE_SUPPLICANT` as part of this observation work.

## Reproducible sources

- OpenIntelWireless/itlwm: `53c51c2cdd6e4b69beb91f310d74c53422b0f8bd`.
  In this document, `net80211/` means the upstream directory
  `itl80211/openbsd/net80211/` at that revision.
- Official hostap: `24c033de87759e3f8818507a60d873899658a7cf` from
  `https://git.w1.fi/hostap.git`. `hostap/src/` refers to that revision.
- Local references use paths relative to this repository. Upstream line numbers
  below refer to the fixed revisions, before build-time source overrides.
  Local function names are the stable reference when later edits move lines.

The existing build compiles the pinned net80211 sources with
`IEEE80211_STA_ONLY`. `tools/build_network_stack.py` does not define
`USE_APPLE_SUPPLICANT` or `IO80211FAMILY_V2`.

## Where authentication is currently handled

| Path | Verified behavior in the pinned source |
|---|---|
| Authentication RX | `net80211/ieee80211_input.c:2241`, `ieee80211_recv_auth`: line 2263 rejects every authentication algorithm other than OPEN. SAE authentication frames cannot reach the local SAE component through this function. |
| Authentication TX | `net80211/ieee80211_output.c:1497`, `ieee80211_get_auth`: line 1511 writes OPEN unconditionally and allocates only the six fixed authentication bytes. |
| EAPOL RX | `net80211/ieee80211_input.c:1065`, `ieee80211_enqueue_data`: lines 1121–1143 route RSN EAPOL to the built-in PAE in the current build. `net80211/ieee80211_pae_input.c:90`, `ieee80211_eapol_key_input`, discards non-Key EAPOL at line 118. This loses the EAP exchange needed for enterprise authentication. |
| Association response | `net80211/ieee80211_input.c:2633`, `ieee80211_recv_assoc_resp`, parses supported rate/capability elements and advances toward RUN at line 2845. It does not implement OWE DH-response validation or wait for an OWE PMK. |
| Existing driver state gate | `src/network/MacNetworkController.cpp`, `State::newState`, accepts AUTH-to-ASSOC only from the current receive path with the expected AUTH subtype and station token. A future userspace completion cannot legitimately bypass that condition by setting `receivingProtocol`. |

The observation queue must copy bounded frame data while the original packet is
valid and then leave the original state-machine path intact. It must never turn
an unsupported authentication response into a successful association.

## Management-frame and EAPOL integration points

### Management RX

The stack exposes `ic_recv_mgmt` in `net80211/ieee80211_var.h:406` and initializes
it in `net80211/ieee80211_proto.c:118`. The input path calls it at
`net80211/ieee80211_input.c:684`, after the applicable direction/state and
management-protection processing. Line 685 frees the mbuf immediately afterward.

A wrapper can save the original function, copy a bounded observation, then call
the original function. Neither the mbuf nor its node pointer may escape into the
userspace ABI. A future external-authentication mode would need a separate,
explicit ownership decision restricted to its active BSSID, state, and session;
the current observation path makes no such decision.

### Management TX

`ic_send_mgmt` exists at `net80211/ieee80211_var.h:409`. A future explicit SAE
mode can replace only the OPEN authentication transaction and use
`net80211/ieee80211_output.c:192`, `ieee80211_mgmt_output`, to build addresses,
sequence numbers, and the queued 802.11 header around a validated body.

That API requires a node reference that the driver will eventually release.
Allocation, enqueue, and completion failures must follow the ownership rules of
the existing `ieee80211_send_mgmt` caller. The current work does not add arbitrary
raw-frame injection or call this a working SAE transport.

### EAPOL ownership and TX

There is no existing per-controller EAPOL callback at the current receive
decision. A narrowly scoped source override in `ieee80211_enqueue_data` can add
an observation callback without changing mbuf ownership or the original PAE
dispatch. Any future consuming hook must choose exactly one protocol owner per
frame; running two independent four-way state machines on duplicated frames is
not a valid integration.

Do not globally enable `USE_APPLE_SUPPLICANT` to obtain this callback. Besides
redirecting EAPOL, that macro changes `net80211/ieee80211_proto.c:1684` so the RUN
transition reports link-up without the current RSN port-valid check. That is a
behavioral change outside the observation-only work.

A future privileged EAPOL TX endpoint can construct an Ethernet EAPOL packet
and use the existing `if_snd` queue under the command gate. The generic
encapsulation path at `net80211/ieee80211_output.c:614–620` permits EAPOL while
the protected data port is closed. It must not depend on the ordinary Ethernet
interface already reporting link-up. Destination/source addresses, EtherType,
EAPOL length, association/session identity, and the maximum packet size require
validation before admission.

## SAE and OWE cannot be represented as PSK

The local `src/auth/SaeSession.*` and `src/auth/OweSession.*` implement userspace
cryptographic components. Their exported PMKs are inputs to later authenticated
state transitions, not evidence that a radio connection or four-way handshake
succeeded.

The pinned stack needs changes beyond feeding those PMKs to a PSK request:

- `net80211/ieee80211_crypto.h:57–63` defines only 802.1X, PSK, and their SHA-256
  variants. `ieee80211_parse_rsn_akm` at `ieee80211_input.c:1446` ignores SAE
  selector 8 and OWE selector 18. RSN construction at
  `ieee80211_output.c:1052–1074` cannot emit those AKMs.
- `ieee80211_choose_rsnparams` at `net80211/ieee80211_node.c:1578` assumes that
  a non-PSK choice is 802.1X in lines 1592–1605. Adding an enum without updating
  selection would silently choose the wrong authentication protocol.
- `ieee80211_recv_4way_msg1` at
  `net80211/ieee80211_pae_input.c:202` retrieves a PMKSA only for 802.1X; every
  other case copies `ic_psk` at lines 258–269. A new AKM needs an explicit PMK
  source and its proper PMKID semantics.
- The current PAE rejects descriptor version 0 at
  `net80211/ieee80211_pae_input.c:143` and chooses only versions 1, 2, or 3 at
  `net80211/ieee80211_pae_output.c:101–109`. Official hostap checks the
  AKM-defined descriptor version 0 at `hostap/src/rsn_supp/wpa.c:4161` and
  handles its MIC rules at `hostap/src/common/wpa_common.c:403`. SAE uses an
  AES-CMAC branch; OWE has a separate AKM/PMK-length-aware branch. Their PTK
  derivation must also match the negotiated AKM.

A reliable implementation must either extend all of these net80211 paths or
make the mature userspace supplicant own authentication and the four-way
handshake, with explicit key-installation and port-authorization APIs. Merely
changing the display label, copying a derived PMK to `ic_psk`, or advertising
SAE/OWE while transmitting a PSK RSN IE is not support.

OWE additionally needs its DH element in the association request, validation of
the peer DH element in the association response, and sequencing that prevents
the handshake from using a PMK before that response has been accepted. An
external SAE completion needs a dedicated gated state transition tied to the
current authentication transaction, not a synthetic OPEN response.

## Key installation and PMF

The default `ic_set_key` is `ieee80211_set_key` at
`net80211/ieee80211_crypto.c:152`. It supports software TKIP, CCMP, and BIP and
sets `IEEE80211_KEY_SWCRYPTO` on success. The local
`src/network/Net80211PacketBridge.cpp::protectFrame` reuses those software
ciphers; RX deliberately does not assert hardware decryption/replay validation.

Useful existing protections must be preserved:

- `net80211/ieee80211_pae_input.c:556–597` installs a pairwise key only while
  `IEEE80211_NODE_RSN_NEW_PTK` is set, preventing repeated message 3 from
  resetting its packet-number state.
- `ieee80211_must_update_group_key` at
  `net80211/ieee80211_pae_input.c:367` guards GTK/IGTK reinstallation.
- `net80211/ieee80211_pae_input.c:641–677` validates and installs IGTK IDs 4/5.
  Port authorization occurs separately after successful key processing at
  lines 681–697.
- `net80211/ieee80211_crypto_bip.c:189–232` checks the management IPN, validates
  the MIC, and updates replay state only after successful verification.
- `net80211/ieee80211_crypto_ccmp.c:342–348` uses a distinct management-frame
  receive sequence counter. The cipher constructs management-frame-specific
  nonce data at lines 139–142.

PMF is not complete merely because these algorithms compile. The current
controller's `State::attachProtocol` declares `IEEE80211_C_RSN`, not
`IEEE80211_C_MFP`. More importantly, in the audited upstream and local sources,
`IEEE80211_NODE_RXMGMTPROT` and `IEEE80211_NODE_TXMGMTPROT` are only defined
(`net80211/ieee80211_node.h:475–476`) and read
(`ieee80211_input.c:649`, `ieee80211_output.c:239`); no code sets them.

Before enabling PMF, connect validated negotiation and PTK/IGTK installation to
the required RX/TX protection state, and clear that state at the proper
disconnect/rekey boundaries. Test unicast protected management frames,
multicast BIP frames, wrong MICs, replayed packet numbers, missing IGTK, and
unprotected robust management frames after protection is active. The existing
STA SA Query responder (`ieee80211_input.c:3343`) is a useful component, not
proof of complete PMF behavior. WPA3 support must not be advertised before its
mandatory PMF behavior and negotiated cipher/AKM combination are validated.

## Enterprise integration options

A smaller first enterprise path can leave the existing WPA2 four-way handshake
in the kernel and route only the EAP exchange to a userspace supplicant. The
supplicant must perform the selected EAP method, certificate/identity checks,
and key derivation before a session-bound PMK handoff. EAP success alone is not
a request to mark the data port valid.

`ieee80211_pmksa_add` at `net80211/ieee80211_crypto.c:644` accepts an explicit
AKM, and `ieee80211_recv_4way_msg1` at `ieee80211_pae_input.c:258` can retrieve
it. However, `SIOCS80211KEYAVAIL` at `ieee80211_ioctl.c:682–687` hardcodes
`IEEE80211_AKM_8021X`. Do not expose that ioctl unchanged as a general enterprise
key API. A gated endpoint must validate the actual AKM, BSSID, current session,
key length, lifecycle, and permissions. The PMKSA lifetime is explicitly unused
in the upstream implementation (`ieee80211_crypto.c:664`), so expiry and stale
session behavior also need an explicit policy.

WPA3-Enterprise, its optional 192-bit suites, and every possible EAP method are
separate capabilities; none follows from implementing one WPA2 EAP path.

## Lifecycle and evidence required before activation

Future authentication control operations need firmware epoch, connection
generation, and peer identity checks under the existing hardware command gate.
Disconnect, controller stop, client exit, timeout, and queue overflow must not
allow a stale message to install keys or authorize a later connection. Bounded
queues must report loss explicitly; raw credentials and key material must not
enter diagnostics or observation reports.

The next evidence should distinguish: observation delivery; authenticated
management-frame exchange; association acceptance; four-way completion; key
installation; protected management traffic; data-port authorization; and actual
IP connectivity. Offline SAE/OWE tests or an RX observation alone establish none
of the subsequent radio stages. The current work does not change radio-band
policy, claim a native IO80211 interface, or deploy a new boot profile.
