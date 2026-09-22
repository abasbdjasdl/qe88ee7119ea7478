// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace rtl8852be { namespace network {
// Bounded first-frame wait for the following real PPDU report. Owner clears on
// every admission stop/tune and physical firmware epoch. No invented RSSI.
class MacNetworkPhyWait {
public:
    static constexpr size_t capacity=16512;
    struct Packet {uint8_t bytes[capacity]{};size_t length{};uint64_t time{};uint16_t rate{};bool valid{};};
private:
    Packet packets_[8]{};
public:
    void clear(){for(auto &p:packets_)p.valid=false;}
    bool store(uint8_t ppdu,uint16_t rate,const uint8_t *bytes,size_t length,uint64_t now){
        if(ppdu>=8||!bytes||!length||length>capacity)return false;
        auto &p=packets_[ppdu];p.valid=false;
        for(size_t i=0;i<length;++i)p.bytes[i]=bytes[i];
        p.length=length;p.time=now;p.rate=rate;p.valid=true;return true;
    }
    // Returned bytes remain borrowed until another store to this PPDU slot.
    // Consume the flag before calling protocol, which can request channel changes.
    const Packet *take(uint8_t ppdu,uint16_t rate,uint64_t now){
        if(ppdu>=8)return nullptr;auto &p=packets_[ppdu];
        if(!p.valid)return nullptr;p.valid=false;
        if(p.rate!=rate||now<p.time||now-p.time>250000)return nullptr;
        return &p;
    }
};
} }
