// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "WirelessSelection.hpp"
#include "WirelessStatus.hpp"
#include "AuthenticationEvents.hpp"
namespace rtl8852be { namespace network { namespace control {
// Versioned, pointer-free local x86_64 ABI. No Apple private layouts cross here.
constexpr uint32_t version=1,connectionType=0x52313601;
// Keep output below the IOKit in-band structure limit. Larger outputs become
// memory descriptors; this deliberately bounded v1 endpoint does not map them.
constexpr uint32_t cacheCapacity=60;
enum Selector : uint32_t { status=0,join=1,disconnect=2,captureBegin=3,captureEnd=4,captureRead=5 };
struct Network {
    uint8_t ssid[32],bssid[6],ssidLength,channel,signalPercent,flags;
    uint16_t rsnCapabilities;
    uint32_t protocols,akms,ciphers,reserved[2];
};
struct Status {
    uint32_t version,flags;
    uint64_t sampledAtUs,selectionGeneration;
    uint32_t link,count;
    Network current,cached[cacheCapacity];
};
struct Join {
    uint32_t version,security,pairwise,group,ssidLength,pmkLength,specificBssid,reserved;
    uint8_t ssid[32],bssid[6],reservedBytes[2],pmk[32];
};
static_assert(sizeof(Network)==64&&sizeof(Status)==3936&&sizeof(Join)==104,"control ABI layout changed");
inline void encode(const wireless::Network &n,Network &out){
    out={};if(n.ssidLength>32)return;
    for(unsigned i=0;i<n.ssidLength;++i)out.ssid[i]=n.ssid[i];
    for(unsigned i=0;i<6;++i)out.bssid[i]=n.bssid[i];
    out.ssidLength=n.ssidLength;out.channel=n.channel;
    out.signalPercent=n.signalPercent>100?100:n.signalPercent;
    out.flags=(n.fiveGhz?1:0)|(n.privacy?2:0);out.rsnCapabilities=n.rsnCapabilities;
    out.protocols=n.protocols;out.akms=n.akms;out.ciphers=n.ciphers;
}
inline void encode(const wireless::Snapshot &s,Status &out){
    out={};out.version=version;out.sampledAtUs=s.sampledAtUs;out.selectionGeneration=s.selectionGeneration;
    out.flags=(s.currentValid?1:0)|(s.selectionPending?2:0)|(s.scanInProgress?4:0)|
        ((s.cacheTruncated||s.count>cacheCapacity)?8:0);
    out.link=static_cast<uint32_t>(s.link);out.count=s.count>cacheCapacity?cacheCapacity:s.count;
    if(s.currentValid)encode(s.current,out.current);
    for(unsigned i=0;i<out.count;++i)encode(s.cached[i],out.cached[i]);
}
inline bool decode(const Join &in,selection::Join &out){
    selection::wipe(&out,sizeof(out));
    if(in.version!=version||in.reserved||in.reservedBytes[0]||in.reservedBytes[1]||
       in.security>2||in.pairwise>1||in.group>1||in.specificBssid>1||
       !in.ssidLength||in.ssidLength>32||in.pmkLength>32)return false;
    // Preserve existing enum values: 0=open, 1=WPA2-PSK, 2=WPA-PSK.
    out.security=static_cast<selection::Security>(in.security);
    out.pairwise=static_cast<selection::Cipher>(in.pairwise);out.group=static_cast<selection::Cipher>(in.group);
    out.ssidLength=in.ssidLength;out.pmkLength=in.pmkLength;out.specificBssid=in.specificBssid!=0;
    for(unsigned i=0;i<32;++i){out.ssid[i]=in.ssid[i];out.pmk[i]=in.pmk[i];}
    for(unsigned i=0;i<6;++i)out.bssid[i]=in.bssid[i];
    if(!selection::valid(out)){selection::wipe(&out,sizeof(out));return false;}
    // Open requests cannot smuggle unused key bytes into retained state.
    if(!out.pmkLength)selection::wipe(out.pmk,sizeof(out.pmk));
    return true;
}
} } }
