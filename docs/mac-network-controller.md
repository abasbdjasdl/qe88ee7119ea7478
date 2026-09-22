# Native network controller

`R16NetworkController` is a real `IOEthernetController` owner. It creates a workloop,
command gate, timer, MSI source, six TX banks, CH12, RXQ/RPQ, the immutable-epoch
firmware command dispatcher and net80211 protocol state. DMA allocation takes
place outside the gate. Register programming, interrupt service, protocol timers,
ACK delivery and station transitions share that gate.

It is not yet a separately loadable driver personality. Its default factory
returns null and `start()` fails with `R16Failure=concrete boot service missing`.
The hardware personality must override `createBootService()` with the real
`MacNetworkBootService`. No method has a successful placeholder implementation.

## Hardware contract

1. `allocate(device, BAR2, loop)` runs outside the gate and prepares download DMA.
2. `prepare(identity)` runs inside it, performs the actual power/MAC/firmware
   sequence, provides the real MAC/epoch/regulatory channels, and leaves firmware
   running with bus master off, IRQs masked, host DMA idle and runtime rings zero.
3. The controller publishes nine real 64-entry banks, starts DMA/MSI, then calls
   `start(commands,sink)` so RFK/BT setup uses the same real CH12/C2H command bus.
4. `ready()` must remain false until that setup actually finishes. Only then does
   the controller begin the StationController role/no-link/CAM sequence.
5. `beginAction` performs the register/channel/firmware operations in
   `station-controller.md`; completion must retain the original token and be
   delivered later. Sink completions are queued before touching StationController.
   Missing action support is a failure, never a synthesized success.
6. `txInfo` supplies hardware policy for a real encrypted/raw net80211 lease.
   `rxInfo` supplies the actual channel and PHY metadata. C2H delivery does not
   depend on data-frame metadata availability. Unknown harmless notifications
   should be ignored with result zero; a nonzero C2H callback faults the runtime.
7. `stop` cancels backend callbacks, shuts down CPU/RF and proves its own resources
   releasable. Failure retains the owner, PCI provider, BAR, queues, protocol and
   bus together. Runtime DMA is never freed based only on a logical stop request.

## Protocol path

Only legacy 20 MHz 802.11a/b/g, open authentication and WPA2-PSK/CCMP software
crypto are advertised. HT/VHT/HE, hardware keys, aggregation and background roaming
are not advertised. PHY/RF support must not be inferred from the protocol stack.

The net80211 scan selector drives one bounded StationController channel visit at
a time. A real tune completion permits the probe request. The controller advances
to the next channel only after the visit and home-channel restoration finish.
Actual received AP authentication triggers ASSOC; actual received association/AID
triggers station CMAC/Join/CAM programming. These two transitions require the real
RX delivery context and matching management subtype. Only after DONE completion
is protocol RUN entered. `ni_port_valid` after software EAPOL/key installation
opens the Ethernet link for RSN; an open association uses its actual RUN evidence.
Join ACK alone never sets the Ethernet link active.

Ethernet output enters net80211 encapsulation/software encryption, actual TX DMA
and the two-condition TXBD/RPQ completion ledger. RX goes through Realtek RX
assembly, net80211 validation/decryption and the attached Ethernet interface.
DHCP/IP remain in macOS; no fake lease or IP address is supplied by the kext.

Hardware actions first stop new host TX admission, then wait for the actual
TXBD/RPQ ownership ledger to drain before pausing the old-channel scheduler or
changing channel/port/CAM state. The existing station action deadline bounds
that wait as well as the action (12 seconds total, including a full radio tune
bounded to 10 seconds; standalone firmware ACKs remain 2 seconds). Net80211 node reset also waits for live TX
leases. Passive scan RX is accepted while the radio is stable even when its TX
traffic policy is `none`; no normal RX is delivered during a hardware action.
When the first frame precedes the first real PHY report, an eight-slot bounded
cache waits for the matching PPDU counter/rate for up to 250 ms. It never supplies
synthetic RSSI. The value 4 passed to `deliverRealtekRx` is the RX descriptor
offset following the PCI prefix, not an assumed PHY rate.

Configuration is an OSData `R16SSID` (1–32 raw bytes), plus optional OSData `R16PSK`
(32-byte pre-derived WPA2 PSK). Missing SSID leaves the interface waiting instead
of joining an arbitrary open network. No passphrase derivation runs inside the
kernel gate and no key is logged. The main personality/build owner must supply
configuration and the concrete boot service before installation.

## Validation and limits

The actual controller translation unit was compiled for x86_64 macOS kernel
against the pinned MacKernelSDK and itlwm protocol headers. This checks native
IOKit signatures and instantiates StationController with its real adapter. It is
not a hardware, firmware-ACK, association or DHCP success test. The parent build
must link the complete kext, verify all symbols and inspect the concrete boot
implementation before attempting a machine boot. Existing bounded component
host tests cover the ring/runtime/command/station implementations used here.
