// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include "FirmwareMailbox.hpp"
namespace rtl8852be { namespace firmware {
// Borrowed device, BAR2 and workloop. One controller-owned instance per device;
// no other code may touch the register mailbox or host counter byte concurrently.
class MacMailboxIo {
    IOPCIDevice *device_{};IOMemoryMap *mapping_{};IOWorkLoop *loop_{};bool cancelled_{};
    bool accessible(uint32_t,size_t)const;static void barrier();
public:
    MacMailboxIo(IOPCIDevice *,IOMemoryMap *,IOWorkLoop *);
    bool valid()const{return device_&&mapping_&&loop_;}
    bool inGate()const{return valid()&&loop_->inGate();}
    void cancel(){cancelled_=true;}bool cancelled()const{return cancelled_;}
    bool read8(uint32_t,uint8_t &);bool read16(uint32_t,uint16_t &);bool read32(uint32_t,uint32_t &);
    bool write8(uint32_t,uint8_t);bool write32(uint32_t,uint32_t);
    uint64_t nowUs();bool delayUs(unsigned);
};
extern template class Mailbox<MacMailboxIo>;
} }
