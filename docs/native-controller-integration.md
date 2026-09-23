# Native Wi-Fi controller integration: existing WPA2 data path

This is a source audit and integration contract, not an implemented native
interface or evidence that the macOS Wi-Fi menu can select a network. Scope is
the existing WPA2-Personal path and current radio policy. No SAE, additional
bands, or new authentication capability is needed to make this first integration.

## Evidence profiles

The reference checkout is `../itlwm-reference`, commit
`53c51c2cdd6e4b69beb91f310d74c53422b0f8bd`. Its `AirportItlwmV2` implementation
and private headers describe a Sonoma-era architecture. They are a useful
integration example, **not a verified Sequoia C++ ABI**.

The local Sequoia 15.4.1 KC has SHA-256
`d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1`.
See [native-wcl-evidence.md](native-wcl-evidence.md) for the actual WCL producer,
consumer, dispatch and message-length evidence. The existing
`tools/build_native_contract.py` uses `__IO80211_TARGET=140400`; targeting
`x86_64-apple-macos15.0` does not make those private declarations Sequoia-compatible.

## Reuse the data path; add the native control/interface graph

The pinned reference uses this graph:

```text
IOPCIDevice
  AirportItlwm : IO80211Controller
    AirportItlwmSkywalkInterface : IO80211InfraProtocol   native control plane
    AirportItlwmEthernetInterface : IOEthernetInterface  BSD mbuf data plane
```

Sources: `AirportItlwm/AirportItlwmV2.hpp`, `AirportItlwmSkywalkInterface.hpp`,
`AirportItlwmEthernetInterface.hpp`, and `AirportItlwmV2.cpp::start/createInterface`.
`include/Airport/IO80211Controller.h` itself derives from `IOEthernetController`.
The native interface is attached to both its provider and IO80211Controller;
the BSD interface is created through `IONetworkController::attachInterface`.
`AirportItlwmEthernetInterface::attachToDataLinkLayer` passes the real BSD ifnet
to `prepareBSDInterface` and synchronizes the native interface name/unit/MAC.

The RTL driver currently has only `R16NetworkController : IOEthernetController`
and one `IOEthernetInterface`. The minimal structural change is a separately
selected native build/profile with an IO80211Controller-derived controller,
native infrastructure interface and BSD bridge, retaining the existing State,
boot service, net80211 owner, PCI owner and DMA queues. Update the metaclass,
base start/stop/free/configure calls, createInterface, native enable/disable
overloads, work queue and media publication together. Adding another independent
PCI-matching controller beside the existing owner is not this design.

| Integration point | Existing implementation to preserve | Necessary native connection |
| --- | --- | --- |
| RX | `MacNetworkController.cpp::State::deliver` feeds `deliverRealtekRx`; net80211 decrypts/decapsulates into mbufs | Keep `ic.ic_if.iface` pointing at the **BSD IOEthernetInterface-compatible bridge**. Pinned `_mbuf.cpp::_if_input` calls its `inputPacket` and `flushInputQueue`. Never assign an IO80211InfraProtocol pointer there. |
| TX | `outputPacket` -> `outputGated` -> `if_snd.queue->lockEnqueue` -> `pumpTx` | Route the native service's BSD output to this same entry point. Preserve one mbuf owner, free-on-drop semantics and `Traffic::authorized` gating. No second raw-802.11/Skywalk data path is required by the reference design. |
| Protocol owner | `attachProtocol`, one net80211 runtime/gate, `MacNetworkBootService` | Keep one owner and existing hardware-completion callbacks. Native requests translate into bounded commands to that owner. |
| Link state | `State::poll` authorizes after hardware association, RUN and `ni_port_valid`; `setLinkStatus` also checks authorized traffic | Publish BSD and native link changes from a common transition helper, with generation and retained event data. Notify native link/running state and SSID/BSSID changes only through the verified target ABI. |
| Join | `selectWirelessNetwork` -> `queueSelection` -> disconnect/drain -> `applyPendingSelection` -> `ieee80211_add_ess` | Decode the actual target request into `selection::Join`; return queued status, then emit real association/failure/authorized transitions. Preserve old-key/drain ordering. |
| Disconnect/power | `disconnectWirelessNetwork`, `enableGated`, shutdown fencing | Route native disconnect and power changes to these state transitions; cancel pending joins/scans, invalidate callback generations and publish terminal state. |
| Status | `copyWirelessStatus`, `NativeWirelessData.hpp` | Serve one consistent gated snapshot for each request. Extend only with measured/observed fields; advertise only the existing supported policy. |

