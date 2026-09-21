// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "RadioAccess.hpp"
#include "RadioInitialization.hpp"
namespace rtl8852be { namespace network {
// Borrowed BAR2; controller retains device/map and serializes on its workloop.
// This does not turn on power, clocks, bus mastering, queues or interrupts.
class MacRadioIo {
    IOPCIDevice *device_{};IOMemoryMap *mapping_{};bool stopped_{};
    bool accessible(uint32_t offset)const;
public:
    MacRadioIo(IOPCIDevice *device,IOMemoryMap *mapping);
    bool valid()const{return device_&&mapping_;}
    void cancel(){stopped_=true;}
    bool cancelled()const{return stopped_;}
    bool read32(uint32_t offset,uint32_t &value);
    bool write32(uint32_t offset,uint32_t value);
    bool delayUs(unsigned us);
};
extern template class RadioAccess<MacRadioIo>;
extern template class RadioInitialization<MacRadioIo>;
} }
