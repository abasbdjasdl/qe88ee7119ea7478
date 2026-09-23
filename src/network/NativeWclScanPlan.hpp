// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeWclScan.hpp"
#include "NativeForegroundScan.hpp"

namespace rtl8852be { namespace network { namespace nativewclscanplan {

// This maps a decoded request to an internal, disconnected-only scan plan.
// The caller still owns WCL request generations, result filtering and events.
// Neither a decoded message nor this mapping proves a live IO80211 interface.
enum class Status : uint8_t {planned,notDecoded,unsupportedHomeTiming,unsupportedDwell,invalidChannels};
struct Result {Status status{Status::notDecoded};bool emissionReady{false};};

inline Result map(const nativewclscan::Result &decoded,
                  const nativewclscan::Request &request,
                  foregroundscan::RequestedPlan &out){
    auto *outputBytes=reinterpret_cast<uint8_t*>(&out);
    for(size_t index=0;index<sizeof(out);++index)outputBytes[index]=0;
    if(decoded.status!=nativewclscan::Status::knownSubset)
        return {Status::notDecoded,false};
    // The backend admits only an idle, disconnected station. There is no
    // associated home channel to schedule here. Nonzero WCL home timing is
    // rejected rather than silently replaced by the backend's polling gap.
    if(request.homeRestMs||request.homeAwayMs)
        return {Status::unsupportedHomeTiming,false};
    const bool active=request.mode==nativewclscan::Mode::active;
    if(!active&&request.mode!=nativewclscan::Mode::passive)
        return {Status::notDecoded,false};
    const uint32_t dwell=active?request.activeDwellMs:request.passiveDwellMs;
    if(dwell<10||dwell>1000)return {Status::unsupportedDwell,false};
    if(!request.channelCount||request.channelCount>sizeof(out.channels))
        return {Status::invalidChannels,false};
    uint16_t seen=0;
    for(size_t index=0;index<request.channelCount;++index){
        const uint8_t channel=request.channels[index];
        if(channel<1||channel>11)return {Status::invalidChannels,false};
        const uint16_t bit=uint16_t(1u<<(channel-1));
        if(seen&bit)return {Status::invalidChannels,false};
        seen|=bit;
    }
    out.active=active;out.dwellMs=uint16_t(dwell);
    out.channelCount=request.channelCount;
    for(size_t index=0;index<request.channelCount;++index)
        out.channels[index]=request.channels[index];
    return {Status::planned,false};
}

} } }
