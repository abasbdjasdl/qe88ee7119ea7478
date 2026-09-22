// SPDX-License-Identifier: BSD-3-Clause
#include "MacBtInitializationIo.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
namespace rtl8852be { namespace bt { namespace initialization {
MacBtInitializationIo::MacBtInitializationIo(IOPCIDevice *d,IOMemoryMap *m,IOWorkLoop *w,network::FirmwareCommandLink c):
    commands_(c),radioIo_(d,m,{this,radioGuard}),radio_(radioIo_){
    if(!d||!m||!w||!c.valid()||!radioIo_.valid())return;
    device_=d;mapping_=m;loop_=w;
}
uint64_t MacBtInitializationIo::nowUs(){uint64_t t=0,n=0;clock_get_uptime(&t);absolutetime_to_nanoseconds(t,&n);return n/1000;}
bool MacBtInitializationIo::accessible(uint32_t a,size_t width){
    if(!inGate()||stopped_||!width||(a&(width-1))||a>=0x10000||width>0x10000-a)return false;
    const auto now=nowUs();if(!clockStarted_){start_=now;clockStarted_=true;}
    if(now<last_||now-start_>=2000000){cancel();return false;}last_=now;
    const auto command=device_->configRead16(kIOPCIConfigCommand);
    return command!=0xffff&&(command&2)&&commands_.available(commands_.owner);
}
bool MacBtInitializationIo::radioGuard(void *p){return static_cast<MacBtInitializationIo *>(p)->accessible(0,1);}
void MacBtInitializationIo::barrier(){__atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();}
static bool byteRegister(uint32_t a){
    switch(a){case 0x40:case 0x41:case controlPathRegister:case 0xda20:case 0xda35:
    case 0xda40:case 0xda42:case 0xcc07:case 0xda4c:case 0xda6c:return true;default:return false;}
}
static bool wordRegister(uint32_t a){return a==0xc340||a==priorityRegister;}
static bool dwordRegister(uint32_t a){
    return a==0xda30||a==0xda10||a==0xda2c||a==0xda40||a==0xd200||a==0xd220;
}
bool MacBtInitializationIo::read8(uint32_t a,uint8_t &v){
    v=0;if((!byteRegister(a)&&a!=lteControl+3)||!accessible(a,1))return false;
    barrier();v=*(reinterpret_cast<const volatile uint8_t *>(mapping_->getVirtualAddress())+a);barrier();return accessible(a,1);
}
bool MacBtInitializationIo::read16(uint32_t a,uint16_t &v){
    v=0;if((!wordRegister(a)&&a!=schedulerRegister)||!accessible(a,2))return false;
    barrier();v=OSReadLittleInt16(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return accessible(a,2);
}
bool MacBtInitializationIo::read32(uint32_t a,uint32_t &v){
    v=0;if((!dwordRegister(a)&&a!=firmwareControl&&a!=cmacFunctionRegister&&a!=0x80&&
       a!=scoreboardRegister&&a!=lteReadData)||!accessible(a,4))return false;
    barrier();v=OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);barrier();return accessible(a,4);
}
bool MacBtInitializationIo::write8(uint32_t a,uint8_t v){
    if(!byteRegister(a)||!accessible(a,1))return false;
    *(reinterpret_cast<volatile uint8_t *>(mapping_->getVirtualAddress())+a)=v;barrier();return accessible(a,1);
}
bool MacBtInitializationIo::write16(uint32_t a,uint16_t v){
    if(!wordRegister(a)||!accessible(a,2))return false;
    OSWriteLittleInt16(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return accessible(a,2);
}
bool MacBtInitializationIo::write32(uint32_t a,uint32_t v){
    const bool lte=a==lteControl&&(v==0x800f0038||v==0xc00f0038||v==0x800f003c||v==0xc00f003c);
    const bool sb=a==scoreboardRegister&&(v&0x00ffffff)==initialScoreboard&&(v&0x81000000)==0x81000000;
    if((!dwordRegister(a)&&a!=lteWriteData&&!lte&&!sb)||!accessible(a,4))return false;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);barrier();return accessible(a,4);
}
bool MacBtInitializationIo::readRf(uint8_t path,uint32_t a,uint32_t mask,uint32_t &v){
    v=0;if(path>1||(a!=2&&a!=0xef)||mask!=0xfffff||!accessible(0,1))return false;
    return radio_.readRf(path,a,mask,v)&&accessible(0,1);
}
bool MacBtInitializationIo::writeRf(uint8_t path,uint32_t a,uint32_t mask,uint32_t v){
    const bool allowed=(a==2&&v==0)||(a==0xef&&(v==0||v==0x20000))||
        (a==0x33&&(v==0||v==2))||(a==0x3f&&(v==0x5df||v==0x5ff||v==0x55f));
    if(path>1||mask!=0xfffff||!allowed||!accessible(0,1))return false;
    return radio_.writeRf(path,a,mask,v)&&radio_.drain()&&accessible(0,1);
}
bool MacBtInitializationIo::delayUs(unsigned us){
    if(us>1000||!accessible(0,1))return false;
    if(us==1000)IOSleep(1);else if(us)IODelay(us);return accessible(0,1);
}
bool MacBtInitializationIo::submitH2c(network::CommandId id,bool done,const uint8_t *p,size_t length,uint8_t &sequence){
    sequence=0;if(!done||!p||!accessible(0,1))return false;
    const bool allowed=(network::sameCommand(id,monitorCommand)&&length==130&&(p[0]==1||p[0]==2)&&p[1]==16)||
        (network::sameCommand(id,slotsCommand)&&length==146&&p[0]==1&&p[1]==18)||
        (network::sameCommand(id,driverCommand)&&((length==14&&p[0]==0&&p[1]==12)||(length==6&&p[0]==6&&p[1]==4)))||
        (network::sameCommand(id,policyCommand)&&length==26&&p[0]==0&&p[1]==12&&p[2]==3&&p[14]==1&&p[15]==10&&p[16]==1);
    return allowed&&commands_.submit(commands_.owner,id,done,p,length,sequence)&&accessible(0,1);
}
template class Initialization<MacBtInitializationIo>;
} } }
