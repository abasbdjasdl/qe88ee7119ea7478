// SPDX-License-Identifier: BSD-3-Clause
#include "MacInitializationIo.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
namespace rtl8852be { namespace macinit {
MacInitializationIo::MacInitializationIo(IOPCIDevice *d,IOMemoryMap *m){
    if(!d||!m||!m->getVirtualAddress()||d->configRead16(kIOPCIConfigVendorID)!=0x10ec||
       d->configRead16(kIOPCIConfigDeviceID)!=0xb852)return;
    auto *b=d->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if(!b||m->getPhysicalAddress()!=b->getPhysicalAddress()||m->getLength()!=b->getLength()||m->getLength()<0x10000)return;
    device_=d;mapping_=m;
}
uint16_t MacInitializationIo::command(){return valid()?device_->configRead16(kIOPCIConfigCommand):0xffff;}
bool MacInitializationIo::accessible(uint32_t a,size_t n,bool write){
    if(!valid()||cancelled_||(a&(n-1))||a>mapping_->getLength()||n>mapping_->getLength()-a)return false;
    if((command()&6)!=2)return false;
    return write?initWriteAddress(a):initAddress(a);
}
void MacInitializationIo::barrier(){__atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();}
bool MacInitializationIo::readCmac(uint32_t a,unsigned width,uint32_t &v){
    // rtw89 pci.c read32_cmac: CMAC can return DEAD while its clock is gated.
    // Authorize the requested register/width, then read its aligned word so
    // byte/halfword reads cannot disguise DEAD as a plausible field value.
    v=0;if(!accessible(a,width,false)||a<0xc000||a>=0xe000)return false;
    const auto aligned=a&~3u;
    if(aligned+4>mapping_->getLength())return false;
    for(unsigned retry=0;retry<=10;++retry){
        if(!accessible(a,width,false))return false;
        barrier();const auto word=OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),aligned);barrier();
        if(word!=0xdeadbeef){v=word>>((a&3)*8);return accessible(a,width,false);}
        v=word;if(retry==10||!write32(R_AX_CK_EN,0xffffffff))return false;
    }
    return false;
}
bool MacInitializationIo::read8(uint32_t a,uint8_t &v){
    if(a>=0xc000&&a<0xe000){uint32_t word=0;const bool ok=readCmac(a,1,word);v=uint8_t(word);return ok;}
    v=0;if(!accessible(a,1,false))return false;barrier();
    v=*(reinterpret_cast<const volatile uint8_t *>(mapping_->getVirtualAddress())+a);barrier();return true;
}
bool MacInitializationIo::read16(uint32_t a,uint16_t &v){
    if(a>=0xc000&&a<0xe000){uint32_t word=0;const bool ok=readCmac(a,2,word);v=uint16_t(word);return ok;}
    v=0;if(!accessible(a,2,false))return false;barrier();
    v=OSReadLittleInt16(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return true;
}
bool MacInitializationIo::read32(uint32_t a,uint32_t &v){
    if(a>=0xc000&&a<0xe000)return readCmac(a,4,v);
    v=0;if(!accessible(a,4,false))return false;barrier();
    v=OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return true;
}
bool MacInitializationIo::write8(uint32_t a,uint8_t v){
    if(!accessible(a,1,true))return false;
    *(reinterpret_cast<volatile uint8_t *>(mapping_->getVirtualAddress())+a)=v;barrier();return true;
}
bool MacInitializationIo::write16(uint32_t a,uint16_t v){
    if(!accessible(a,2,true))return false;
    OSWriteLittleInt16(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return true;
}
bool MacInitializationIo::write32(uint32_t a,uint32_t v){
    if(!accessible(a,4,true))return false;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return true;
}
uint64_t MacInitializationIo::nowUs(){uint64_t t=0,n=0;clock_get_uptime(&t);absolutetime_to_nanoseconds(t,&n);return n/1000;}
bool MacInitializationIo::delayUs(unsigned us){
    if(!valid()||cancelled_||us>50000)return false;
    while(us>=1000){if(cancelled_)return false;IOSleep(1);us-=1000;}
    if(us)IODelay(us);return !cancelled_;
}
template class MacInitialization<MacInitializationIo>;
} }
