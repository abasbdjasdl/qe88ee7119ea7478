// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "NetworkDescriptors.hpp"
namespace rtl8852be { namespace network {
// Metadata only. Never stores payloads, addresses or EAPOL key material.
struct RxTrace {unsigned type{3};bool eapol{};uint16_t deauthReason{};bool deauth{};};
inline RxTrace inspectRx(const RxPacket &rx,const uint8_t *local){
    RxTrace result;
    if(rx.info.pkt_type||!rx.payload||rx.length<2||(rx.payload[0]&3))return result;
    const auto *p=rx.payload;result.type=(p[0]>>2)&3;
    if(rx.length<28)return result; // 24-byte base header plus FCS.
    bool addressed=local!=nullptr;
    for(unsigned i=0;i<6&&addressed;++i)addressed=p[4+i]==local[i];
    const bool clear=!(p[1]&0x40)&&!hardwareDecrypted(rx.info);
    if(result.type==0&&(p[0]&0xf0)==0xc0&&clear&&addressed&&rx.length>=30){
        result.deauth=true;result.deauthReason=uint16_t(p[24])|(uint16_t(p[25])<<8);
    }
    if(result.type!=2||!clear||!addressed)return result;
    // Null data has no MSDU; A-MSDU requires subframe parsing and is not used.
    if(p[0]&0x40)return result;
    size_t header=24;if((p[1]&3)==3)header+=6;
    if(p[0]&0x80){
        if(rx.length<header+2+4||(p[header]&0x80))return result;
        header+=2;if(p[1]&0x80)header+=4;
    }
    if(rx.length<header+8+4)return result;
    static const uint8_t llc[8]={0xaa,0xaa,3,0,0,0,0x88,0x8e};
    for(unsigned i=0;i<8;++i)if(p[header+i]!=llc[i])return result;
    result.eapol=true;return result;
}
} }
