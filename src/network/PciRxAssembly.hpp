// SPDX-License-Identifier: BSD-3-Clause
// PCI segment layout follows pinned rtw89 pci.c rxbd_deliver_skbs.
// Copyright(c) 2020-2022 Realtek Corporation (upstream BSD option)
#pragma once
#include "NetworkDescriptors.hpp"
namespace rtl8852be { namespace network {
enum class AssemblyStatus {incomplete,complete,invalid};
struct AssembledRx {const uint8_t *data{};size_t bytes{};RxPacket packet{};};
// Allocate per RXQ/RPQ off the kernel stack. Returned storage survives until
// the next feed/reset; the callback must consume/copy it before recycling.
class PciRxAssembly {
    uint8_t bytes_[4+32+6+56+16383]{};
    size_t used_{},target_{};
    bool active_{};
public:
    void reset(){used_=target_=0;active_=false;}
    bool pending()const{return active_;}
    AssemblyStatus feed(const uint8_t *dma,size_t capacity,AssembledRx &out){
        out={};
        if(!dma||capacity<4){reset();return AssemblyStatus::invalid;}
        const uint32_t prefix=little32(dma);
        const size_t written=prefix&0x3fff;
        const bool first=prefix&0x8000,last=prefix&0x4000;
        // In packet mode the 8852BE does not use RX tag validation. Trust the
        // completed producer index plus cache synchronization, never tag alone.
        if(written<4||written>capacity){reset();return AssemblyStatus::invalid;}
        size_t start=4;
        if(first){
            if(active_){reset();return AssemblyStatus::invalid;}
            RxPacket header{};
            if(decodeRxHeader(dma,written,4,header)!=DescriptorStatus::ok){reset();return AssemblyStatus::invalid;}
            target_=header.offset+header.length;
            if(target_>sizeof(bytes_)){reset();return AssemblyStatus::invalid;}
            used_=header.offset;start=header.offset;
            for(size_t i=0;i<used_;++i)bytes_[i]=dma[i];active_=true;
        }else if(!active_){reset();return AssemblyStatus::invalid;}
        size_t copy=written-start;
        // Some complete packet-mode buffers contain trailing alignment bytes.
        // Only the FS+LS case may trim these, as in upstream rtw89.
        if(copy>target_-used_){
            if(first&&last)copy=target_-used_;
            else{reset();return AssemblyStatus::invalid;}
        }
        if(!copy || (!last&&copy==target_-used_)){reset();return AssemblyStatus::invalid;}
        for(size_t i=0;i<copy;++i)bytes_[used_+i]=dma[start+i];used_+=copy;
        if(!last)return AssemblyStatus::incomplete;
        if(used_!=target_){reset();return AssemblyStatus::invalid;}
        if(decodeRx(bytes_,used_,4,out.packet)!=DescriptorStatus::ok){reset();return AssemblyStatus::invalid;}
        out.data=bytes_;out.bytes=used_;active_=false;used_=target_=0;
        return AssemblyStatus::complete;
    }
};
} }
