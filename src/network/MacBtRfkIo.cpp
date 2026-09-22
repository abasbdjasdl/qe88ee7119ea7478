// SPDX-License-Identifier: BSD-3-Clause
#include "MacBtRfkIo.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
namespace rtl8852be { namespace bt {
MacBtRfkIo::MacBtRfkIo(IOPCIDevice *d,IOMemoryMap *m,IOWorkLoop *w,network::FirmwareCommandLink c):commands_(c){
    if(!d||!m||!w||!c.valid()||!m->getVirtualAddress()||d->configRead16(kIOPCIConfigVendorID)!=0x10ec||
       d->configRead16(kIOPCIConfigDeviceID)!=0xb852)return;
    auto *bar=d->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if(!bar||m->getPhysicalAddress()!=bar->getPhysicalAddress()||m->getLength()!=bar->getLength()||m->getLength()<0x10000)return;
    device_=d;mapping_=m;loop_=w;
}
bool MacBtRfkIo::accessible(uint32_t a,size_t width){
    if(!inGate()||cancelled_||(a&(width-1))||a>=0x10000||width>0x10000-a)return false;
    const auto cmd=device_->configRead16(kIOPCIConfigCommand);
    return cmd!=0xffff&&(cmd&2)&&commands_.available(commands_.owner);
}
void MacBtRfkIo::barrier(){__atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();}
bool MacBtRfkIo::read8(uint32_t a,uint8_t &v){
    v=0;if((a!=controlPathRegister&&a!=lteControl+3)||!accessible(a,1))return false;
    barrier();v=*(reinterpret_cast<const volatile uint8_t *>(mapping_->getVirtualAddress())+a);barrier();return accessible(a,1);
}
bool MacBtRfkIo::read16(uint32_t a,uint16_t &v){
    v=0;if((a!=priorityRegister&&a!=schedulerRegister)||!accessible(a,2))return false;
    barrier();v=OSReadLittleInt16(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return accessible(a,2);
}
bool MacBtRfkIo::read32(uint32_t a,uint32_t &v){
    v=0;if((a!=scoreboardRegister&&a!=firmwareControl&&a!=cmacFunctionRegister&&a!=lteReadData)||!accessible(a,4))return false;
    barrier();v=OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return accessible(a,4);
}
bool MacBtRfkIo::write8(uint32_t a,uint8_t v){
    if(a!=controlPathRegister||!accessible(a,1))return false;
    *(reinterpret_cast<volatile uint8_t *>(mapping_->getVirtualAddress())+a)=v;barrier();return accessible(a,1);
}
bool MacBtRfkIo::write16(uint32_t a,uint16_t v){
    if(a!=priorityRegister||!accessible(a,2))return false;
    OSWriteLittleInt16(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return accessible(a,2);
}
bool MacBtRfkIo::write32(uint32_t a,uint32_t v){
    const bool allowed=a==lteWriteData||(a==lteControl&&(v==0x800f0038||v==0xc00f0038))||
        (a==scoreboardRegister&&(v&0x81000000)==0x81000000);
    if(!allowed||!accessible(a,4))return false;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return accessible(a,4);
}
uint64_t MacBtRfkIo::nowUs(){uint64_t t=0,n=0;clock_get_uptime(&t);absolutetime_to_nanoseconds(t,&n);return n/1000;}
bool MacBtRfkIo::delayUs(unsigned us){
    if(us>1000||!accessible(0,1))return false;
    if(us==1000)IOSleep(1);else if(us)IODelay(us);return accessible(0,1);
}
bool MacBtRfkIo::submitH2c(network::CommandId id,bool done,const uint8_t *data,size_t length,uint8_t &sequence){
    sequence=0;if(!network::sameCommand(id,policyCommand)||!done||!data||length!=26||!accessible(0,1))return false;
    return commands_.submit(commands_.owner,id,done,data,length,sequence)&&accessible(0,1);
}
template class RfkCoordination<MacBtRfkIo>;
} }
