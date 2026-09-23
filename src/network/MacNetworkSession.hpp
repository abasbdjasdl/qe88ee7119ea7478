// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <IOKit/IOReturn.h>
#include "NativeScanCache.hpp"
#include "NativeForegroundScan.hpp"
#include "WirelessStatus.hpp"
#include "WirelessSelection.hpp"

struct MacNetworkState;
namespace rtl8852be { namespace network {
struct MacNetworkStateHost;
class MacNetworkBootService;
struct MacLinkPublication;

// The same concrete RTL8852BE radio backend is used by the working Ethernet
// owner and a future single native owner; neither factory claims PCI itself.
MacNetworkBootService *createRtl8852beBootService();

// One PCI-backed radio session. create() takes ownership of boot only on
// success; the caller deletes it if allocation fails. prepare() runs before
// the owner gate, start()/stop()/poll() under that gate. Destroy only after a
// successful stop/drain or before any hardware resource was published.
// These functions do not create an IO80211 service or attach a second owner.
MacNetworkState *createMacNetworkState(MacNetworkStateHost &,MacNetworkBootService *);
bool prepareMacNetworkState(MacNetworkState *);
bool startMacNetworkState(MacNetworkState *);
bool stopMacNetworkState(MacNetworkState *);
void pollMacNetworkState(MacNetworkState *);
void destroyMacNetworkState(MacNetworkState *);
IOReturn copyMacNetworkWirelessStatus(MacNetworkState *,wireless::Snapshot &);
IOReturn copyMacNetworkLinkPublication(MacNetworkState *,MacLinkPublication &);
// One already-validated selection value, copied by the session under its gate;
// null requests disconnect. No credential is exposed as a registry property.
IOReturn queueMacNetworkSelection(MacNetworkState *,const selection::Join *);

// Passive, complete hardware observations only. Caller owns the same session
// gate and an output buffer; no borrowed cache pointer crosses that gate.
IOReturn copyMacNetworkScanSummary(MacNetworkState *,nativescan::Summary &);
IOReturn copyMacNetworkScanEntry(MacNetworkState *,nativescan::Token,size_t,
                                 nativescan::Entry &);
IOReturn copyMacNetworkScanChannel(MacNetworkState *,nativescan::Token,size_t,
                                   nativescan::Channel &);
// A copied, exact-profile WCL request enters the real foreground scanner.
// Success is admission only. The owner must separately deliver completion and
// results through its verified IO80211 event path.
IOReturn beginMacNetworkWclScan(MacNetworkState *,const void *,size_t,bool,
                                foregroundscan::Status &);
} }
