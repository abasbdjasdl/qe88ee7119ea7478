// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "PciRuntime.hpp"
namespace rtl8852be { namespace network {
// Borrowed device/BAR2; both must outlive runtime and all event sources.
class MacPciRuntimeIo {
    IOPCIDevice *device_{};IOMemoryMap *mapping_{};
    bool range(uint32_t offset,size_t bytes)const;
    bool memoryEnabled(bool requireMaster=false);
public:
    MacPciRuntimeIo(IOPCIDevice *,IOMemoryMap *);
    bool valid()const{return device_&&mapping_;}
    uint16_t command();bool writeCommand(uint16_t);
    uint16_t read16(uint32_t);uint32_t read32(uint32_t);
    bool write16(uint32_t,uint16_t);bool write32(uint32_t,uint32_t);
    void barrier();uint64_t nowUs();void pauseUs(unsigned);
};
extern template class PciRuntime<MacPciRuntimeIo>;
} }
