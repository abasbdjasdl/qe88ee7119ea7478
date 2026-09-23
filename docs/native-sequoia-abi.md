# Native menu integration checkpoint

The existing deployed driver remains the WPA2 Ethernet-bridge implementation.
This change does not register a native interface, enable new bands/security
modes, or deploy a test build.

The macOS 15.4.1 recovery kernel has different private class slots and parameter
signatures from the pinned Sonoma-era headers. The new offline declaration
audit matches seven complete target tables instead of checking only whether
callbacks happen to occupy pure-virtual slots. This catches reordered callbacks
that would otherwise compile and call the wrong method in the kernel.

The target includes `init(IOService*, ether_addr*)`, `setMacAddress(ether_addr&)`,
`getSelfMacAddr()` returning six bytes by value, const logger/work-queue accessors,
new WCL parameter types and a different InfraProtocol callback list. The audit
also discovered two occupied IONetworkController slots that the pinned SDK still
declared reserved. Return evidence and unresolved declarations are documented in
`tools/native_abi/`; an exact mangled symbol is not proof of a return type.

Backend groundwork now has two distinct components:

* All State link transitions pass through the controller's virtual
  `setLinkStatus`. The same hardware gate retains the existing port-authorization
  guard and records only successfully applied link states. A frontend can pull
  a revision/epoch/selection-generation snapshot from its own queue without
  calling back into Apple framework code while holding the hardware gate.
* `NativeScanCache` stores complete, immutable-while-borrowed scan snapshots with
  raw IE bytes and explicit signal units. It requires a full channel plan and
  full-scan completion; a per-channel dwell cannot publish a result. Failed,
  cancelled or lossy scans preserve the previous complete snapshot. This cache
  is not yet connected to live RX, a scan scheduler or WCL notifications.

Remaining native-menu work includes actual IO80211 controller/interface
construction, valid registration/queue objects and teardown, linking against
the target's exported symbols, independent scan requests that preserve an
existing connection, decoding the WCL SSID/key request, and real association/
failure/link notifications. The old native association struct cannot be cast
onto the newer 988-byte WCL candidate message. Runtime validation must prove
native discovery, scan results, selecting a second network, DHCP and HTTPS;
successful compilation alone does not establish any of those behaviors.
