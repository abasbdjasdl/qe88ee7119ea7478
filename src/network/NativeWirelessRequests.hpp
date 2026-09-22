// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "WirelessSelection.hpp"
#include "WirelessRsn.hpp"
#include <Airport/Apple80211.h>
#include <libkern/libkern.h>
namespace rtl8852be { namespace network { namespace nativewifi {
inline IOReturn decodeAssociation(const apple80211_assoc_data *in,selection::Join &out){
    selection::wipe(&out,sizeof(out));
    if(!in||in->version!=APPLE80211_VERSION||!in->ad_ssid_len||in->ad_ssid_len>32||
       in->ad_rsn_ie_len>APPLE80211_MAX_RSN_IE_LEN)return kIOReturnBadArgument;
    if(in->ad_mode!=APPLE80211_AP_MODE_INFRA||in->ad_auth_lower!=APPLE80211_AUTHTYPE_OPEN||in->ad_flags)
        return kIOReturnUnsupported;
    if(in->ad_auth_upper==APPLE80211_AUTHTYPE_NONE){
        if(in->ad_key.key_len||in->ad_rsn_ie_len||in->ad_key.key_cipher_type!=APPLE80211_CIPHER_NONE)
            return kIOReturnUnsupported;
        out.security=selection::Security::open;
    }else if(in->ad_auth_upper==APPLE80211_AUTHTYPE_WPA2_PSK){
        if(in->ad_key.key_cipher_type!=APPLE80211_CIPHER_PMK||in->ad_key.key_len!=32||
           !selection::wpa2PskRsn(in->ad_rsn_ie,in->ad_rsn_ie_len))return kIOReturnUnsupported;
        out.security=selection::Security::wpa2Psk;out.pmkLength=32;
        memcpy(out.pmk,in->ad_key.key,32);
    }else return kIOReturnUnsupported;
    out.ssidLength=in->ad_ssid_len;memcpy(out.ssid,in->ad_ssid,out.ssidLength);
    memcpy(out.bssid,in->ad_bssid.octet,6);
    for(auto b:out.bssid)out.specificBssid=out.specificBssid||b!=0;
    if(!selection::valid(out)){selection::wipe(&out,sizeof(out));return kIOReturnBadArgument;}
    return kIOReturnSuccess;
}
template<class Controller> IOReturn associate(Controller &controller,const apple80211_assoc_data *data){
    selection::Join request;
    IOReturn result=decodeAssociation(data,request);
    if(result==kIOReturnSuccess)result=controller.selectWirelessNetwork(request);
    selection::wipe(&request,sizeof(request));
    return result; // Queued is not an association-complete notification.
}
template<class Controller> IOReturn disassociate(Controller &controller){
    return controller.disconnectWirelessNetwork();
}
} } }
