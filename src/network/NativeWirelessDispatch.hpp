// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeWirelessData.hpp"
#include "NativeWirelessRequests.hpp"
namespace rtl8852be { namespace network { namespace nativewifi {
// Adapter boundary, not an IO80211 registration or a userspace pointer handler.
// The verified framework entry point must supply a kernel-owned buffer and
// its actual capacity. Scratch must be caller-owned (not a large stack object),
// serialized by that entry point, and never reused across concurrent requests.
template<class Controller>
IOReturn getRequest(Controller &controller,wireless::Snapshot &scratch,
                    unsigned request,void *buffer,size_t capacity){
    size_t required=0;
    switch(request){
    case APPLE80211_IOC_SSID:required=sizeof(apple80211_ssid_data);break;
    case APPLE80211_IOC_BSSID:required=sizeof(apple80211_bssid_data);break;
    case APPLE80211_IOC_CHANNEL:required=sizeof(apple80211_channel_data);break;
    case APPLE80211_IOC_RSSI:required=sizeof(apple80211_rssi_data);break;
    default:return kIOReturnUnsupported;
    }
    if(!buffer||capacity<required)return kIOReturnBadArgument;
    memset(buffer,0,required);
    const IOReturn result=controller.copyWirelessStatus(scratch);
    if(result!=kIOReturnSuccess)return result;
    // Each reply comes from one snapshot acquired under the hardware gate.
    switch(request){
    case APPLE80211_IOC_SSID:return ssid(scratch,static_cast<apple80211_ssid_data*>(buffer));
    case APPLE80211_IOC_BSSID:return bssid(scratch,static_cast<apple80211_bssid_data*>(buffer));
    case APPLE80211_IOC_CHANNEL:return channel(scratch,static_cast<apple80211_channel_data*>(buffer));
    case APPLE80211_IOC_RSSI:return rssi(scratch,static_cast<apple80211_rssi_data*>(buffer));
    default:return kIOReturnUnsupported;
    }
}
template<class Controller>
IOReturn setRequest(Controller &controller,unsigned request,const void *buffer,size_t length){
    if(request==APPLE80211_IOC_ASSOCIATE){
        if(!buffer||length!=sizeof(apple80211_assoc_data))return kIOReturnBadArgument;
        // Do not dereference a potentially unaligned framework buffer.
        apple80211_assoc_data local;memcpy(&local,buffer,sizeof(local));
        const auto result=associate(controller,&local);
        selection::wipe(&local,sizeof(local));return result;
    }
    if(request==APPLE80211_IOC_DISASSOCIATE){
        if(length)return kIOReturnBadArgument;
        return disassociate(controller);
    }
    return kIOReturnUnsupported;
}
} } }
