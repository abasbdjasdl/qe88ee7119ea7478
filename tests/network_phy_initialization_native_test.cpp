// SPDX-License-Identifier: BSD-3-Clause
// Executes the actual native MMIO implementation; the model supplies hardware
// SI/RF completion, not a second copy of the initialization algorithm.
#define OSReadLittleInt32 phyBaseRead32
#define OSWriteLittleInt32 phyBaseWrite32
#define IODelay phyBaseDelay
#include "network_rfk_fakes/Fake.hpp"
#undef OSReadLittleInt32
#undef OSWriteLittleInt32
#undef IODelay
#include <vector>
#include <cstdio>
namespace model {
struct Access {unsigned a;uint32_t value;bool write;};
static std::vector<Access> trace;
static uint32_t rf[2][256];static uint8_t serial[256];
static bool stuckSi,corruptSi,frozen,dropCckThreshold,enableDuringCck;static unsigned slowAt,reverseAt;static uint64_t slowUs;
static void reset(){fakeRfk::reset();trace.clear();std::memset(rf,0,sizeof(rf));std::memset(serial,0,sizeof(serial));
    stuckSi=corruptSi=frozen=dropCckThreshold=enableDuringCck=false;slowAt=reverseAt=0;slowUs=0;rf[0][0x42]=23u<<1;rf[1][0x42]=41u<<1;}
static void set(volatile void *b,unsigned a,uint32_t v){auto *p=reinterpret_cast<volatile uint8_t*>(b)+a;
    for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(i*8));}