The reference's BSD bridge `getProvider()` override is explicitly described
upstream as a hack, and its startup writes directly into two private
`mExpansionData` registration pointers. These details cannot be transplanted
without proving the matching Sequoia layout and lifetime rules. The same applies
to the reference's `reportLinkStatus(3, 0x80)`/`(1, 0)` constants.

## Link groundwork now present; native notifications still missing

All `MacNetworkController.cpp` State, startup and enable/disable link updates now
use virtual `setLinkStatus`; the only qualified IOEthernetController call is in
`applyLinkStatus`. It preserves the existing authorized-traffic guard and records
an observation only after the base status update succeeds. Runtime updates from
outside the hardware gate use the existing `controlLock_ -> gate_` fence; internal
updates, including shutdown, never reacquire `controlLock_` from the gate.

`copyLinkPublication(MacLinkPublication&)` is the pointer/key-free observation
path for a future native frontend running on its own queue. The copy contains
revision, hardware epoch, selection generation, last-change monotonic timestamp
and actual applied status bits. Duplicate status/epoch/selection tuples do not
advance the revision; a failed base update does not publish requested state and
makes snapshot reads fail until a later successful update.
Revision zero means no publication yet. The counter skips zero on wrap; compare
revisions for inequality, not a perpetual numerical ordering. The copy is gated,
cleared on failure, and rejected after the external stop fence closes. The
frontend must invalidate its own state and remembered revision on a failed read
or stop instead of retaining a prior up snapshot. This latest-state view is not a lossless stream of transient association
events, nor an IO80211 notification. No observer function is invoked under the
hardware gate. A derived setLinkStatus override must chain to the base and defer
framework notifications; it must not call the external snapshot API from the gate.

The controller now installs `ic_newstate` and chains/restores `ic_event_handler`.
The latter records complete foreground scan passes; association/deauthentication
notifications have not yet been connected to a native frontend. The pinned
net80211 implementation emits:

| Source event | Source location in the pinned tree | Meaning for the adapter |
| --- | --- | --- |
| `IEEE80211_EVT_STA_ASSOC_DONE` | `itl80211/openbsd/net80211/ieee80211_input.c`, association response handler | Association response processed; not WPA2 key completion or proof of IP connectivity. |
| `IEEE80211_EVT_STA_DEAUTH` | Same file, deauthentication handler | Link loss with a bounded copy of reason/current generation; never retain the node pointer. |
| `IEEE80211_EVT_SCAN_DONE` | `ieee80211_node.c::ieee80211_end_scan` | End of a net80211 channel scan; distinct from one hardware dwell finishing. |

The passive scan observer carries a physical hardware epoch, its own pass
generation and the exact StationController dwell token. Association event payloads
must carry the hardware incarnation and selection/scan generation, copied under
the hardware gate; drop stale completions after disconnect, cancel, stop or a new
request. Association timeouts and hardware failures also need a terminal native
result because they are not guaranteed to produce a successful assoc-done event.
WPA2 data link-up still comes from controlled-port authorization, never from
`queueSelection` success or the earlier association event.

## Foreground scanning and remaining native request work

The legacy startup poll begins a scan only when `enabled && credentials`.
`beginNativeForegroundScan`, `copyNativeForegroundScanStatus` and
`cancelNativeForegroundScan` now provide an independent, kernel-only passive
operation without provisioned credentials. Admission requires an enabled, idle,
unconnected station in net80211 INIT, no legacy credentials/AUTO_JOIN, pending
join or deferred work. Connected requests return Busy without disconnecting;
active requests currently return Unsupported. No native framework callback
calls these APIs yet, so this is not menu scanning.

The operation snapshots actual boot policy intersected with currently allowed
net80211 channels, starts one real station scan per channel and waits for its
restore/end completion. The completion callback only records the result;
the next poll starts another channel after StationController has cleared the
old token. A distinct hardware epoch/request counter binds status/cancellation,
and exact dwell tokens bind RX and channel completion. Only a full observer
publication supplies a completed snapshot token. Cancellation/selection/power-off
and a two-minute request deadline first enter draining; cancellation acceptance
does not claim hardware drain. Hardware failure requires the existing proven
shutdown path, preserving the prior complete scan cache.

