// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "PciRingSetup.hpp"
namespace rtl8852be { namespace network {
// Borrowed BAR2 mapping: the owning controller keeps device open and both
// objects retained. No mapping/unmapping, PCI command changes or DMA start.
class MacPciRingIo {
    IOPCIDevice *device_{};IOMemoryMap *mapping_{};
    bool range(uint32_t offset,size_t bytes)const;
public:
    MacPciRingIo(IOPCIDevice *device,IOMemoryMap *mapping);
    bool valid()const{return device_&&mapping_;}
    uint16_t command();
    uint16_t read16(uint32_t offset);
    uint32_t read32(uint32_t offset);
    bool write16(uint32_t offset,uint16_t value);
    bool write32(uint32_t offset,uint32_t value);
    uint64_t nowUs();
    void pauseUs(unsigned microseconds);
};
extern template class PciRingSetup<MacPciRingIo>;
} }
