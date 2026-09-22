// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "MacInitialization.hpp"
namespace rtl8852be { namespace macinit {
// Borrowed device/BAR2, serialized by the controller's workloop. MAC setup is
// permitted only with bus mastering off. No ownership, power-on or PCI writes.
class MacInitializationIo {
    IOPCIDevice *device_{};IOMemoryMap *mapping_{};bool cancelled_{};
    bool accessible(uint32_t,size_t,bool);
    bool readCmac(uint32_t,unsigned,uint32_t &);
    static void barrier();
public:
    MacInitializationIo(IOPCIDevice *,IOMemoryMap *);
    bool valid()const{return device_&&mapping_;}
    void cancel(){cancelled_=true;}
    bool cancelled()const{return cancelled_;}
    uint16_t command();
    bool read8(uint32_t,uint8_t &);bool read16(uint32_t,uint16_t &);bool read32(uint32_t,uint32_t &);
    bool write8(uint32_t,uint8_t);bool write16(uint32_t,uint16_t);bool write32(uint32_t,uint32_t);
    uint64_t nowUs();bool delayUs(unsigned);
};
extern template class MacInitialization<MacInitializationIo>;
} }
