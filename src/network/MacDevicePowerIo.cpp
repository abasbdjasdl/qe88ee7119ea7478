// SPDX-License-Identifier: BSD-3-Clause
#include "MacDevicePowerIo.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
namespace rtl8852be { namespace powerseq {
MacDevicePowerIo::MacDevicePowerIo(IOPCIDevice *d,IOMemoryMap *m,IOWorkLoop *w){
    if(!d||!m||!w||!m->getVirtualAddress()||d->configRead16(kIOPCIConfigVendorID)!=0x10ec||
       d->configRead16(kIOPCIConfigDeviceID)!=0xb852)return;
    auto *bar=d->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if(!bar||m->getPhysicalAddress()!=bar->getPhysicalAddress()||m->getLength()!=bar->getLength()||m->getLength()<0x10000)return;
    device_=d;mapping_=m;loop_=w;
}
uint16_t MacDevicePowerIo::command(){return valid()?device_->configRead16(kIOPCIConfigCommand):0xffff;}
bool MacDevicePowerIo::gateAndPci(){const auto cmd=command();return inGate()&&cmd!=0xffff&&(cmd&6)==2;}
bool MacDevicePowerIo::accessible(uint32_t a,unsigned width,bool write){
    return gateAndPci()&&!(a&(width-1))&&a<0x10000&&width<=0x10000-a&&powerAddress(a,width,write);
}
void MacDevicePowerIo::barrier(){__atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();}
bool MacDevicePowerIo::read8(uint32_t a,uint8_t &v){
    v=0;if(!accessible(a,1,false))return false;barrier();
    v=*(reinterpret_cast<const volatile uint8_t *>(mapping_->getVirtualAddress())+a);barrier();return gateAndPci();
}
bool MacDevicePowerIo::read16(uint32_t a,uint16_t &v){
    v=0;if(!accessible(a,2,false))return false;barrier();
    v=OSReadLittleInt16(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return gateAndPci();
}
bool MacDevicePowerIo::read32(uint32_t a,uint32_t &v){
    v=0;if(!accessible(a,4,false))return false;barrier();
    v=OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return gateAndPci();
}
bool MacDevicePowerIo::write8(uint32_t a,uint8_t v){
    if(!accessible(a,1,true)||(a==R_AX_SCOREBOARD+3&&v!=MAC_AX_NOTIFY_TP_MAJOR&&v!=MAC_AX_NOTIFY_PWR_MAJOR))return false;
    *(reinterpret_cast<volatile uint8_t *>(mapping_->getVirtualAddress())+a)=v;barrier();return gateAndPci();
}
bool MacDevicePowerIo::write16(uint32_t a,uint16_t v){
    if(!accessible(a,2,true))return false;
    OSWriteLittleInt16(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return gateAndPci();
}
bool MacDevicePowerIo::write32(uint32_t a,uint32_t v){
    if(!accessible(a,4,true)||(a==R_AX_WLAN_XTAL_SI_CTRL&&!powerSiCommand(v)))return false;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return gateAndPci();
}
uint64_t MacDevicePowerIo::nowUs(){uint64_t t=0,n=0;clock_get_uptime(&t);absolutetime_to_nanoseconds(t,&n);return n/1000;}
bool MacDevicePowerIo::delayUs(unsigned us){
    if(us>1000||!gateAndPci())return false;
    if(us==1000)IOSleep(1);else if(us)IODelay(us);return gateAndPci();
}
template class DevicePower<MacDevicePowerIo>;
} }
