// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace rtl8852be { namespace network { namespace wireless {
// Kernel adapter data, not an Apple private ABI or a userspace wire structure.
// No keys, stack-owned pointers, or guessed dBm measurements leave the gate.
enum class Link : uint8_t { off, idle, scanning, connecting, authenticating, connected, faulted };
struct Network {
    uint8_t ssid[32]{},ssidLength{},bssid[6]{},channel{},signalPercent{};
    bool fiveGhz{},privacy{};
    uint32_t protocols{},akms{},ciphers{}; // Observed net80211 flags, not supported capabilities.
    uint16_t rsnCapabilities{};
};
struct Snapshot {
    uint64_t sampledAtUs{},selectionGeneration{};
    Link link{Link::off};
    bool currentValid{},selectionPending{},scanInProgress{},cacheTruncated{};
    Network current{};
    uint32_t count{};
    Network cached[64]{}; // Cached observations; a read never claims a fresh scan.
};
inline Link linkState(bool enabled,bool faulted,bool stopping,bool pending,
                      bool scanning,bool run,bool authorized,bool associated){
    if(faulted)return Link::faulted;
    if(!enabled||stopping)return Link::off;
    if(pending)return Link::connecting;
    if(scanning)return Link::scanning;
    if(run&&authorized)return Link::connected;
    if(run||associated)return Link::authenticating;
    return Link::idle;
}
inline bool append(Snapshot &s,const Network &n){
    if(n.ssidLength>sizeof(n.ssid))return false;
    if(s.count>=sizeof(s.cached)/sizeof(s.cached[0])){s.cacheTruncated=true;return false;}
    s.cached[s.count++]=n;return true;
}
} } }
