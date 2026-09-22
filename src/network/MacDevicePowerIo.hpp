// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include "DevicePower.hpp"
namespace rtl8852be { namespace powerseq {
// Borrowed PCI/BAR/workloop. DevicePower owns cancellation policy so a latched
// cancel can still enter bounded shutdown/PMC cleanup. Every raw operation still
// requires the gate, a live PCI device, memory enabled and bus mastering off.
class MacDevicePowerIo {
    IOPCIDevice *device_{};IOMemoryMap *mapping_{};IOWorkLoop *loop_{};bool cancelled_{};
    bool accessible(uint32_t,unsigned,bool);
    bool gateAndPci();
    static void barrier();
public:
    MacDevicePowerIo(IOPCIDevice *,IOMemoryMap *,IOWorkLoop *);
    bool valid()const{return device_&&mapping_&&loop_;}
    bool inGate(){return valid()&&loop_->inGate();}
    void cancel(){cancelled_=true;}bool cancelled()const{return cancelled_;}
    uint16_t command();
    bool read8(uint32_t,uint8_t &);bool read16(uint32_t,uint16_t &);bool read32(uint32_t,uint32_t &);
    bool write8(uint32_t,uint8_t);bool write16(uint32_t,uint16_t);bool write32(uint32_t,uint32_t);
    uint64_t nowUs();bool delayUs(unsigned);
};
extern template class DevicePower<MacDevicePowerIo>;
} }