static uint32_t raw(const volatile void *b,unsigned a){auto *p=reinterpret_cast<const volatile uint8_t*>(b)+a;
    return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
}
inline uint32_t OSReadLittleInt32(const volatile void *b,unsigned a){
    const auto v=phyBaseRead32(b,a);model::trace.push_back({a,v,false});
    if(fakeRfk::accesses==model::slowAt)fakeRfk::time+=model::slowUs;
    if(fakeRfk::accesses==model::reverseAt)fakeRfk::time=0;return v;
}
inline void OSWriteLittleInt32(volatile void *b,unsigned a,uint32_t v){
    phyBaseWrite32(b,a,v);model::trace.push_back({a,v,true});if(fakeRfk::accesses==fakeRfk::ignoredWrite)return;
    if(a==0x14b64){if(model::dropCckThreshold)model::set(b,a,v&0x00ffffff);
        if(model::enableDuringCck)model::set(b,0x14b74,model::raw(b,0x14b74)|0x40000000);}
    if(a==0x270){assert((v&0x80000000)&&((v&255)==4||(v&255)==5));
        const auto address=uint8_t(v);const bool read=v&0x01000000;
        if(read){assert((v&0x00ff0000)==0);v=(v&~0xff00u)|(uint32_t(model::serial[address]^(model::corruptSi?1:0))<<8);}
        else{assert((v&0x00ff0000)==0x00ff0000);model::serial[address]=uint8_t(v>>8);}
        if(!model::stuckSi)model::set(b,a,v&~0x80000000u);
    }else if(a==0x10370){const auto p=(v>>28)&1,address=(v>>20)&255;
        const uint32_t mask=(v&0x80000000)?model::raw(b,0x10374)&0xfffff:0xfffff;
        model::rf[p][address]=(model::rf[p][address]&~mask)|(v&mask);
    }else if(a==0x10378){const auto p=(v>>8)&1,address=v&255;model::set(b,0x1174c,0x04000000|model::rf[p][address]);}
}
inline void IODelay(unsigned us){if(model::frozen)++fakeRfk::delays;else phyBaseDelay(us);}
#include "../src/network/MacRadioIo.cpp"
#include "../src/network/MacPhyInitialization.cpp"
using namespace rtl8852be;
using namespace rtl8852be::network;
namespace pc=rtl8852be::network::phyinit_constants;
struct Fixture {
    IOPCIDevice device;IOMemoryMap map;IOWorkLoop loop;bool owned=true;unsigned checks=0,denyAt=0;
    MacPhyInitialization init;CalibrationSnapshot cal{};firmware::CapabilitySnapshot caps{};
    static bool guard(void *p){auto &f=*static_cast<Fixture*>(p);++f.checks;return f.owned&&f.checks!=f.denyAt;}
    Fixture():init(&device,&map,&loop,{this,guard}){
        cal.cut=1;cal.board.identityValid=cal.board.xtalValid=true;cal.board.xtal=0x7f;cal.board.rfe=9;
        for(unsigned i=0;i<6;++i)cal.board.mac[i]=uint8_t(i+1);
        caps.epoch=19;caps.cut=cal.cut;caps.rfe=9;caps.rxNss=2;caps.supportCckpd=true;
        std::memcpy(caps.mac,cal.board.mac,6);
        map.set(0x10000+pc::R_P0_RPL1,0x000000f9);map.set(0x10000+pc::R_P1_RPL1,0x0000000b);
        map.set(0x10000+pc::R_BANDEDGE,pc::B_BANDEDGE_EN);
        for(unsigned a=pc::R_AX_PWR_MACID_LMT_TABLE0;a<=pc::R_AX_PWR_MACID_LMT_TABLE127;a+=4)map.set(a,0x12345678);
    }
    bool before(){return init.powerUnit()&&init.reset()&&init.beforeRfk(cal,caps);}
    bool all(){return before()&&init.powerReference()&&init.receivePath({0,0,1});}
    uint32_t field(uint32_t a,uint32_t m,bool bb=true){unsigned s=0;while(!(m&(1u<<s)))++s;return (map.get((bb?0x10000:0)+a)&m)>>s;}
};
int main(){
    model::reset();Fixture first;assert(first.all());assert(first.init.result().afterRfkDone);
    const auto golden=model::trace;const unsigned accesses=fakeRfk::accesses;
    assert(first.init.result().offsetBase==-7&&first.init.result().rssiBase==11);
    assert(first.init.result().thermal[0]==23&&first.init.result().thermal[1]==41);
    assert(first.init.result().thermalPresent[0]&&first.init.result().thermalPresent[1]);
    assert(first.init.result().defaultBandedge&&model::serial[4]==127&&model::serial[5]==127);
    assert(first.map.get(pc::R_AX_PWR_COEXT_CTRL)==0x01ebf000);
    assert(first.field(pc::R_AX_PWR_UL_TB_2T,pc::B_AX_PWR_UL_TB_2T_MASK,false)==29);
    for(unsigned a=pc::R_AX_PWR_MACID_LMT_TABLE0;a<=pc::R_AX_PWR_MACID_LMT_TABLE127;a+=4)assert(first.map.get(a)==0);
    assert(first.field(pc::R_IFS_T1,pc::B_IFS_T1_TH_HIGH_MSK)==2);
    assert(first.field(pc::R_IFS_T4,pc::B_IFS_T4_TH_LOW_MSK)==33);
    assert(first.field(pc::R_IFS_T4,pc::B_IFS_T4_TH_HIGH_MSK)==128);
    assert(first.field(pc::R_DCFO_WEIGHT,pc::B_DCFO_WEIGHT_MSK)==8);
    assert(first.field(pc::R_BMODE_PDTH_V1,pc::B_BMODE_PDTH_LOWER_BOUND_MSK_V1)==128);
    assert(first.field(0x5804,pc::B_DPD_TSSI_CW)==172&&first.field(0x7808,pc::B_DPD_PWR_CW)==312);
    assert(first.field(pc::R_CHBW_MOD_V1,pc::B_ANT_RX_SEG0)==3);
    assert(first.field(pc::R_PATH1_BT_SHARE_V1,pc::B_PATH1_BT_SHARE_V1)==1);
    assert(!first.init.receivePath({0,0,1}));
    model::reset();Fixture inactive;model::dropCckThreshold=true;assert(inactive.all());
    assert(inactive.field(pc::R_BMODE_PDTH_V1,pc::B_BMODE_PDTH_LOWER_BOUND_MSK_V1)==0);
    assert(inactive.field(pc::R_BMODE_PDTH_EN_V1,pc::B_BMODE_PDTH_LIMIT_EN_MSK_V1)==0);
    model::reset();Fixture activated;model::dropCckThreshold=model::enableDuringCck=true;
    assert(!activated.all()&&activated.init.result().error==PhyInitError::readback&&activated.init.result().address==0x14b74);
    // A ready SI register read that itself takes over 50 ms is too late.
    for(unsigned n=0;n<golden.size();++n)if(golden[n].a==0x270&&!golden[n].write){
        model::reset();Fixture f;model::slowAt=n+1;model::slowUs=50001;
        // The final explicit data read is governed by the enclosing 1 s phase,
        // whereas every read in siIdle must honor its own 50 ms deadline.
        const bool finalData=n&&golden[n-1].a==0x270&&!golden[n-1].write;
        if(!finalData){assert(!f.all());assert(f.init.result().error==PhyInitError::timeout);}
    }
    unsigned readFaults=0;
    for(unsigned n=0;n<golden.size();++n)if(!golden[n].write){
        model::reset();Fixture f;fakeRfk::badRead=n+1;assert(!f.all());assert(!f.init.result().afterRfkDone);
        const auto count=fakeRfk::accesses;assert(!f.init.powerReference());assert(fakeRfk::accesses==count);++readFaults;
    }
    unsigned writeFaults=0;
    for(unsigned n=0;n<golden.size();++n)if(golden[n].write){
        model::reset();Fixture f;fakeRfk::ignoredWrite=n+1;
        // Source reset/write-only strobes cannot all be read back; other writes
        // must either fail closed or leave the entire final programmed image.
        if(f.all()){
            for(unsigned a=0;a<sizeof(f.map.data);a+=4){
                if(a==0x270||a==0x10370||a==0x10374||a==0x10378||a==0x1174c)continue;
                if(a==0x14b64){assert((f.map.get(a)&0x00ffffff)==(first.map.get(a)&0x00ffffff));assert(!(f.map.get(0x14b74)&0x40000000));}
                else assert(f.map.get(a)==first.map.get(a));
            }
        }else assert(!f.init.result().afterRfkDone);++writeFaults;
    }
    for(unsigned cut=0;cut<2;++cut)for(unsigned path=0;path<4;++path)for(unsigned nss=1;nss<3;++nss){
        model::reset();Fixture f;f.cal.cut=f.caps.cut=uint8_t(cut);f.caps.supportCckpd=cut;
        f.caps.antennaRx=uint8_t(path);f.caps.rxNss=uint8_t(nss);f.device.command=6; // active firmware H2C DMA allowed
        f.cal.board.gainOffsetValid=f.cal.phy.gainCompValid=true;
        f.cal.board.gainOffset[0][0]=-8;f.cal.board.gainOffset[0][1]=7;f.cal.board.gainOffset[1][1]=-8;
        f.cal.phy.gainComp[0][0]=-8;assert(f.all());
        assert(f.field(pc::R_CHBW_MOD_V1,pc::B_ANT_RX_SEG0)==(path?path:3));
        assert(f.field(pc::R_RXHE,pc::B_RXHE_MAX_NSS)==nss-1);
        assert(f.field(pc::R_P0_AGC_RSVD,0xff)==224);
        assert(f.field(pc::R_PATH1_BT_SHARE_V1,pc::B_PATH1_BT_SHARE_V1)==(path!=1));
        assert(f.field(pc::R_P0_RPL1,pc::B_P0_RPL1_BIAS_MASK)==(path==2?121:137));
    }
    model::reset();Fixture neighbors;neighbors.map.set(0x10000+pc::R_P0_RPL1,0x98abcd80);
    neighbors.map.set(0x10000+pc::R_P1_RPL1,0x76abcd7f);neighbors.cal.board.gainOffsetValid=true;
    neighbors.cal.board.gainOffset[0][1]=-8;assert(neighbors.all());
    assert(neighbors.init.result().offsetBase==-128&&neighbors.init.result().rssiBase==127);
    assert(neighbors.map.get(0x10000+pc::R_P0_RPL1)==0x98abcd00);
    assert(neighbors.map.get(0x10000+pc::R_P1_RPL1)==0x76abcd7f); // positive clamp, preserve 24 neighboring bits
    model::reset();Fixture noThermal;model::rf[0][0x42]=0;assert(noThermal.all());
    assert(!noThermal.init.result().thermalPresent[0]&&noThermal.init.result().thermal[0]==0);
    for(unsigned kind=0;kind<7;++kind){model::reset();Fixture f;
        if(kind==0)f.device.command=0xffff;if(kind==1)f.device.command=0;
        if(kind==2)f.loop.gate=false;if(kind==3)f.owned=false;if(kind==4)f.init.cancel();
        if(kind==5){f.map.physical++;MacPhyInitialization other(&f.device,&f.map,&f.loop,{&f,Fixture::guard});assert(!other.valid());continue;}
        if(kind==6){f.map.length=0x10000;MacPhyInitialization other(&f.device,&f.map,&f.loop,{&f,Fixture::guard});assert(!other.valid());continue;}
        assert(!f.all()&&fakeRfk::accesses==0);
    }
    for(unsigned n=1;n<first.checks;++n){model::reset();Fixture f;f.denyAt=n;assert(!f.all());assert(!f.init.result().afterRfkDone);}
    for(unsigned kind=0;kind<5;++kind){model::reset();Fixture f;assert(f.init.powerUnit()&&f.init.reset());
        if(kind==0){model::stuckSi=true;model::frozen=true;}
        if(kind==1)model::corruptSi=true;
        if(kind==2){model::slowAt=fakeRfk::accesses+1;model::slowUs=1000001;}
        if(kind==3)model::reverseAt=fakeRfk::accesses+1;
        if(kind==4)f.map.set(0x270,0x80000000);
        assert(!f.init.beforeRfk(f.cal,f.caps));assert(!f.init.result().beforeRfkDone&&f.init.result().requiresReset);
        assert(fakeRfk::accesses<accesses+2000);
        if(kind==4)for(const auto &a:model::trace)assert(a.a!=0x270||!a.write);
    }
    model::reset();Fixture mismatch;mismatch.caps.cut=0;assert(mismatch.init.powerUnit()&&mismatch.init.reset());
    const auto count=fakeRfk::accesses;assert(!mismatch.init.beforeRfk(mismatch.cal,mismatch.caps));assert(fakeRfk::accesses==count);
    std::printf("PHY native model passed: %u read faults, %u dropped writes, %u ownership boundaries, cut/path/NSS and SI/time faults\n",readFaults,writeFaults,first.checks-1);
}
