// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "DavEfuseReader.hpp"
namespace rtl8852be { namespace network {
// Owner retains device/map and exclusive XTAL SI ownership until reader returns.
class MacDavEfuseIo {
    IOPCIDevice *device_{};IOMemoryMap *mapping_{};bool cancelled_{};
    bool accessible()const;
public:
    MacDavEfuseIo(IOPCIDevice *device,IOMemoryMap *mapping);
    bool valid()const{return device_&&mapping_;}
    void cancel(){cancelled_=true;}bool cancelled()const{return cancelled_;}
    bool read8(uint32_t offset,uint8_t &value);
    bool read32(uint32_t offset,uint32_t &value);
    bool write32(uint32_t offset,uint32_t value);
    bool delayUs(unsigned us);
    uint64_t nowUs();
};
extern template class DavEfuseReader<MacDavEfuseIo>;
} }
