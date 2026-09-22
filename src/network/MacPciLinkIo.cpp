// SPDX-License-Identifier: BSD-3-Clause
#include "MacPciLinkIo.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
namespace rtl8852be { namespace network { namespace pcilink {
MacPciLinkIo::MacPciLinkIo(IOPCIDevice *d,IOMemoryMap *m,IOWorkLoop *w){
    if(!d||!m||!w||d->configRead16(kIOPCIConfigVendorID)!=0x10ec||
       d->configRead16(kIOPCIConfigDeviceID)!=0xb852||!m->getVirtualAddress())return;
    auto *bar=d->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if(!bar||m->getPhysicalAddress()!=bar->getPhysicalAddress()||m->getLength()!=bar->getLength()||m->getLength()<0x10000)return;
    device_=d;mapping_=m;loop_=w;
}
bool MacPciLinkIo::accessible(uint32_t a,size_t width){
    if(!inGate()||cancelled_||!width||(a&(width-1))||a>=0x10000||width>0x10000-a)return false;
    const auto command=device_->configRead16(kIOPCIConfigCommand);return command!=0xffff&&(command&2);
}
void MacPciLinkIo::barrier(){__atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();}
static bool wordRegister(uint32_t a){
    switch(a){case 4:case 0x70:case 0x74:case 0x1008:case 0x13f0:case 0x11d8:
    case 0x11c0:case 0x1000:case 0x8410:case 0x8414:case 0x8418:case 0x841c:
    case 0x8810:case 0x9a00:return true;default:return false;}
}
bool MacPciLinkIo::read8(uint32_t a,uint8_t &v){
    v=0;if((a!=dbiFlag+2&&(a<dbiReadData||a>dbiReadData+3))||!accessible(a,1))return false;
    barrier();v=*(reinterpret_cast<const volatile uint8_t *>(mapping_->getVirtualAddress())+a);barrier();return accessible(a,1);
}
bool MacPciLinkIo::read16(uint32_t a,uint16_t &v){
    v=0;if((a!=mdioConfig&&a!=mdioReadData)||!accessible(a,2))return false;
    barrier();v=OSReadLittleInt16(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return accessible(a,2);
}
bool MacPciLinkIo::read32(uint32_t a,uint32_t &v){
    v=0;if(!wordRegister(a)||!accessible(a,4))return false;
    barrier();v=OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return accessible(a,4);
}
bool MacPciLinkIo::write8(uint32_t a,uint8_t v){
    const bool allowed=(a==mdioConfig&&v<=31)||(a==dbiFlag+2&&(v==1||v==2))||(a>=dbiWriteData&&a<=dbiWriteData+3);
    if(!allowed||!accessible(a,1))return false;
    *(reinterpret_cast<volatile uint8_t *>(mapping_->getVirtualAddress())+a)=v;barrier();return accessible(a,1);
}
bool MacPciLinkIo::write16(uint32_t a,uint16_t v){
    if((a!=mdioConfig&&a!=mdioWriteData&&a!=dbiFlag)||!accessible(a,2))return false;
    OSWriteLittleInt16(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return accessible(a,2);
}
bool MacPciLinkIo::write32(uint32_t a,uint32_t v){
    if(!wordRegister(a)||!accessible(a,4))return false;
    // The only INIT_CFG1 change this adapter permits is KEEP_REG. Never use it
    // to enable HCI/DMA or reset BDRAM behind the DMA owner's back.
    if(a==0x1000){uint32_t old=0;if(!read32(a,old)||((old^v)&~0xc00000U))return false;}
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return accessible(a,4);
}
bool MacPciLinkIo::readConfig8(uint16_t a,uint8_t &v){
    v=0;if((a!=0x82&&a!=0x719)||!accessible(0,1))return false;
    v=device_->extendedConfigRead8(a);barrier();
    // All-ones is ambiguous on the native config API (no status return). The
    // initializer then uses its real DBI engine, not a guessed default byte.
    return v!=0xff&&accessible(0,1);
}
bool MacPciLinkIo::writeConfig8(uint16_t a,uint8_t v){
    if(a!=0x719||!accessible(0,1))return false;
    device_->extendedConfigWrite8(a,v);barrier();
    return accessible(0,1)&&device_->extendedConfigRead8(a)==v;
}
uint64_t MacPciLinkIo::nowUs(){uint64_t t=0,n=0;clock_get_uptime(&t);absolutetime_to_nanoseconds(t,&n);return n/1000;}
bool MacPciLinkIo::delayUs(unsigned us){
    if(us>1000||!accessible(0,1))return false;
    if(us==1000)IOSleep(1);else if(us)IODelay(us);return accessible(0,1);
}
template class Initialization<MacPciLinkIo>;
} } }
