// SPDX-License-Identifier: BSD-3-Clause
#include "MacPciRuntimeIo.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
namespace rtl8852be { namespace network {
MacPciRuntimeIo::MacPciRuntimeIo(IOPCIDevice *d,IOMemoryMap *m){
    if(!d||!m||!m->getVirtualAddress()||d->configRead16(kIOPCIConfigVendorID)!=0x10ec||d->configRead16(kIOPCIConfigDeviceID)!=0xb852)return;
    auto *b=d->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if(!b||m->getPhysicalAddress()!=b->getPhysicalAddress()||m->getLength()!=b->getLength()||m->getLength()<0x10000)return;
    device_=d;mapping_=m;
}
bool MacPciRuntimeIo::range(uint32_t a,size_t n)const{return valid()&&a<=mapping_->getLength()&&n<=mapping_->getLength()-a;}
uint16_t MacPciRuntimeIo::command(){return valid()?device_->configRead16(kIOPCIConfigCommand):0xffff;}
bool MacPciRuntimeIo::memoryEnabled(bool requireMaster){
    const auto cmd=command();const unsigned mask=requireMaster?6u:2u;
    return cmd!=0xffff&&(cmd&mask)==mask;
}
bool MacPciRuntimeIo::writeCommand(uint16_t v){
    const auto old=command();if(old==0xffff||!(v&2)||((old^v)&~0x404u))return false;
    device_->configWrite16(kIOPCIConfigCommand,v);return command()==v;
}
uint16_t MacPciRuntimeIo::read16(uint32_t a){
    if((a&1)||!range(a,2)||!memoryEnabled())return 0xffff;
    const auto v=OSReadLittleInt16(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return v;
}
uint32_t MacPciRuntimeIo::read32(uint32_t a){
    if((a&3)||!range(a,4)||!memoryEnabled())return 0xffffffff;
    const auto v=OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return v;
}
bool MacPciRuntimeIo::write16(uint32_t a,uint16_t v){
    if(!range(a,2)||!memoryEnabled(true)||v>=64)return false;
    bool allowed=false;for(const auto &r:ringRegisters)if(a==r.index)allowed=true;if(!allowed)return false;
    OSWriteLittleInt16(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return true;
}
bool MacPciRuntimeIo::write32(uint32_t a,uint32_t v){
    if(!range(a,4)||!memoryEnabled())return false;
    bool allowed=false;
    for(unsigned i=0;i<3;++i){
        if(a==irqMasks[i])allowed=v==0||v==irqEnabled[i];
        if(a==irqStatus[i])allowed=(v&~irqEnabled[i])==0;
    }
    if(a==0x1000||a==0x1010){const auto old=read32(a),mask=a==0x1000?0x2800u:runtimeStopMask;
        allowed=old!=0xffffffff&&old!=0xdeadbeef&&((old^v)&~mask)==0;
    }
    if(!allowed)return false;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return true;
}
void MacPciRuntimeIo::barrier(){__atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();}
uint64_t MacPciRuntimeIo::nowUs(){uint64_t t=0,n=0;clock_get_uptime(&t);absolutetime_to_nanoseconds(t,&n);return n/1000;}
void MacPciRuntimeIo::pauseUs(unsigned us){if(us<=1000)IODelay(us);}
template class PciRuntime<MacPciRuntimeIo>;
} }
