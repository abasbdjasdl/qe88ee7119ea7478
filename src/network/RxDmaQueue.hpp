// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2020-2022 Realtek Corporation (rtw89 RX BD format, BSD option)
#pragma once
#include "DmaMapping.hpp"
#include "PciDataPath.hpp"
namespace rtl8852be { namespace network {
struct RxDmaPoll {bool ok{};uint16_t consumer{};size_t processed{},delivered{},dropped{};};
// One instance each for RXQ/RPQ, allocated off the kernel stack. The 8852BE
// uses packet mode, check_rx_tag=false and rx_ring_eq_is_full=false.
// Caller reads a hardware producer after MMIO acquire and publishes returned
// consumer only after success. A fault requires full DMA stop before reuse.
template<class Buffer> class RxDmaQueue {
public:
    static constexpr size_t count=64,bufferBytes=11454+40;
    using Receive=void (*)(void *,const uint8_t *,size_t);
private:
    Buffer ring_,buffers_[count];uint16_t consumer_{};
    bool occupied_{},ready_{},visible_{},faulted_{};
public:
    RxDmaQueue()=default;RxDmaQueue(const RxDmaQueue &)=delete;RxDmaQueue &operator=(const RxDmaQueue &)=delete;
    template<class Device,class Loop> bool allocate(Device *device,Loop *loop){
        if(occupied_)return false;occupied_=true;consumer_=0;faulted_=false;
        if(!ring_.allocate(device,loop,count*8,8)){release();return false;}
        for(size_t i=0;i<count;++i)if(!buffers_[i].allocate(device,loop,bufferBytes,8)){release();return false;}
        const auto ring=ring_.mapping();
        if(!ring.bytes||ring.capacity<count*8||!dma32Range(ring.physical,count*8)){release();return false;}
        for(size_t i=0;i<count;++i){const auto b=buffers_[i].mapping();
            if(!b.bytes||b.capacity<bufferBytes||!dma32Range(b.physical,bufferBytes)){release();return false;}
            uint8_t *bd=ring.bytes+i*8;store16(bd,uint16_t(bufferBytes));store16(bd+2,0);store32(bd+4,uint32_t(b.physical));
        }
        if(!ring_.syncForDevice()){release();return false;}
        ready_=true;return true;
    }
    DataMapping ringMapping()const{return ready_?ring_.mapping():DataMapping{};}
    bool faulted()const{return faulted_;}
    bool markDeviceVisible(){
        if(!ready_||faulted_)return false;
        visible_=true;
        bool ok=ring_.markDeviceVisible();
        for(auto &b:buffers_)ok=b.markDeviceVisible()&&ok;
        if(!ok)faulted_=true;return ok;
    }
    RxDmaPoll poll(uint16_t producer,size_t budget,Receive receive,void *context){
        RxDmaPoll result;result.consumer=consumer_;
        if(!ready_||!visible_||faulted_||producer>=count||!budget||!receive)return result;
        size_t pending=(producer+count-consumer_)%count;if(pending>budget)pending=budget;
        for(size_t i=0;i<pending;++i){auto &buffer=buffers_[consumer_];
            if(!buffer.syncForCpu()){faulted_=true;return result;}
            const auto m=buffer.mapping();const size_t written=little32(m.bytes)&0x3fff;
            if(written<4||written>bufferBytes){++result.dropped;}
            else {receive(context,m.bytes,written);++result.delivered;}
            // Clear only after synchronous processing. No callback may retain
            // this DMA pointer after returning; RX assembly copies its payload.
            store32(m.bytes,0);
            if(!buffer.syncForDevice()){faulted_=true;return result;}
            consumer_=uint16_t((consumer_+1)%count);result.consumer=consumer_;++result.processed;
        }
        result.ok=true;return result;
    }
    bool release(){
        if(visible_)return false;ready_=false;bool ok=true;
        for(auto &b:buffers_)ok=b.release()&&ok;
        ok=ring_.release()&&ok;if(ok){occupied_=false;faulted_=false;consumer_=0;}return ok;
    }
    // Same quiescence contract as MacDmaBuffer::releaseAfterDmaStopped.
    bool releaseAfterDmaStopped(){
        ready_=false;visible_=false;bool ok=true;
        for(auto &b:buffers_)ok=b.releaseAfterDmaStopped()&&ok;
        ok=ring_.releaseAfterDmaStopped()&&ok;
        if(ok){occupied_=false;faulted_=false;consumer_=0;}return ok;
    }
};
} }
