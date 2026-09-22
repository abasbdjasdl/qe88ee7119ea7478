// SPDX-License-Identifier: BSD-3-Clause
#include "MacRfkIo.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
namespace rtl8852be { namespace rfk {
MacRfkIo::MacRfkIo(IOPCIDevice *d,IOMemoryMap *m,CalibrationControl c):radioIo_(d,m),radio_(radioIo_),control_(c){
    if(radioIo_.valid()){device_=d;mapping_=m;}
}
bool MacRfkIo::accessible()const{
    if(!valid()||cancelled())return false;
    const auto cmd=device_->configRead16(kIOPCIConfigCommand);
    return cmd!=0xffff&&(cmd&2)!=0;
}
bool MacRfkIo::macRead(uint32_t a,uint32_t &v){
    v=0;if(!accessible()||(a&3)||a>=0x10000)return false;
    OSSynchronizeIO();v=OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping_->getVirtualAddress()),a);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);return v!=0xffffffff&&v!=0xdeadbeef;
}
bool MacRfkIo::begin(Kind kind){
    if(active_||!accessible()||!control_.owner||!control_.begin||!control_.end)return false;
    if(!control_.begin(control_.owner,kind))return false;
    active_=true;kind_=kind;u32 fw=0,cmac=0,sys=0,tx=0;
    // Independent evidence of firmware/MAC/BB readiness and acknowledged pause.
    constexpr u32 bbResetMask=(B_AX_FEN_BBRSTB|B_AX_FEN_BB_GLB_RSTN)<<((R_AX_SYS_FUNC_EN&3)*8);
    if(!macRead(R_AX_WCPU_FW_CTRL,fw)||fieldGet(B_AX_WCPU_FWDL_STS_MASK,fw)!=7||
       !macRead(R_AX_CMAC_FUNC_EN,cmac)||!(cmac&B_AX_CMAC_EN)||
       !macRead(R_AX_SYS_FUNC_EN&~3u,sys)||(sys&bbResetMask)!=bbResetMask||
       !macRead(R_AX_CTN_TXEN,tx)||(tx&B_AX_CTN_TXEN_ALL_MASK)!=0){
        (void)end(kind,false);return false;
    }
    return true;
}
bool MacRfkIo::end(Kind kind,bool success){
    if(!active_||kind!=kind_)return false;
    // Must remain callable after cancellation or a failed MMIO transaction.
    if(!control_.end(control_.owner,kind,success))return false;
    active_=false;return true;
}
bool MacRfkIo::readRf(u8 p,u32 a,u32 m,u32 &v){v=0;return active_&&accessible()&&radio_.readRf(p,a,m,v);}
bool MacRfkIo::writeRf(u8 p,u32 a,u32 m,u32 v){return active_&&accessible()&&radio_.writeRf(p,a,m,v);}
bool MacRfkIo::readBb(u32 a,u32 &v){v=0;return active_&&accessible()&&radio_.readBaseband(a,v);}
bool MacRfkIo::writeBb(u32 a,u32 v){return active_&&accessible()&&radio_.writeBaseband(a,v);}
bool MacRfkIo::writeMac(u32 a,u32 v){
    if(!active_||!accessible()||a!=R_AX_PHYREG_SET||v!=0xf)return false;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();return true;
}
bool MacRfkIo::drain(){return active_&&accessible()&&radio_.drain();}
uint64_t MacRfkIo::nowUs(){uint64_t t=0,n=0;clock_get_uptime(&t);absolutetime_to_nanoseconds(t,&n);return n/1000;}
bool MacRfkIo::delayUs(unsigned us){return active_&&accessible()&&radioIo_.delayUs(us);}
template class Initialization<MacRfkIo>;
} }
