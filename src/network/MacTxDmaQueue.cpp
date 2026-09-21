// SPDX-License-Identifier: GPL-2.0-or-later
#include "MacTxDmaQueue.hpp"
#include <sys/errno.h>
namespace rtl8852be { namespace network {
bool MacTxDmaQueue::allocate(IOPCIDevice *device,IOWorkLoop *loop){
    if(attached_||!memory_.allocate(device,loop))return false;
    for(size_t i=0;i<64;++i)pages_[i]={memory_.descriptorMapping(i),memory_.frameMapping(i)};
    return true;
}
int MacTxDmaQueue::attach(ieee80211com *ic,uint8_t channel){
    if(attached_||!memory_.ready())return EBUSY;
    const int error=queue_.initialize(ic,channel,memory_.ringMapping(),pages_);attached_=error==0;return error;
}
int MacTxDmaQueue::stage(TxLease &lease,TxInfo info,uint16_t &nextProducer){
    nextProducer=0;if(!attached_||!memory_.ready())return ENETDOWN;
    uint16_t page=0xffff;const int error=queue_.stage(lease,info,nextProducer,&page);if(error)return error;
    if(!memory_.syncForDevice(page)){
        // The queue already owns this lease. Retain it until proven DMA stop;
        // the caller must not return it or publish a doorbell after this error.
        nextProducer=0;return EIO;
    }
    return 0;
}
bool MacTxDmaQueue::releaseAfterDmaStopped(){
    queue_.reclaimAfterDmaStopped();attached_=false;for(auto &p:pages_)p={};
    return memory_.releaseAfterDmaStopped();
}
bool MacFirmwareDmaQueue::allocate(IOPCIDevice *device,IOWorkLoop *loop){
    if(attached_||!memory_.allocate(device,loop))return false;
    for(size_t i=0;i<64;++i)packets_[i]=memory_.frameMapping(i);return true;
}
int MacFirmwareDmaQueue::attach(){
    if(attached_||!memory_.ready())return EBUSY;
    const int error=queue_.initialize(memory_.ringMapping(),packets_);attached_=error==0;return error;
}
int MacFirmwareDmaQueue::stage(const uint8_t *command,size_t bytes,uint16_t &nextProducer){
    nextProducer=0;if(!attached_||!memory_.ready())return ENETDOWN;
    const int error=queue_.stage(command,bytes,nextProducer);if(error)return error;
    const size_t slot=(nextProducer+63)%64;
    if(!memory_.syncForDevice(slot)){nextProducer=0;return EIO;}return 0;
}
bool MacFirmwareDmaQueue::releaseAfterDmaStopped(){
    queue_.reclaimAfterDmaStopped();attached_=false;for(auto &p:packets_)p={};
    return memory_.releaseAfterDmaStopped();
}
} }
