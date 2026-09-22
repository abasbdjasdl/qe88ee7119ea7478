// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "WirelessStatus.hpp"
#include <Airport/Apple80211.h>
#include <libkern/libkern.h>
namespace rtl8852be { namespace network { namespace nativewifi {
// Data translation only. This does not instantiate an unverified Apple subclass.
// The controller adapter must obtain one gated snapshot before each request.
inline bool current(const wireless::Snapshot &s){
    return s.currentValid&&!s.selectionPending&&s.current.ssidLength<=32&&
        (s.link==wireless::Link::connected||s.link==wireless::Link::authenticating);
}
inline IOReturn ssid(const wireless::Snapshot &s,apple80211_ssid_data *out){
    if(!out)return kIOReturnBadArgument;
    memset(out,0,sizeof(*out));out->version=APPLE80211_VERSION;
    if(!current(s))return kIOReturnNotReady;
    out->ssid_len=s.current.ssidLength;
    memcpy(out->ssid_bytes,s.current.ssid,out->ssid_len);return kIOReturnSuccess;
}
inline IOReturn bssid(const wireless::Snapshot &s,apple80211_bssid_data *out){
    if(!out)return kIOReturnBadArgument;
    memset(out,0,sizeof(*out));out->version=APPLE80211_VERSION;
    if(!current(s))return kIOReturnNotReady;
    memcpy(out->bssid.octet,s.current.bssid,6);return kIOReturnSuccess;
}
inline IOReturn channel(const wireless::Snapshot &s,apple80211_channel_data *out){
    if(!out)return kIOReturnBadArgument;
    memset(out,0,sizeof(*out));out->version=APPLE80211_VERSION;
    if(!current(s)||!s.current.channel)return kIOReturnNotReady;
    out->channel.version=APPLE80211_VERSION;out->channel.channel=s.current.channel;
    out->channel.flags=APPLE80211_C_FLAG_20MHZ|
        (s.current.fiveGhz?APPLE80211_C_FLAG_5GHZ:APPLE80211_C_FLAG_2GHZ);
    return kIOReturnSuccess;
}
inline IOReturn rssi(const wireless::Snapshot &s,apple80211_rssi_data *out){
    if(!out)return kIOReturnBadArgument;
    memset(out,0,sizeof(*out));out->version=APPLE80211_VERSION;
    if(!current(s))return kIOReturnNotReady;
    out->num_radios=1;out->rssi_unit=APPLE80211_UNIT_PERCENT;
    out->rssi[0]=out->aggregate_rssi=out->rssi_ext[0]=out->aggregate_rssi_ext=
        s.current.signalPercent>100?100:s.current.signalPercent;
    return kIOReturnSuccess;
}
} } }
