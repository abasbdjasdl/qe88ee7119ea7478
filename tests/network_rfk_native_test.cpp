// SPDX-License-Identifier: BSD-3-Clause
// Executes native MMIO/cleanup source against an explicit IOKit register model.
#include "network_rfk_fakes/Fake.hpp"
#include "../src/network/MacRadioIo.cpp"
#include "../src/network/MacRfkIo.cpp"
#include <cstdio>
#include <initializer_list>
namespace r=rtl8852be::rfk;
struct Owner {
    unsigned beginCount{},endCount{},shotCount{},recoverCount{},checkCount{},failCheckAt{};
    bool failEnd{},failShot{},failRecover{};uint64_t deadline=UINT64_MAX;
    IOMemoryMap *resumeMap{};
    static bool begin(void *o,r::Kind){++static_cast<Owner *>(o)->beginCount;return true;}
    static bool end(void *o,r::Kind kind,bool success){auto &x=*static_cast<Owner *>(o);++x.endCount;
        if(kind==r::Kind::channel&&success&&x.resumeMap)x.resumeMap->set(r::R_AX_CTN_TXEN,1);return !x.failEnd;}
    static bool shot(void *o,r::Kind,uint8_t,bool){auto &x=*static_cast<Owner *>(o);++x.shotCount;return !x.failShot;}
    static bool recover(void *o,r::Kind){auto &x=*static_cast<Owner *>(o);++x.recoverCount;return !x.failRecover;}
    static bool check(void *o,r::Kind){auto &x=*static_cast<Owner *>(o);++x.checkCount;
        return fakeRfk::time<x.deadline&&(!x.failCheckAt||x.checkCount<x.failCheckAt);}
    r::CalibrationControl control(){return {this,begin,end,shot,recover,check};}
};
struct Fixture {
    IOPCIDevice device;IOMemoryMap map;Owner owner;r::MacRfkIo io;
    Fixture():io(&device,&map,owner.control()){
        fakeRfk::reset();map.set(r::R_AX_WCPU_FW_CTRL,r::fieldPrep(r::B_AX_WCPU_FWDL_STS_MASK,7));
        map.set(r::R_AX_CMAC_FUNC_EN,r::B_AX_CMAC_EN);
        map.set(r::R_AX_SYS_FUNC_EN&~3u,(r::B_AX_FEN_BBRSTB|r::B_AX_FEN_BB_GLB_RSTN)<<16);
    }
    void arm(){assert(io.begin(r::Kind::tssi));assert(io.oneshot(r::Kind::tssi,0x53,true));assert(io.armCalibrationTx());
        assert(io.writeBb(r::R_PMAC_TX_PRD,0x56780000|r::B_PMAC_CTX_EN|r::B_PMAC_PTX_EN));fakeRfk::accesses=0;}
};
int main(){
    {Fixture f;auto ctl=f.owner.control();ctl.check=nullptr;r::MacRfkIo io(&f.device,&f.map,ctl);
        assert(!io.begin(r::Kind::iqk)&&!f.owner.beginCount);}
    {Fixture f;assert(f.io.begin(r::Kind::dpk));assert(f.io.writeBb(r::R_NCTL_CFG,0x1019));
        f.owner.deadline=fakeRfk::time+2500;const auto before=fakeRfk::time;
        assert(!f.io.delayUs(50000)&&fakeRfk::time-before==3000);
        const auto accesses=fakeRfk::accesses;f.owner.deadline=UINT64_MAX;
        assert(!f.io.writeBb(r::R_NCTL_CFG,0)&&fakeRfk::accesses==accesses); // expired lease stays lost
        assert(!f.io.end(r::Kind::dpk,true)&&f.io.leaseActive()&&!f.owner.endCount);
        f.owner.failRecover=true;assert(!f.io.end(r::Kind::dpk,false)&&f.io.leaseActive());
        f.owner.failRecover=false;assert(f.io.end(r::Kind::dpk,false)&&f.owner.recoverCount==2);}
    {Fixture f;assert(f.io.begin(r::Kind::iqk));f.map.set(0x1174c,0x03000000);
        f.owner.deadline=fakeRfk::time+3;const auto before=fakeRfk::time;uint32_t value=99;
        assert(!f.io.readRf(0,0,0xfffff,value)&&value==0&&fakeRfk::time-before==3);
        assert(f.io.end(r::Kind::iqk,false)&&f.owner.recoverCount==1);}
    {Fixture f;assert(f.io.begin(r::Kind::dpk));f.owner.failCheckAt=f.owner.checkCount+3;
        // Outer and inner prechecks pass; the post-MMIO lease check fails.
        assert(!f.io.writeBb(r::R_NCTL_CFG,0x1019));
        assert(f.map.get(0x10000+r::R_NCTL_CFG)==0x1019);
        f.owner.failRecover=true;assert(!f.io.end(r::Kind::dpk,false)&&f.io.leaseActive());
        f.owner.failRecover=false;assert(f.io.end(r::Kind::dpk,false));}
    {Fixture f;f.arm();f.owner.deadline=fakeRfk::time;
        assert(!f.io.writeBb(r::R_NCTL_CFG,0x1019));assert(f.io.stopCalibrationTx());
        assert(!f.io.calibrationTxArmed()&&f.io.leaseActive());
        assert(f.io.end(r::Kind::tssi,false)&&f.owner.recoverCount==1);}
    {Fixture f;assert(!f.io.armCalibrationTx());assert(f.io.stopCalibrationTx()&&!fakeRfk::accesses);f.arm();
        assert(!f.io.oneshot(r::Kind::tssi,0x53,false)&&f.owner.shotCount==1);
        f.io.cancel();assert(!f.io.writeBb(0x100,1));assert(f.io.stopCalibrationTx());
        assert(!f.io.calibrationTxArmed()&&f.map.get(0x10000+r::R_PMAC_TX_PRD)==0x56780000);
        assert(f.io.end(r::Kind::tssi,false)&&!f.io.leaseActive()&&f.owner.shotCount==2&&f.owner.endCount==1);}
    for(unsigned n:{1u,3u}){Fixture f;f.arm();fakeRfk::badRead=n;assert(!f.io.stopCalibrationTx()&&f.io.calibrationTxArmed());
        fakeRfk::badRead=0;assert(f.io.end(r::Kind::tssi,false)&&!f.io.calibrationTxArmed());}
    {Fixture f;f.arm();fakeRfk::ignoredWrite=2;assert(!f.io.stopCalibrationTx()&&f.io.calibrationTxArmed());
        fakeRfk::ignoredWrite=0;assert(f.io.end(r::Kind::tssi,false));}
    for(unsigned cmd:{0u,0xffffu}){Fixture f;f.arm();f.device.command=cmd;
        assert(!f.io.end(r::Kind::tssi,false)&&f.io.leaseActive()&&f.io.calibrationTxArmed());
        assert(!f.owner.endCount&&f.owner.shotCount==1&&!fakeRfk::accesses);
        f.device.command=2;assert(f.io.end(r::Kind::tssi,false));}
    {Fixture f;f.arm();f.owner.failShot=true;assert(!f.io.end(r::Kind::tssi,false));
        assert(f.io.leaseActive()&&!f.io.calibrationTxArmed()&&!f.owner.endCount);
        f.owner.failShot=false;assert(f.io.end(r::Kind::tssi,false));}
    {Fixture f;f.arm();f.owner.failEnd=true;assert(!f.io.end(r::Kind::tssi,false)&&f.io.leaseActive());
        f.owner.failEnd=false;assert(f.io.end(r::Kind::tssi,false));}
    {Fixture f;assert(f.io.begin(r::Kind::iqk));assert(!f.io.armCalibrationTx());assert(f.io.end(r::Kind::iqk,true));}
    {Fixture f;auto ctl=f.owner.control();ctl.oneshot=nullptr;r::MacRfkIo io(&f.device,&f.map,ctl);
        assert(!io.begin(r::Kind::tssi)&&!f.owner.beginCount);}
    {Fixture f;auto ctl=f.owner.control();ctl.recover=nullptr;r::MacRfkIo io(&f.device,&f.map,ctl);
        assert(!io.begin(r::Kind::dpk)&&!f.owner.beginCount);}
    {Fixture f;assert(f.io.begin(r::Kind::dpk));assert(f.io.writeBb(r::R_NCTL_CFG,0x1019));
        f.io.cancel();f.owner.failRecover=true;assert(!f.io.end(r::Kind::dpk,false));
        assert(f.io.leaseActive()&&f.owner.recoverCount==1&&!f.owner.endCount);
        f.owner.failRecover=false;f.owner.failEnd=true;assert(!f.io.end(r::Kind::dpk,false));
        assert(f.owner.recoverCount==2);f.owner.failEnd=false;assert(f.io.end(r::Kind::dpk,false)&&f.owner.recoverCount==2);}
    {Fixture f;f.arm();f.owner.failRecover=true;assert(!f.io.end(r::Kind::tssi,false));
        assert(!f.io.calibrationTxArmed()&&f.io.leaseActive()&&f.owner.shotCount==1&&!f.owner.endCount);
        f.owner.failRecover=false;assert(f.io.end(r::Kind::tssi,false)&&f.owner.shotCount==2);}
    {Fixture f;auto ctl=f.owner.control();ctl.oneshot=nullptr;r::MacRfkIo io(&f.device,&f.map,ctl);
        assert(io.begin(r::Kind::scan)&&!io.armCalibrationTx());
        assert(io.writeBb(r::R_P0_TSSI_TRK,0xc0));io.cancel();f.owner.failRecover=true;
        assert(!io.end(r::Kind::scan,false)&&io.leaseActive()&&!f.owner.endCount&&!f.owner.shotCount);
        f.owner.failRecover=false;assert(io.end(r::Kind::scan,false)&&!io.leaseActive());}
    {Fixture f;namespace c=rtl8852be::channel;uint8_t byte=0;uint32_t word=0;
        assert(!f.io.writeChannelMac8(c::R_AX_WMAC_RFMOD,2));
        assert(f.io.begin(r::Kind::iqk)&&!f.io.writeChannelMac32(c::R_AX_PPDU_STAT,0));assert(f.io.end(r::Kind::iqk,true));
        assert(f.io.begin(r::Kind::channel));f.map.set(c::R_AX_WMAC_RFMOD,0xaabbccdd);
        assert(f.io.readChannelMac8(c::R_AX_WMAC_RFMOD,byte)&&byte==0xdd);
        assert(f.io.writeChannelMac8(c::R_AX_WMAC_RFMOD,2)&&f.map.get(c::R_AX_WMAC_RFMOD)==0xaabbcc02);
        assert(!f.io.writeChannelMac8(c::R_AX_WMAC_RFMOD+1,0));
        assert(!f.io.writeChannelMac32(r::R_AX_CTN_TXEN,1)&&!f.io.writeChannelMac32(r::R_AX_CMAC_FUNC_EN,0));
        assert(f.io.writeChannelMac32(c::R_AX_PPDU_STAT,0x20)&&f.io.readChannelMac32(c::R_AX_PPDU_STAT,word)&&word==0x20);
        f.owner.resumeMap=&f.map;assert(!f.io.end(r::Kind::channel,true)&&f.io.leaseActive());
        f.owner.resumeMap=nullptr;f.map.set(r::R_AX_CTN_TXEN,0);
        assert(f.io.end(r::Kind::channel,false)&&f.owner.recoverCount==1);}
    {Fixture f;rtl8852be::channel::ChannelProgramming<r::MacRfkIo> channel(f.io);
        rtl8852be::network::BasebandGain gain;rtl8852be::network::BoardCalibration board;rtl8852be::network::PhyCalibration phy;
        board.identityValid=true;assert(channel.configure(gain,board,phy,0,0,3));
        f.map.set(r::R_AX_WCPU_FW_CTRL,0);f.owner.failEnd=true;
        assert(!channel.program({1,2,42,36})&&f.io.leaseActive()&&!channel.result.ownershipReleased);
        f.owner.failEnd=false;assert(channel.abort()&&!f.io.leaseActive());}
    {Fixture f;namespace p=rtl8852be::power;uint32_t v=0;
        assert(!f.io.readPowerMac32(p::R_AX_PWR_LMT,v)&&!f.io.writePowerMac32(p::R_AX_PWR_LMT,1));
        assert(f.io.begin(r::Kind::iqk));assert(!f.io.writePowerMac32(p::R_AX_PWR_LMT,1));assert(f.io.end(r::Kind::iqk,true));
        assert(f.io.begin(r::Kind::channel));
        for(auto address:{p::R_AX_PWR_LMT,p::R_AX_PWR_BY_RATE,p::R_AX_PWR_RU_LMT}){
            assert(f.io.writePowerMac32(address,0xffffffff));assert(f.io.readPowerMac32(address,v)&&v==0xffffffff);}
        for(auto address:{p::R_AX_PWR_LMT+1,p::R_AX_PWR_RATE_CTRL+8,p::R_AX_PWR_RU_LMT+48,r::R_AX_CTN_TXEN,0xf200u})
            assert(!f.io.readPowerMac32(address,v)&&!f.io.writePowerMac32(address,0));
        f.map.set(r::R_AX_CTN_TXEN,1);assert(!f.io.writePowerMac32(p::R_AX_PWR_LMT,0));f.map.set(r::R_AX_CTN_TXEN,0);
        f.map.set(r::R_AX_CMAC_FUNC_EN,0);assert(!f.io.readPowerMac32(p::R_AX_PWR_LMT,v));f.map.set(r::R_AX_CMAC_FUNC_EN,r::B_AX_CMAC_EN);
        f.device.command=0xffff;assert(!f.io.writePowerMac32(p::R_AX_PWR_LMT,0));f.device.command=2;
        f.owner.failRecover=true;assert(!f.io.end(r::Kind::channel,false)&&f.io.leaseActive());
        f.owner.failRecover=false;assert(f.io.end(r::Kind::channel,false));}
    // A missing sentinel in power data is not device loss. Independent status
    // reads before AND after that data still detect a lost device.
    for(unsigned failedRead:{1u,2u,4u,5u}){Fixture f;namespace p=rtl8852be::power;uint32_t v=0;
        assert(f.io.begin(r::Kind::channel));fakeRfk::accesses=0;fakeRfk::badRead=failedRead;
        assert(!f.io.readPowerMac32(p::R_AX_PWR_LMT,v));fakeRfk::badRead=0;assert(f.io.end(r::Kind::channel,false));}
    puts("PASS: native MacRfkIo live lease checks inside RF polls and sleep slices, post-write expiry and PMAC cleanup, power MMIO permissions, signed power data and retained recovery ownership; modeled MMIO");
}
