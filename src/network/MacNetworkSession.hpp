// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

struct MacNetworkState;
namespace rtl8852be { namespace network {
struct MacNetworkStateHost;
class MacNetworkBootService;

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
} }
