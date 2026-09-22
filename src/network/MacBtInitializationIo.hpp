// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/IOWorkLoop.h>
#include "MacRadioIo.hpp"
#include "FirmwareCommands.hpp"
#include "BtInitialization.hpp"
namespace rtl8852be { namespace bt { namespace initialization {
// Native adapter restricted to the source-defined initial PTA/RF configuration.
// A separate instance from MacBtRfkIo: this is a one-use, 2-second initialization
// access window; its object/command owner and BAR mapping are borrowed.
class MacBtInitializationIo {
    IOPCIDevice *device_{};IOMemoryMap *mapping_{};IOWorkLoop *loop_{};
    network::FirmwareCommandLink commands_{};bool stopped_{},clockStarted_{};
    uint64_t start_{},last_{};
    network::MacRadioIo radioIo_;network::RadioAccess<network::MacRadioIo> radio_;
    bool accessible(uint32_t,size_t);static bool radioGuard(void *);
    static void barrier();
public:
    MacBtInitializationIo(IOPCIDevice *,IOMemoryMap *,IOWorkLoop *,network::FirmwareCommandLink);
    bool valid()const{return device_&&mapping_&&loop_&&commands_.valid()&&radioIo_.valid();}
    bool inGate()const{return valid()&&loop_->inGate();}
    bool cancelled()const{return stopped_;}void cancel(){stopped_=true;radioIo_.cancel();}
    uint64_t nowUs();bool delayUs(unsigned);
    bool read8(uint32_t,uint8_t &);bool read16(uint32_t,uint16_t &);bool read32(uint32_t,uint32_t &);
    bool write8(uint32_t,uint8_t);bool write16(uint32_t,uint16_t);bool write32(uint32_t,uint32_t);
    bool readRf(uint8_t,uint32_t,uint32_t,uint32_t &);bool writeRf(uint8_t,uint32_t,uint32_t,uint32_t);
    bool submitH2c(network::CommandId,bool,const uint8_t *,size_t,uint8_t &);
    void invalidateFirmwareEpoch(){if(commands_.valid())commands_.invalidate(commands_.owner);}
};
extern template class Initialization<MacBtInitializationIo>;
} } }
