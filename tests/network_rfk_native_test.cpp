// SPDX-License-Identifier: BSD-3-Clause
// Executes native MMIO/cleanup source against an explicit IOKit register model.
#include "network_rfk_fakes/Fake.hpp"
#include "../src/network/MacRadioIo.cpp"
#include "../src/network/MacRfkIo.cpp"
#include <cstdio>
#include <initializer_list>
namespace r=rtl8852be::rfk;
struct Owner {
    unsigned beginCount{},endCount{},shotCount{},recoverCount{};bool failEnd{},failShot{},failRecover{};
    static bool begin(void *o,r::Kind){++static_cast<Owner *>(o)->beginCount;return true;}
    static bool end(void *o,r::Kind,bool){auto &x=*static_cast<Owner *>(o);++x.endCount;return !x.failEnd;}
    static bool shot(void *o,r::Kind,uint8_t,bool){auto &x=*static_cast<Owner *>(o);++x.shotCount;return !x.failShot;}
    static bool recover(void *o,r::Kind){auto &x=*static_cast<Owner *>(o);++x.recoverCount;return !x.failRecover;}
    r::CalibrationControl control(){return {this,begin,end,shot,recover};}
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
    puts("PASS: native MacRfkIo PMAC stop after cancellation, read/write/readback failures, device loss, mandatory recovery before releasing modified calibration/coexistence ownership; modeled MMIO");
}