`State::scanFinished` sets `scanDone` after one StationController channel visit;
poll then calls `ieee80211_next_scan`. It now marks only that channel complete
in the observer. `IEEE80211_EVT_SCAN_DONE` publishes a stable cache only after
every planned channel has completed. This is one net80211 mode/active-or-passive
pass, not yet a framework scan request completion. The pinned AirportItlwm `fakeScanDone` timer
posts after 100 ms regardless of RF completion; it is not an appropriate completion
source for this driver.

There is also a connected-state constraint: current `newState(S_SCAN)` disconnects
when the station is not idle. Wiring a native refresh directly to
`ieee80211_begin_cache_bgscan` would not establish nondisruptive background scans.
The first adapter must explicitly distinguish a disconnected foreground scan
from a connected request. Until a real background channel/restore operation is
implemented, preserve the current connection and return the target framework's
verified busy/cached-result behavior; do not report an unperformed fresh scan.

`WirelessStatus::Network` retains only a collapsed node view. A separate
`NativeScanObservation.hpp` observer now copies real beacon/probe responses before
net80211 delivery: complete bounded IEs, beacon interval/capabilities, binary
SSID/BSSID, tuned channel, host monotonic observation time and an explicitly
typed signal. The RTL backend preserves actual PHY dBm; the sample is the most
recent valid PHY report, not proven exact per-MPDU attribution. No percentage
conversion or fabricated noise measurement is used.

The ~300 KiB double-bank observer is heap-backed and optional on allocation
failure. `copyNativeScanSummary/Entry/Channel` use the external control-lock/gate
fence and exact epoch/generation/index checks. Cancellation retains the last
complete snapshot; overflow/truncation or an incomplete channel plan cannot
publish a new complete result. Entries are copied to caller-owned storage,
without retaining node pointers. The observer serves both legacy scans and the
new independent passive scheduler. It deliberately excludes background scans.
The unattended recovery collector also records count-only `R16NativeScan*`
properties (observer availability, open pass and last complete token/count/channel
count). Complete means a retained full pass exists, not a live WCL request
completed. These diagnostics contain no SSID/BSSID or raw IE data.

The foreground scheduler's tests exercise the real StationController and Observer
with simulated hardware completions, including stale tokens, all scan phases,
cancel/timeout/failure, no probes or protocol autojoin, and old-cache retention.
They are not real-radio or native-menu tests. `NativeScanProbe.hpp` separately
encodes a wildcard SSID and existing legacy rate IEs for a future active path;
it does not enqueue/transmit a frame. Reusing `ieee80211_send_mgmt(PROBE_REQ)`
blindly would arm net80211's management timer, while its stock probe body reads
`ic_des_essid`, which may still contain an earlier selection. Active scans need
an independently owned node/frame and verified TX ownership without those effects.

Exact-target WCL inspection establishes selector 1 as Active and 2 as Passive;
capabilities do not rewrite ordinary selector 1 to Passive. The +4 private-MAC
trigger has a legitimate no-op when the driver explicitly lacks that capability
or the feature is disabled, but it is not an arbitrary flag to ignore. Normal
non-ID requests also acquire flag 0x08; remaining policy/dwell fields still need
admission semantics. Do not connect a default active request to the passive API
and report success, or interpret payload timing values as the WCL watchdog.

`NativeWclBeacon.hpp` can produce an offline 64-byte metadata plus raw-IE draft
from one such entry for the pinned KC. It revalidates bounds, names, channels
and signal units; its `emissionReady` remains false. Real interface registration,
request binding and notification lifetime are still missing. See
[native-wcl-notifications.md](native-wcl-notifications.md).

## Join credentials, dispatch and gate contract

`NativeWirelessRequests.hpp` currently decodes the pinned legacy
`apple80211_assoc_data`: infra/open lower auth, bounded SSID/IE and an explicit
32-byte PMK for WPA2-Personal. It wipes temporary credentials and queues the
existing selection operation. This is reusable validation **after** the real
framework request has been identified and copied into owned kernel memory.

