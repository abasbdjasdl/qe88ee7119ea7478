// SPDX-License-Identifier: BSD-3-Clause
#include "MacFirmwareMailboxIo.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
namespace rtl8852be { namespace firmware {
MacMailboxIo::MacMailboxIo(IOPCIDevice *d,IOMemoryMap *m,IOWorkLoop *w){
    if(!d||!m||!w||!m->getVirtualAddress()||d->configRead16(kIOPCIConfigVendorID)!=0x10ec||d->configRead16(kIOPCIConfigDeviceID)!=0xb852)return;
    auto *b=d->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if(!b||m->getPhysicalAddress()!=b->getPhysicalAddress()||m->getLength()!=b->getLength()||m->getLength()<0x10000)return;
    device_=d;mapping_=m;loop_=w;
}
bool MacMailboxIo::accessible(uint32_t a,size_t n)const{
    if(!inGate()||cancelled_||(a&(n-1))||a>mapping_->getLength()||n>mapping_->getLength()-a)return false;
    const auto cmd=device_->configRead16(kIOPCIConfigCommand);return cmd!=0xffff&&(cmd&2)!=0;
}
void MacMailboxIo::barrier(){__atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();}
bool MacMailboxIo::read8(uint32_t a,uint8_t &v){
    v=0;if(!accessible(a,1)||(a!=h2cControl&&a!=c2hControl&&a!=hostCounters))return false;
    barrier();v=*(reinterpret_cast<const volatile uint8_t *>(mapping_->getVirtualAddress())+a);barrier();return true;
}
bool MacMailboxIo::read16(uint32_t a,uint16_t &v){
    v=0;if(!accessible(a,2)||a!=schedulerTx)return false;
    barrier();v=OSReadLittleInt16(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return true;
}
bool MacMailboxIo::read32(uint32_t a,uint32_t &v){
    v=0;bool allowed=a==firmwareControl;for(auto reg:c2hData)if(a==reg)allowed=true;
    if(!allowed||!accessible(a,4))return false;
    barrier();v=OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return true;
}
bool MacMailboxIo::write8(uint32_t a,uint8_t v){
    if(!accessible(a,1)||!((a==h2cControl&&v==1)||(a==c2hControl&&v==0)||a==hostCounters))return false;
    *(reinterpret_cast<volatile uint8_t *>(mapping_->getVirtualAddress())+a)=v;barrier();return true;
}
bool MacMailboxIo::write32(uint32_t a,uint32_t v){
    bool allowed=false;for(auto reg:h2cData)if(a==reg)allowed=true;
    if(!allowed||!accessible(a,4))return false;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return true;
}
uint64_t MacMailboxIo::nowUs(){uint64_t t=0,n=0;clock_get_uptime(&t);absolutetime_to_nanoseconds(t,&n);return n/1000;}
bool MacMailboxIo::delayUs(unsigned us){
    if(!inGate()||cancelled_||us>1000)return false;
    if(us==1000)IOSleep(1);else if(us)IODelay(us);return !cancelled_;
}
template class Mailbox<MacMailboxIo>;
} }
