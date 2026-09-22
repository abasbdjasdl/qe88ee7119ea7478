// SPDX-License-Identifier: BSD-3-Clause
#include "MacRadioIo.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
namespace rtl8852be { namespace network {
MacRadioIo::MacRadioIo(IOPCIDevice *device,IOMemoryMap *mapping,RadioAccessGuard guard):guard_(guard){
    if(bool(guard.owner)!=bool(guard.check))return;
    if(!device||!mapping||device->configRead16(kIOPCIConfigVendorID)!=0x10ec||
       device->configRead16(kIOPCIConfigDeviceID)!=0xb852||!mapping->getVirtualAddress())return;
    auto *bar=device->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if(!bar||mapping->getPhysicalAddress()!=bar->getPhysicalAddress()||
       mapping->getLength()!=bar->getLength()||mapping->getLength()<0x20000)return;
    device_=device;mapping_=mapping;
}
bool MacRadioIo::accessible(uint32_t offset)const{
    if(!valid()||stopped_||(offset&3)||offset<0x10000||offset>=0x20000)return false;
    const auto command=device_->configRead16(kIOPCIConfigCommand);
    return command!=0xffff&&(command&2)&&(!guard_.check||guard_.check(guard_.owner));
}
bool MacRadioIo::read32(uint32_t offset,uint32_t &value){
    value=0;if(!accessible(offset))return false;
    OSSynchronizeIO();value=OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if(value==0xffffffff||!accessible(offset)){value=0;return false;}return true;
}
bool MacRadioIo::write32(uint32_t offset,uint32_t value){
    if(!accessible(offset))return false;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),offset,value);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();return accessible(offset);
}
bool MacRadioIo::delayUs(unsigned us){
    if(us>50000||!accessible(0x10000))return false;
    // Long table delays may sleep only on the owner's thread/workloop, never
    // in an interrupt filter. Bound individual waits and check cancellation.
    while(us>=1000){IOSleep(1);us-=1000;if(!accessible(0x10000))return false;}
    if(us)IODelay(us);return accessible(0x10000);
}
template class RadioAccess<MacRadioIo>;
template class RadioInitialization<MacRadioIo>;
} }