`NativeWirelessDispatch.hpp` currently supports GET SSID/BSSID/CHANNEL/RSSI and
SET ASSOCIATE/DISASSOCIATE only. It does not register any IO80211 service, handle
scan requests, report power/capabilities/state, or establish the Sequoia menu's
credential route. Unsupported operations must remain unsupported rather than
returning success from empty methods. Required capability, power, interface,
current-network and scan operations should be added according to verified
framework callers and conservative existing-driver capability values.

The KC evidence proves a WCL candidate message of 988 bytes for command `0x1ba`
and a scan message of 5456 bytes for `0x1b9`; it does **not** prove the legacy
association struct is the Sequoia menu's request. `NativeWclCandidates.hpp` is
observation-only and deliberately cannot supply an SSID or credential. The actual
SSID/PMK request delivery and completion identity must be resolved before a menu
selection can invoke `selectWirelessNetwork` correctly. If the framework supplies
a passphrase rather than PMK, use a bounded, wiped, non-gate-blocking derivation
path with a verified request lifetime; do not reinterpret bytes as a PMK.

`NativeWclJoin.hpp` now decodes a narrow, target-pinned 988-byte WPA2-PSK/CCMP
candidate with an embedded raw PMK. It rejects unsupported policy and authentication
instead of lowering security. This is credential extraction only; it is not yet
called by a registered WCL interface. Separate CIPHER_KEY delivery, callback
operation identity, real queue admission and terminal notifications still need
to be wired together. See [native-auth-status.md](native-auth-status.md).

The existing WPA2 net80211 supplicant/key installation remains the handshake
owner in this first integration. Do not also enable `USE_APPLE_SUPPLICANT` or
feed/install the same EAPOL keys twice. Native UI control does not by itself
require replacing the working WPA2 key exchange.

`runControlAction` locks `controlLock_` then the hardware command gate and
explicitly rejects callers already inside `loop_->inGate()`. Consequently native
callbacks cannot blindly call `selectWirelessNetwork` or `copyWirelessStatus`
while holding that gate. Either use a separately fenced frontend queue entering
the existing external methods, or add explicit internal gate-owned methods whose
callers already satisfy stop/lifetime protection. Do not acquire `controlLock_`
from the gate and invert the shutdown lock order. Copy event/status data under
the gate and send framework notifications outside it unless the target ABI
expressly requires a compatible, proven gate context.

## Minimal implementation sequence and proof required

1. Verify the Sequoia controller/infra/bridge constructors, class sizes and virtual
   slots; work queue, registration, attach/detach and request argument signatures;
   WCL scan/join credential route; and notification payload/ownership semantics.
   A Sonoma header-only compile or a found symbol is insufficient for these.
2. Add the native controller/interface/bridge build profile and required bundle
   dependencies/personality, while preserving the existing hardware/backend owner.
   Test unloaded construction/dispatch contracts before any real load.
3. Add the bounded foreground-scan operation, stable scan results, real completion
   events and conservative power/capability/status responses. Add the framework
   join adapter to the existing drain-and-select path and unify link publication.
4. Preserve startup rollback, stop request draining and DMA-retention safeguards.
   Stop/cancel native callbacks before detaching the interfaces; keep the objects
   alive if existing hardware shutdown cannot prove DMA idle. Native notifications
   must not dereference State or a BSD interface after teardown.
5. Test an actual native IORegistry service graph, initial menu scan without
   `R16SSID`, selection of an existing WPA2 network, correct/wrong passwords,
   switch between two networks, disconnect/reconnect, power off/on and stop during
   scan/join. Confirm link is down before key completion, then DHCP and bidirectional
   network traffic after authorization. An icon alone proves none of those paths.

Existing `network_wireless_selection_test.cpp`, `network_wireless_status_test.cpp`,
`network_native_adapter_test.cpp` and `network_native_wcl_candidates_test.cpp`
cover pieces of bounded data/selection translation. `build_native_contract.py`
compiles private-header declarations and runs offline byte-view tests. None tests
native service registration, Sequoia virtual dispatch, real scan completion,
framework credential delivery or menu-driven association. Add focused adapter
tests for stale generations, cancellation, exact terminal callbacks, real scan
vs cached views, link authorization and stop/reentrant-request races; report those
separately from the hardware/menu validation.
