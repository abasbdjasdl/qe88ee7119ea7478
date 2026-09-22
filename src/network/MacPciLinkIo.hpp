// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include "PciLinkInitialization.hpp"
namespace rtl8852be { namespace network { namespace pcilink {
class MacPciLinkIo {
    IOPCIDevice *device_{};IOMemoryMap *mapping_{};IOWorkLoop *loop_{};bool cancelled_{};
    bool accessible(uint32_t,size_t);static void barrier();
public:
    MacPciLinkIo(IOPCIDevice *,IOMemoryMap *,IOWorkLoop *);
    bool valid()const{return device_&&mapping_&&loop_;}
    bool inGate()const{return valid()&&loop_->inGate();}
    bool cancelled()const{return cancelled_;}void cancel(){cancelled_=true;}
    uint64_t nowUs();bool delayUs(unsigned);
    bool read8(uint32_t,uint8_t &);bool read16(uint32_t,uint16_t &);bool read32(uint32_t,uint32_t &);
    bool write8(uint32_t,uint8_t);bool write16(uint32_t,uint16_t);bool write32(uint32_t,uint32_t);
    bool readConfig8(uint16_t,uint8_t &);bool writeConfig8(uint16_t,uint8_t);
};
extern template class Initialization<MacPciLinkIo>;
} } }
