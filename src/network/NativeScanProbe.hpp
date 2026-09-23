// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace rtl8852be { namespace network { namespace scanprobe {

// Probe-request BODY only for the existing 2.4 GHz legacy-rate implementation.
// Caller must independently authorize the channel and own a live scan/dwell.
// The normal net80211 management output adds addresses, sequence and header.
// There is deliberately no saved SSID/key argument and no HT/VHT/HE capability.
constexpr size_t maxRates=12,maxBytes=2+2+8+2+(maxRates-8);
enum class Error : uint8_t {none,buffer,rates,overlap};
struct Result {Error error{Error::buffer};size_t length{};};
inline bool validRate(uint8_t rate){
    switch(rate&0x7f){
    case 2:case 4:case 11:case 22: // DSSS/CCK, units of 500 kb/s.
    case 12:case 18:case 24:case 36:case 48:case 72:case 96:case 108:return true;
    default:return false;
    }
}
inline bool overlaps(const void *a,size_t na,const void *b,size_t nb){
    const uintptr_t x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
    return na&&nb&&(x<=y?y-x<na:x-y<nb);
}
inline Result encode(const uint8_t *rates,size_t count,void *buffer,size_t capacity){
    auto *out=static_cast<uint8_t*>(buffer);
    const size_t cleared=capacity<maxBytes?capacity:maxBytes;
    // Alias is rejected before examining rates; wiping the output may overwrite
    // an aliased input, which is no longer read. Never keep an input pointer.
    const bool alias=rates&&out&&count&&overlaps(rates,count,out,cleared);
    if(out)for(size_t i=0;i<cleared;++i)out[i]=0;
    if(!out)return {Error::buffer,0};
    if(alias)return {Error::overlap,0};
    if(!rates||!count||count>maxRates)return {Error::rates,0};
    for(size_t i=0;i<count;++i){
        if(!validRate(rates[i]))return {Error::rates,0};
        for(size_t j=0;j<i;++j)if((rates[i]&0x7f)==(rates[j]&0x7f))return {Error::rates,0};
    }
    const size_t first=count<8?count:8;
    const size_t required=2+2+count+(count>8?2:0);
    if(capacity<required)return {Error::buffer,0};
    // IE 0 with length 0: an undirected wildcard probe, never an old network.
    out[0]=0;out[1]=0;out[2]=1;out[3]=uint8_t(first);
    for(size_t i=0;i<first;++i)out[4+i]=rates[i];
    if(count>8){
        out[12]=50;out[13]=uint8_t(count-8);
        for(size_t i=8;i<count;++i)out[14+i-8]=rates[i];
    }
    return {Error::none,required};
}
} } }
