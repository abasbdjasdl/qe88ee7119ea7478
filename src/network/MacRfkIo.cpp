// SPDX-License-Identifier: BSD-3-Clause
#include "MacRfkIo.hpp"
#include "ChannelProgramming.hpp"
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
    if(active_||!accessible()||!control_.owner||!control_.begin||!control_.end||!control_.recover)return false;
    if((kind==Kind::iqk||kind==Kind::tssi)&&!control_.oneshot)return false;
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
    if(txArmed_&&!stopCalibrationTx())return false;
    if(!success&&modified_){
        // A bounded poll failure cannot prove a stopped NCTL/KIP/RF engine.
        // Retain ownership until the controller has verified reset/quiescence.
        if(!control_.recover(control_.owner,kind))return false;
        modified_=false;
    }
    if(oneshotActive_&&!oneshot(kind,oneshotMap_,false))return false;
    u32 tx=0;
    if(kind==Kind::channel&&success&&(!macRead(R_AX_CTN_TXEN,tx)||(tx&B_AX_CTN_TXEN_ALL_MASK))){modified_=true;return false;}
    if(!control_.end(control_.owner,kind,success))return false;
    if(kind==Kind::channel&&success&&(!macRead(R_AX_CTN_TXEN,tx)||(tx&B_AX_CTN_TXEN_ALL_MASK))){modified_=true;return false;}
    active_=false;modified_=false;return true;
}
bool MacRfkIo::oneshot(Kind kind,u8 phyMap,bool start){
    if(!active_||kind!=kind_||!control_.oneshot)return false;
    if(start){
        if(oneshotActive_||!accessible()||!control_.oneshot(control_.owner,kind,phyMap,true))return false;
        oneshotActive_=true;oneshotMap_=phyMap;return true;
    }
    if(txArmed_||!oneshotActive_||phyMap!=oneshotMap_||!control_.oneshot(control_.owner,kind,phyMap,false))return false;
    oneshotActive_=false;return true;
}
bool MacRfkIo::armCalibrationTx(){
    if(!active_||kind_!=Kind::tssi||txArmed_||!oneshotActive_||!accessible())return false;
    // Mark potential emission before the first PMAC write, not after success.
    txArmed_=true;return true;
}
bool MacRfkIo::stopCalibrationTx(){
    if(!txArmed_)return true;
    if(!active_||kind_!=Kind::tssi||!valid())return false;
    const auto cmd=device_->configRead16(kIOPCIConfigCommand);
    if(cmd==0xffff||!(cmd&2))return false;
    // Narrow cleanup bypasses cancellation and the calibration fault latch.
    // Only emission-enable bits are cleared. Preserve all timing/other fields.
    constexpr u32 offset=0x10000+R_PMAC_TX_PRD;
    constexpr u32 enables=B_PMAC_CTX_EN|B_PMAC_PTX_EN;
    auto *base=reinterpret_cast<volatile void *>(mapping_->getVirtualAddress());
    OSSynchronizeIO();const u32 old=OSReadLittleInt32(base,offset);
    if(old==0xffffffff||old==0xdeadbeef)return false;
    OSWriteLittleInt32(base,offset,old&~enables);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();
    const u32 readback=OSReadLittleInt32(base,offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if(readback==0xffffffff||readback==0xdeadbeef||(readback&enables))return false;
    txArmed_=false;return true;
}
bool MacRfkIo::readRf(u8 p,u32 a,u32 m,u32 &v){v=0;return active_&&accessible()&&radio_.readRf(p,a,m,v);}
bool MacRfkIo::writeRf(u8 p,u32 a,u32 m,u32 v){
    if(!active_||!accessible())return false;modified_=true;return radio_.writeRf(p,a,m,v);}
bool MacRfkIo::readBb(u32 a,u32 &v){v=0;return active_&&accessible()&&radio_.readBaseband(a,v);}
bool MacRfkIo::writeBb(u32 a,u32 v){
    if(!active_||!accessible())return false;modified_=true;return radio_.writeBaseband(a,v);}
bool MacRfkIo::writeMac(u32 a,u32 v){
    if(!active_||!accessible()||a!=R_AX_PHYREG_SET||v!=0xf)return false;
    modified_=true;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();return true;
}
bool MacRfkIo::readChannelMac8(u32 a,u8 &v){
    v=0;if(!active_||kind_!=Kind::channel||!accessible()||!channel::macByteAddress(a))return false;
    OSSynchronizeIO();v=*(reinterpret_cast<const volatile u8 *>(mapping_->getVirtualAddress())+a);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);return true;
}
bool MacRfkIo::writeChannelMac8(u32 a,u8 v){
    if(!active_||kind_!=Kind::channel||!accessible()||!channel::macByteAddress(a))return false;
    modified_=true;*(reinterpret_cast<volatile u8 *>(mapping_->getVirtualAddress())+a)=v;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();return true;
}
bool MacRfkIo::readChannelMac32(u32 a,u32 &v){
    v=0;return active_&&kind_==Kind::channel&&channel::macWordAddress(a,false)&&macRead(a,v);
}
bool MacRfkIo::writeChannelMac32(u32 a,u32 v){
    if(!active_||kind_!=Kind::channel||!accessible()||!channel::macWordAddress(a,true))return false;
    modified_=true;OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping_->getVirtualAddress()),a,v);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();return true;
}
bool MacRfkIo::drain(){return active_&&accessible()&&radio_.drain();}
uint64_t MacRfkIo::nowUs(){uint64_t t=0,n=0;clock_get_uptime(&t);absolutetime_to_nanoseconds(t,&n);return n/1000;}
bool MacRfkIo::delayUs(unsigned us){return active_&&accessible()&&radioIo_.delayUs(us);}
template class Initialization<MacRfkIo>;
} }
template class rtl8852be::channel::ChannelProgramming<rtl8852be::rfk::MacRfkIo>;
