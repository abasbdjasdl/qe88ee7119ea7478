// SPDX-License-Identifier: BSD-3-Clause
#include "MacEfuseIo.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
namespace rtl8852be { namespace network {
MacEfuseIo::MacEfuseIo(IOPCIDevice *device,IOMemoryMap *mapping){
    if(!device||!mapping||device->configRead16(kIOPCIConfigVendorID)!=0x10ec||
       device->configRead16(kIOPCIConfigDeviceID)!=0xb852||!mapping->getVirtualAddress())return;
    auto *bar=device->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if(!bar||mapping->getPhysicalAddress()!=bar->getPhysicalAddress()||
       mapping->getLength()!=bar->getLength()||mapping->getLength()<0x1000)return;
    device_=device;mapping_=mapping;
}
bool MacEfuseIo::accessible(uint32_t offset,size_t bytes)const{
    if(!valid()||offset>=0x1000||bytes>0x1000-offset)return false;
    const auto command=device_->configRead16(kIOPCIConfigCommand);
    return command!=0xffff&&(command&2);
}
bool MacEfuseIo::read8(uint32_t offset,uint8_t &value){
    value=0;if(!accessible(offset,1))return false;
    OSSynchronizeIO();value=*(reinterpret_cast<const volatile uint8_t *>(mapping_->getVirtualAddress())+offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);return true;
}
bool MacEfuseIo::read16(uint32_t offset,uint16_t &value){
    value=0;if((offset&1)||!accessible(offset,2))return false;
    OSSynchronizeIO();value=OSReadLittleInt16(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);return true;
}
bool MacEfuseIo::read32(uint32_t offset,uint32_t &value){
    value=0;if((offset&3)||!accessible(offset,4))return false;
    OSSynchronizeIO();value=OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);return true;
}
bool MacEfuseIo::write8(uint32_t offset,uint8_t value){
    uint8_t old=0;if(offset!=0xcc||!read8(offset,old)||old==0xff||((old^value)&~4u))return false;
    *(reinterpret_cast<volatile uint8_t *>(mapping_->getVirtualAddress())+offset)=value;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();return true;
}
bool MacEfuseIo::write16(uint32_t offset,uint16_t value){
    uint16_t old=0;if(offset!=0||!read16(offset,old)||old==0xffff||((old^value)&~0xc100u))return false;
    OSWriteLittleInt16(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),offset,value);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();return true;
}
bool MacEfuseIo::write32(uint32_t offset,uint32_t value){
    if(offset==0x30){
        // Only read requests to the 8852B DDV or PHY calibration ranges.
        const auto address=value>>16;
        if((value&~0x07ff0000u)||!((address<1216)||(address>=0x580&&address<0x600)))return false;
    }else if(offset==0x38){
        uint32_t old=0;if(!read32(offset,old)||old==0xffffffff||((old^value)&~0x80000u))return false;
    }else return false;
    if(!accessible(offset,4))return false;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),offset,value);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();return true;
}
bool MacEfuseIo::delayUs(unsigned us){if(!valid()||us>1000)return false;if(us==1000)IOSleep(1);else if(us)IODelay(us);return true;}
uint64_t MacEfuseIo::nowUs(){uint64_t ticks=0,ns=0;clock_get_uptime(&ticks);absolutetime_to_nanoseconds(ticks,&ns);return ns/1000;}
template class EfuseReader<MacEfuseIo>;
} }
