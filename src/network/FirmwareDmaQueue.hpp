// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2020-2022 Realtek Corporation (upstream BSD option)
// CH12 framing and retention policy: pinned rtw89 core.c/pci.c.
#pragma once
#include "FirmwareProtocol.hpp"
namespace rtl8852be { namespace network {
inline bool encodeCommandDma(const uint8_t *command,size_t length,uint64_t physical,
                             uint8_t *out,size_t capacity,uint8_t (&bd)[8],size_t &written){
    written=0;
    if(!command||!out||length<8||length>16383||capacity<length+24||!dma32Range(physical,length+24))return false;
    if((little32(command+4)&0x3fff)!=length)return false;
    const auto source=reinterpret_cast<uintptr_t>(command),target=reinterpret_cast<uintptr_t>(out);
    if(target>=source?target-source<length:source-target<length+24)return false;
    TxInfo info{};info.ch_dma=12;info.pkt_size=uint16_t(length);
    if(length>=24)info.seq=uint16_t((uint16_t(command[22])|(uint16_t(command[23])<<8))>>4);
    size_t descriptorBytes;
    if(encodeTx(info,out,capacity,descriptorBytes)!=DescriptorStatus::ok||descriptorBytes!=24)return false;
    for(size_t i=0;i<length;++i)out[24+i]=command[i];
    store16(bd,uint16_t(length+24));store16(bd+2,0x4000);store32(bd+4,uint32_t(physical));
    written=length+24;return true;
}
// Unlike data TX, firmware commands have no RPQ completion. Upstream retains
// the most recent eight consumed packets for PCI multi-tag DMA. Preserve that
// exact lag; a firmware ACK does not authorize earlier DMA buffer reuse.
template<size_t Count> class FirmwareDmaOwnership {
    static_assert(Count>9&&Count<=4095,"CH12 needs room for 8 retained DMA tags");
    void *cookies_[Count]{};uint16_t retained_[Count]{};
    size_t producer_{},consumer_{},pending_{},holdHead_{},holdTail_{},held_{};
public:
    FirmwareDmaOwnership()=default;
    FirmwareDmaOwnership(const FirmwareDmaOwnership &)=delete;
    FirmwareDmaOwnership &operator=(const FirmwareDmaOwnership &)=delete;
    size_t producer()const{return producer_;}
    size_t retained()const{return held_;}
    size_t pending()const{return pending_;}
    bool full()const{return pending_==Count-1||cookies_[producer_];}
    bool commit(uint16_t slot,void *cookie){
        if(!cookie||full()||slot!=producer_)return false;
        cookies_[producer_]=cookie;producer_=(producer_+1)%Count;++pending_;return true;
    }
    template<class Release> bool consumeTo(size_t index,Release release){
        if(index>=Count)return false;const size_t advance=(index+Count-consumer_)%Count;
        if(advance>pending_)return false;
        for(size_t n=0;n<advance;++n){
            retained_[holdTail_]=uint16_t(consumer_);holdTail_=(holdTail_+1)%Count;++held_;
            consumer_=(consumer_+1)%Count;--pending_;
            if(held_>8){const auto slot=retained_[holdHead_];holdHead_=(holdHead_+1)%Count;--held_;
                void *cookie=cookies_[slot];cookies_[slot]=nullptr;release(cookie);}
        }
        return true;
    }
    // DMA-idle + bus-master-off proof is required, just like the data ledger.
    template<class Release> void reclaimAfterDmaStopped(Release release){
        for(auto &cookie:cookies_)if(cookie){release(cookie);cookie=nullptr;}
        producer_=consumer_=pending_=holdHead_=holdTail_=held_=0;
    }
};
} }
