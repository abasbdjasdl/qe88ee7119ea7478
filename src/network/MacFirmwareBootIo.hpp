// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include "FirmwareBootPreparation.hpp"
#include "../PciFirmwareTransport.hpp"
namespace rtl8852be { namespace firmwareboot {
class MacFirmwareBootIo {
    IOPCIDevice *device_{};IOMemoryMap *mapping_{};IOWorkLoop *loop_{};
    bool cancelled_{},cleanup_{};bool access(uint32_t,unsigned,bool masterAllowed);
    static void barrier();
public:
    MacFirmwareBootIo(IOPCIDevice *,IOMemoryMap *,IOWorkLoop *);
    bool valid()const{return device_&&mapping_&&loop_;}bool inGate(){return valid()&&loop_->inGate();}
    bool cancelled()const{return cancelled_;}void cancel(){cancelled_=true;}
    void beginCleanup(){cleanup_=true;}void endCleanup(){cleanup_=false;}
    uint16_t command();uint64_t nowUs();bool delayUs(unsigned);void pauseUs(unsigned us){(void)delayUs(us);}
    bool read16(uint32_t,uint16_t &);bool read32(uint32_t,uint32_t &);
    uint16_t read16(uint32_t);uint32_t read32(uint32_t);
    bool write16(uint32_t,uint16_t);bool write32(uint32_t,uint32_t);
    bool uploadWrite16(uint32_t,uint16_t);bool uploadWrite32(uint32_t,uint32_t);
    bool uploadBusMaster(bool);bool interruptsSafe();void uploadBarrier(){barrier();}
};
extern template class Preparation<MacFirmwareBootIo>;
} }
