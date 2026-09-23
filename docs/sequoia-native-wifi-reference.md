# Sequoia native Wi-Fi menu: source reference and compatibility boundary

For macOS 15, a concrete open-source frontend reference is OpenIntelWireless's
[`AirportItlwm` Ventura-target implementation in v2.3.0](https://github.com/OpenIntelWireless/itlwm/blob/v2.3.0/AirportItlwm/AirportItlwm.cpp),
used with a rollback to the legacy IO80211/Skywalk components and post-install
root patches. A [Sequoia installation guide](https://github.com/5T33Z0/OCLP4Hackintosh/blob/main/Enable_Features/AirportItllwm_Sequoia.md)
describes that route for Intel cards. A separate [machine-specific account](https://github.com/jlempen/Surface-Go-2-OpenCore/blob/main/README.md)
explicitly uses the Ethernet-style `itlwm` path during the installer and
switches to `AirportItlwm` only after the OS and root patches are installed.
These are community reports, not a RTL8852BE hardware result.
The source comparison below is pinned to upstream tag `v2.3.0`, commit
`4ac4c79bc7e34f8764038fc382630a29eb46213d`; a separate local worktree at
`../itlwm-v2.3-reference` holds that tag. The neighboring
`../itlwm-reference` checkout is a later 2.4.0 commit and is not the pin.

A closer Realtek frontend reference is
[`AirPort_RTW88` at `f67f5d3`](https://github.com/xnoah222/Airport_RTW88/tree/f67f5d3c1873519e1b8f85e792ff60c83d10ac3f/src/kext).
Its `AirportRTW88.hpp/.cpp` implements an `IO80211Controller` on one PCI
owner, `AirportRTW88Interface.cpp` handles EAPOL through `IO80211Interface`,
and `AirportRTW88.cpp:589-674,1230-1335,1637-1671` handles Apple80211
requests, scan results and link events. Its hardware backend is Linux `rtw88`
for RTL8822BE/CE and RTL8821CE, **not** the `rtw89` RTL8852BE here; no PCI-ID
addition can substitute for the different firmware/radio/descriptor backend.
Its README claims macOS 15 operation with a restored legacy IO80211 stack, but
the pinned source tree lacks the `AirPort_RTW88.kext/Contents/Info.plist`
required by its `make airport` target, and the published HEAD
[build run](https://github.com/xnoah222/Airport_RTW88/actions/runs/35444696568)
failed. Treat its frontend source as a pattern, not as a reproducible binary or
a tested RTL8852BE driver. In particular, it immediately reports scan success
from cached results while connected and fills a constant noise value; the
Realtek port must retain its actual scan completion and typed signal evidence.
The related [Feixiao `rtw88` project](https://github.com/thegwchr/Feixiao)
documents its own `rtw88ctl`/Starskiff network selector; its hardware scan is
not evidence of Apple's Wi-Fi menu integration.

| Reference code | Concrete part to adapt | RTL8852BE work still required |
| --- | --- | --- |
| `AirportItlwm/AirportItlwm.cpp:339-434`: controller `start`, `attachInterface`, `registerService` | Native controller/interface publication for the Ventura target | Replace its Intel `fHalService`/PCI owner with one Realtek hardware owner. Do not attach a second driver to `10ec:b852` while `R16NetworkController` owns it. |
| `AirportItlwm/AirportSTAIOCTL.cpp:16-208,1327-1450`: Apple80211 dispatch and scan | OS requests and scan-result conversion | Feed the existing complete, token-bound Realtek scan cache and live link snapshots. `AirportItlwm.cpp:445-448` also has a `fakeScanDone` timer; do not copy it as evidence of a completed Realtek scan. |
| `AirportItlwm/AirportItlwmInterface.cpp:35-49`: legacy `IO80211Interface` | Ventura-target EAPOL/mbuf dispatch | Build an adapter for the selected legacy ABI; preserve the proven Realtek RX/TX ownership and shutdown drain. |
| `AirportItlwm/AirportItlwmV2.cpp:181-320` and `AirportItlwmSkywalkInterface.cpp`: Sonoma Skywalk branch | Architecture contrast only | This is a different ABI. Its private `mExpansionData` writes must not be copied into the current Darwin 24.4 driver. |
| `itlwm` Intel transport/firmware source | None | The RTL8852BE firmware, RFK, PCI interrupts and DMA stay in the present backend. |

The existing `R16NetworkController` derives from `IOEthernetController`.
Its hardware session is now a separate `MacNetworkState` with a durable
`MacNetworkStateHost` callback table rather than an Ethernet-only nested class.
The pinned net80211 stack's two link-status calls are redirected through this
host; the Ethernet host preserves its original current-medium/status calls.
`MacNetworkSession.hpp` exposes one opaque create/prepare/start/stop/poll/destroy
boundary and the same RTL8852BE boot-service factory used by the Ethernet
owner. A future native owner can use this hardware session without inheriting
from the Ethernet controller, but no native owner calls it yet.
This is a source-level ownership seam, not a native service: the state still
needs an IO80211 owner with a safe base start/stop transaction. Changing the
Info.plist class name or adding a second PCI personality will not turn `en1`
into an IO80211 station. Existing WPA2 `en1` remains the rollback build.

For the Ventura target, the minimum adaptation is controller/interface
publication, Apple80211 scan requests/results/events, association and key/link
callbacks, legacy packet ingress/egress, and one shutdown owner. For the
unpatched Darwin 24.4 Recovery target, the service must instead register an
`IO80211InfraProtocol` station, deliver WCL events and use Skywalk packets.
These are different frontend implementations over the same Realtek radio and
net80211 backend; the current repository has only offline prototypes for the
Darwin 24.4 frontend.

The legacy route is a **post-install** experiment. The current machine is in
macOS 15 recovery, where those root patches are not installed. The guide's
installer path itself uses an Ethernet-style driver; therefore it cannot
provide an immediate native menu in this recovery boot. Its OpenCore and root
patch changes also target Ventura-era ABI, so the Darwin 24.4 KC symbol/slot
audits in this repository do not certify that legacy variant. It needs a
separate pinned dependency and binary audit before loading.

The public [Sequoia PR #1054](https://github.com/OpenIntelWireless/itlwm/pull/1054/files)
and [PR #1055](https://github.com/OpenIntelWireless/itlwm/pull/1055/files)
are not complete native drivers to copy. The latter's current head contains
placeholder driver/plist content and a Sequoia service shell without native
registration. Their existence is not proof that they work on RTL8852BE.

The current Darwin 24.4 path still has the advantage that it can in principle
run in recovery without root patching, but `native-service-lifecycle-map.md`
documents missing safe base-start/stop, native registration, and packet queue
ownership. The next hardware test should exercise one complete, loadable
frontend with a rollback path, not another unregistered ABI probe.
