// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "MacDmaBuffer.hpp"
#include "TxDmaBank.hpp"
#include "Net80211PciQueue.hpp"
namespace rtl8852be { namespace network {
// Heap/controller-owned. allocate runs outside the gate; attach and all queue
// operations run under the protocol gate. markDeviceVisible precedes publishing
// the ring address. No method enables DMA or writes a doorbell.
class MacTxDmaQueue {
    TxDmaBank<MacDmaBuffer> memory_;
    Net80211PciQueue queue_;
    TxPageMapping pages_[64]{};bool attached_{};
public:
    bool allocate(IOPCIDevice *device,IOWorkLoop *loop);
    int attach(ieee80211com *ic,uint8_t channel);
    DataMapping ringMapping()const{return memory_.ringMapping();}
    bool markDeviceVisible(){return attached_&&memory_.markDeviceVisible();}
    int stage(TxLease &lease,TxInfo info,uint16_t &nextProducer);
    // Exposed for completion/RPQ dispatch and statistics, not raw staging.
    Net80211PciQueue &completions(){return queue_;}
    bool releaseBeforeAttach(){return !attached_&&memory_.release();}
    bool releaseAfterDmaStopped();
};
class MacFirmwareDmaQueue {
    TxDmaBank<MacDmaBuffer,true> memory_;
    Net80211FirmwareQueue queue_;
    DataMapping packets_[64]{};bool attached_{};
public:
    bool allocate(IOPCIDevice *device,IOWorkLoop *loop);
    int attach();
    DataMapping ringMapping()const{return memory_.ringMapping();}
    bool markDeviceVisible(){return attached_&&memory_.markDeviceVisible();}
    int stage(const uint8_t *command,size_t bytes,uint16_t &nextProducer);
    int consumeTo(uint16_t consumer){return queue_.consumeTo(consumer);}
    bool releaseBeforeAttach(){return !attached_&&memory_.release();}
    bool releaseAfterDmaStopped();
};
} }
