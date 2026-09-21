// SPDX-License-Identifier: BSD-3-Clause
#include "MacPciRingIo.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
namespace rtl8852be { namespace network {
MacPciRingIo::MacPciRingIo(IOPCIDevice *device,IOMemoryMap *mapping){
    if(!device||!mapping||device->configRead16(kIOPCIConfigVendorID)!=0x10ec||
       device->configRead16(kIOPCIConfigDeviceID)!=0xb852||!mapping->getVirtualAddress())return;
    auto *bar=device->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if(!bar||mapping->getPhysicalAddress()!=bar->getPhysicalAddress()||
       mapping->getLength()!=bar->getLength()||mapping->getLength()<0x10000)return;
    device_=device;mapping_=mapping;
}
bool MacPciRingIo::range(uint32_t offset,size_t bytes)const{
    return valid()&&offset<=mapping_->getLength()&&bytes<=mapping_->getLength()-offset;
}
uint16_t MacPciRingIo::command(){return valid()?device_->configRead16(kIOPCIConfigCommand):0xffff;}
uint16_t MacPciRingIo::read16(uint32_t offset){
    if((offset&1)||!range(offset,2))return 0xffff;
    return OSReadLittleInt16(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),offset);
}
uint32_t MacPciRingIo::read32(uint32_t offset){
    if((offset&3)||!range(offset,4))return 0xffffffff;
    return OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),offset);
}
bool MacPciRingIo::write16(uint32_t offset,uint16_t value){
    if(!range(offset,2)||(command()&6)!=2)return false;
    bool allowed=false;for(const auto &r:ringRegisters)if(offset==r.count)allowed=true;
    if(!allowed)return false;
    OSWriteLittleInt16(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),offset,value);
    OSMemoryBarrier();return true;
}
bool MacPciRingIo::write32(uint32_t offset,uint32_t value){
    if(!range(offset,4)||(command()&6)!=2)return false;
    bool allowed=offset==0x1014||offset==0x1018;
    // This adapter can reset BDRAM, but cannot enable either host DMA engine.
    if(offset==0x1000)allowed=(value&0x2800)==0;
    for(const auto &r:ringRegisters)if(offset==r.low||offset==r.high||(r.bdram&&offset==r.bdram))allowed=true;
    if(!allowed)return false;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),offset,value);
    OSMemoryBarrier();return true;
}
uint64_t MacPciRingIo::nowUs(){uint64_t ticks=0,ns=0;clock_get_uptime(&ticks);absolutetime_to_nanoseconds(ticks,&ns);return ns/1000;}
void MacPciRingIo::pauseUs(unsigned microseconds){if(microseconds<=1000)IODelay(microseconds);}
template class PciRingSetup<MacPciRingIo>;
} }
