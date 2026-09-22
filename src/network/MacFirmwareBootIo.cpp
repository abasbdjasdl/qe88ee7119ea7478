// SPDX-License-Identifier: BSD-3-Clause
#include "MacFirmwareBootIo.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
namespace rtl8852be { namespace firmwareboot {
MacFirmwareBootIo::MacFirmwareBootIo(IOPCIDevice *d,IOMemoryMap *m,IOWorkLoop *w){
    if(!d||!m||!w||!m->getVirtualAddress()||d->configRead16(kIOPCIConfigVendorID)!=0x10ec||d->configRead16(kIOPCIConfigDeviceID)!=0xb852)return;
    auto *b=d->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if(!b||m->getPhysicalAddress()!=b->getPhysicalAddress()||m->getLength()!=b->getLength()||m->getLength()<0x10000)return;
    device_=d;mapping_=m;loop_=w;
}
uint16_t MacFirmwareBootIo::command(){return valid()?device_->configRead16(kIOPCIConfigCommand):0xffff;}
bool MacFirmwareBootIo::access(uint32_t a,unsigned n,bool masterAllowed){
    if(!inGate()||(!cleanup_&&cancelled_)||(a&(n-1))||a>=0x10000||n>0x10000-a)return false;
    const auto c=command();return c!=0xffff&&(c&2)&&(masterAllowed||!(c&4));
}
void MacFirmwareBootIo::barrier(){__atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();}
bool MacFirmwareBootIo::read16(uint32_t a,uint16_t &v){v=0;
    if((!prepAddress(a,2,false)&&!transport::uploadAddress16(a))||!access(a,2,true))return false;
    barrier();v=OSReadLittleInt16(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return access(a,2,true);}
bool MacFirmwareBootIo::read32(uint32_t a,uint32_t &v){v=0;
    if((!prepAddress(a,4,false)&&!transport::uploadAddress32(a)&&a!=0x1080)||!access(a,4,true))return false;
    barrier();v=OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return access(a,4,true);}
uint16_t MacFirmwareBootIo::read16(uint32_t a){uint16_t v=0;return read16(a,v)?v:0xffff;}
uint32_t MacFirmwareBootIo::read32(uint32_t a){uint32_t v=0;return read32(a,v)?v:0xffffffff;}
bool MacFirmwareBootIo::write16(uint32_t a,uint16_t v){
    if(!prepAddress(a,2,true)||!access(a,2,false))return false;
    OSWriteLittleInt16(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return access(a,2,false);}
bool MacFirmwareBootIo::write32(uint32_t a,uint32_t v){
    if(!prepAddress(a,4,true)||!access(a,4,false))return false;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return access(a,4,false);}
bool MacFirmwareBootIo::uploadWrite16(uint32_t a,uint16_t v){
    if(!transport::uploadAddress16(a)||!access(a,2,true))return false;
    OSWriteLittleInt16(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return access(a,2,true);}
bool MacFirmwareBootIo::uploadWrite32(uint32_t a,uint32_t v){
    if(!transport::uploadAddress32(a)||!access(a,4,true))return false;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return access(a,4,true);}
bool MacFirmwareBootIo::uploadBusMaster(bool enable){
    if(!access(0,4,true))return false;const auto c=command();const uint16_t desired=enable?uint16_t(c|4):uint16_t(c&~4u);
    device_->configWrite16(kIOPCIConfigCommand,desired);barrier();return command()==desired;
}
bool MacFirmwareBootIo::interruptsSafe(){
    return access(0,4,true)&&read32(0x1a0)==0&&read32(0x10b0)==0&&read32(0x13b0)==0;
}
uint64_t MacFirmwareBootIo::nowUs(){uint64_t t=0,n=0;clock_get_uptime(&t);absolutetime_to_nanoseconds(t,&n);return n/1000;}
bool MacFirmwareBootIo::delayUs(unsigned us){if(us>1000||!access(0,4,true))return false;
    if(us==1000)IOSleep(1);else if(us)IODelay(us);return access(0,4,true);}
template class Preparation<MacFirmwareBootIo>;
} }
