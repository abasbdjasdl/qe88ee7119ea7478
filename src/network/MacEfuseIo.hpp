// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "EfuseReader.hpp"
namespace rtl8852be { namespace network {
// Raw I/O remains available after cancel, specifically for rail cleanup.
// The owner holds the device/map until readDdv returns and reports restoration.
class MacEfuseIo {
    IOPCIDevice *device_{};IOMemoryMap *mapping_{};bool cancelled_{};
    bool accessible(uint32_t offset,size_t bytes)const;
public:
    MacEfuseIo(IOPCIDevice *device,IOMemoryMap *mapping);
    bool valid()const{return device_&&mapping_;}
    void cancel(){cancelled_=true;}bool cancelled()const{return cancelled_;}
    bool read8(uint32_t offset,uint8_t &value);
    bool read16(uint32_t offset,uint16_t &value);
    bool read32(uint32_t offset,uint32_t &value);
    bool write8(uint32_t offset,uint8_t value);
    bool write16(uint32_t offset,uint16_t value);
    bool write32(uint32_t offset,uint32_t value);
    bool delayUs(unsigned us);
    uint64_t nowUs();
};
extern template class EfuseReader<MacEfuseIo>;
} }
