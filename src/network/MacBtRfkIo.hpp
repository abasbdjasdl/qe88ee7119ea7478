// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include "BtRfkCoordination.hpp"
#include "FirmwareCommands.hpp"
namespace rtl8852be { namespace bt {
// Native, restricted BAR2 interface for RfkCoordination. The command link must
// come from the SAME global FirmwareCommands used by the station and radio.
// Borrowed device/map/workloop/link owner must outlive all coordinator callbacks.
class MacBtRfkIo {
    IOPCIDevice *device_{};IOMemoryMap *mapping_{};IOWorkLoop *loop_{};
    network::FirmwareCommandLink commands_{};bool cancelled_{};
    bool accessible(uint32_t,size_t);static void barrier();
public:
    MacBtRfkIo(IOPCIDevice *,IOMemoryMap *,IOWorkLoop *,network::FirmwareCommandLink);
    bool valid()const{return device_&&mapping_&&loop_&&commands_.valid();}
    bool inGate()const{return valid()&&loop_->inGate();}
    bool cancelled()const{return cancelled_;}void cancel(){cancelled_=true;}
    uint64_t nowUs();bool delayUs(unsigned);
    bool read8(uint32_t,uint8_t &);bool read16(uint32_t,uint16_t &);bool read32(uint32_t,uint32_t &);
    bool write8(uint32_t,uint8_t);bool write16(uint32_t,uint16_t);bool write32(uint32_t,uint32_t);
    bool submitH2c(network::CommandId,bool,const uint8_t *,size_t,uint8_t &);
    void invalidateFirmwareEpoch(){if(commands_.valid())commands_.invalidate(commands_.owner);}
};
extern template class RfkCoordination<MacBtRfkIo>;
} }
