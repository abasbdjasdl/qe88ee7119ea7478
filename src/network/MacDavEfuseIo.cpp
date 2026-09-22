// SPDX-License-Identifier: BSD-3-Clause
#include "MacDavEfuseIo.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
namespace rtl8852be { namespace network {
MacDavEfuseIo::MacDavEfuseIo(IOPCIDevice *device,IOMemoryMap *mapping){
    if(!device||!mapping||device->configRead16(kIOPCIConfigVendorID)!=0x10ec||
       device->configRead16(kIOPCIConfigDeviceID)!=0xb852||!mapping->getVirtualAddress())return;
    auto *bar=device->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if(!bar||mapping->getPhysicalAddress()!=bar->getPhysicalAddress()||
       mapping->getLength()!=bar->getLength()||mapping->getLength()<0x1000)return;
    device_=device;mapping_=mapping;
}
bool MacDavEfuseIo::accessible()const{
    if(!valid())return false;
    const auto command=device_->configRead16(kIOPCIConfigCommand);
    return command!=0xffff&&(command&2);
}
bool MacDavEfuseIo::read8(uint32_t offset,uint8_t &value){
    value=0;if(offset!=0x271||!accessible())return false;
    OSSynchronizeIO();value=*(reinterpret_cast<const volatile uint8_t *>(mapping_->getVirtualAddress())+offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);return true;
}
bool MacDavEfuseIo::read32(uint32_t offset,uint32_t &value){
    value=0;if(offset!=0x270||!accessible())return false;
    OSSynchronizeIO();value=OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);return true;
}
bool MacDavEfuseIo::write32(uint32_t offset,uint32_t value){
    if(offset!=0x270||!davCommandAllowed(value)||!accessible())return false;
    // Do not overwrite a still-running SI request, including calls outside the reader.
    uint32_t old=0;if(!read32(offset,old)||old==0xffffffffu||(old&0x80000000u))return false;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),offset,value);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();return true;
}
bool MacDavEfuseIo::delayUs(unsigned us){if(!valid()||us>50)return false;if(us)IODelay(us);return true;}
uint64_t MacDavEfuseIo::nowUs(){uint64_t ticks=0,ns=0;clock_get_uptime(&ticks);absolutetime_to_nanoseconds(ticks,&ns);return ns/1000;}
template class DavEfuseReader<MacDavEfuseIo>;
} }
