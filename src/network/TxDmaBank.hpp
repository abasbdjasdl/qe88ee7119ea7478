// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "DmaMapping.hpp"
namespace rtl8852be { namespace network {
// Owns preallocated buffers; packet ownership/recycling stays with the existing
// data/RPQ or firmware/multi-tag ledger. No per-packet DMA allocations.
template<class Buffer,bool Firmware=false> class TxDmaBank {
    Buffer ring_,frames_[64],descriptors_[Firmware?1:64];
    bool occupied_{},ready_{},visible_{},faulted_{};
public:
    static constexpr size_t count=64;
    TxDmaBank()=default;TxDmaBank(const TxDmaBank &)=delete;TxDmaBank &operator=(const TxDmaBank &)=delete;
    template<class Device,class Loop> bool allocate(Device *device,Loop *loop){
        if(occupied_)return false;occupied_=true;faulted_=false;
        if(!ring_.allocate(device,loop,512,8)){release();return false;}
        for(size_t i=0;i<count;++i){
            if(!frames_[i].allocate(device,loop,16383+(Firmware?24:0),8)){release();return false;}
            if(!Firmware&&!descriptors_[i].allocate(device,loop,64,8)){release();return false;}
        }
        ready_=true;return true;
    }
    bool ready()const{return ready_&&!faulted_;}bool faulted()const{return faulted_;}
    DataMapping ringMapping()const{return ready_?ring_.mapping():DataMapping{};}
    DataMapping frameMapping(size_t slot)const{return ready_&&slot<count?frames_[slot].mapping():DataMapping{};}
    DataMapping descriptorMapping(size_t slot)const{return ready_&&!Firmware&&slot<count?descriptors_[slot].mapping():DataMapping{};}
    bool syncForDevice(size_t slot){
        if(!ready()||slot>=count)return false;
        // Data before WD before BD; doorbell is the caller's subsequent step.
        if(!frames_[slot].syncForDevice()||(!Firmware&&!descriptors_[slot].syncForDevice())||!ring_.syncForDevice()){
            faulted_=true;return false;
        }
        return true;
    }
    bool markDeviceVisible(){
        if(!ready())return false;visible_=true;bool ok=ring_.markDeviceVisible();
        for(auto &b:frames_)ok=b.markDeviceVisible()&&ok;
        if(!Firmware)for(auto &b:descriptors_)ok=b.markDeviceVisible()&&ok;
        if(!ok)faulted_=true;return ok;
    }
    bool release(){
        if(visible_)return false;ready_=false;bool ok=true;
        for(auto &b:frames_)ok=b.release()&&ok;
        for(auto &b:descriptors_)ok=b.release()&&ok;
        ok=ring_.release()&&ok;if(ok){occupied_=false;faulted_=false;}return ok;
    }
    bool releaseAfterDmaStopped(){
        ready_=visible_=false;bool ok=true;
        for(auto &b:frames_)ok=b.releaseAfterDmaStopped()&&ok;
        for(auto &b:descriptors_)ok=b.releaseAfterDmaStopped()&&ok;
        ok=ring_.releaseAfterDmaStopped()&&ok;if(ok){occupied_=false;faulted_=false;}return ok;
    }
};
} }
