// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/PciLinkInitialization.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace rtl8852be::network::pcilink;
struct Io {
    uint8_t mm[0x10000]{},config[0x1000]{};uint16_t phy[4][32]{};
    uint64_t time{};unsigned calls{},failAt{},writes{},delayCount{};
    bool gate{true},cancel{},fallback{},mdioStuck{},dbiStuck{},frozen{},ignoreWrites{};
    bool failAfter{},lbcW1c{true};unsigned ioCost{},failMdioAddress{0xff};
    std::vector<uint32_t> wordWrites;
    Io(){
        put32(4,0x4400);put32(0x70,0x4200);put32(0x74,0x80);
        put32(0x1008,0x60);put32(0x13f0,0x30);put32(0x11d8,0x500);
        put32(0x11c0,0x40);put32(0x1000,0x12345);
        put32(0x8410,0x80003000);put32(0x8414,0xf000f000);
        put32(0x8810,0x40);put32(0x9a00,0x46);
        config[0x82]=0x12;config[0x719]=0xad;
        phy[0][0x1b]=0xaabc;phy[0][0x1d]=0xabc1;
        phy[1][0x10]=0x2345;phy[3][0x10]=0x2678;
    }
    void put16(uint32_t a,uint16_t v){mm[a]=uint8_t(v);mm[a+1]=uint8_t(v>>8);}
    void put32(uint32_t a,uint32_t v){for(unsigned i=0;i<4;++i)mm[a+i]=uint8_t(v>>(8*i));}
    uint16_t get16(uint32_t a)const{return uint16_t(mm[a]|unsigned(mm[a+1])<<8);}
    uint32_t get32(uint32_t a)const{return uint32_t(mm[a])|uint32_t(mm[a+1])<<8|uint32_t(mm[a+2])<<16|uint32_t(mm[a+3])<<24;}
    bool tick(){++calls;time+=ioCost;return !failAt||calls!=failAt;}
    bool inGate(){return gate;}bool cancelled(){return cancel;}uint64_t nowUs(){return time;}
    bool delayUs(unsigned us){++delayCount;if(!frozen)time+=us;return true;}
    bool read8(uint32_t a,uint8_t &v){if(!tick())return false;v=mm[a];return true;}
    bool read16(uint32_t a,uint16_t &v){if(!tick())return false;v=get16(a);return true;}
    bool read32(uint32_t a,uint32_t &v){if(!tick())return false;v=get32(a);return true;}
    bool write8(uint32_t a,uint8_t v){
        const bool ok=tick();++writes;if((!ok&&!failAfter)||ignoreWrites)return ok;
        mm[a]=v;
        if(a==dbiFlag+2&&!dbiStuck){
            const auto flags=get16(dbiFlag);const auto address=flags&0xffc;
            if(v==2)for(unsigned lane=0;lane<4;++lane)mm[dbiReadData+lane]=config[address+lane];
            else if(v==1)for(unsigned lane=0;lane<4;++lane)if(flags&(1U<<(12+lane)))config[address+lane]=mm[dbiWriteData+lane];
            mm[a]=0;
        }
        return ok;
    }
    bool write16(uint32_t a,uint16_t v){
        const bool ok=tick();++writes;if((!ok&&!failAfter)||ignoreWrites)return ok;
        put16(a,v);
        if(a==mdioConfig&&(v&0x300)){
            const unsigned page=(v>>12)&3,address=v&31;
            if(mdioStuck||(address+((page&1)?32:0))==failMdioAddress)return ok;
            if(v&0x200)put16(mdioReadData,phy[page][address]);
            if(v&0x100)phy[page][address]=get16(mdioWriteData);
            put16(a,uint16_t(v&~0x300));
        }
        return ok;
    }
    bool write32(uint32_t a,uint32_t v){
        const bool ok=tick();++writes;wordWrites.push_back(a);
        if((ok||failAfter)&&!ignoreWrites)put32(a,lbcW1c&&a==0x11d8?v&~2U:v);return ok;
    }
    bool readConfig8(uint16_t a,uint8_t &v){if(fallback)return false;v=config[a];return true;}
    bool writeConfig8(uint16_t a,uint8_t v){++writes;if(fallback)return false;if(!ignoreWrites)config[a]=v;return true;}
};
static void expected(const Io &io,unsigned speed){
    assert(io.get32(4)==0x400);assert(io.get32(0x70)==0x8200);
    assert(io.get32(0x74)==0xa0);assert(io.get32(0x1008)==0x40);
    assert(io.get32(0x13f0)==0x20);assert(io.get32(0x11d8)==0x581);
    assert(io.get32(0x11c0)==0x43);assert(io.get32(0x1000)==(0x12345|0xc00000));
    assert(io.phy[0][0x1b]==0x1abc);assert(io.phy[0][0x1d]==0xabcd);
    assert(io.phy[speed==1?1:3][0x10]==(speed==1?0x345:0x678));
    assert(io.phy[speed==1?3:1][0x10]==(speed==1?0x2678:0x2345));
    assert(io.get32(0x8410)==0x80002743);assert(io.get32(0x8414)==0xf028f028);
    assert(io.get32(0x8418)==0x90039003);assert(io.get32(0x841c)==0x880b880b);
    assert(io.get32(0x8810)==0x41);assert(io.get32(0x9a00)==0x44);
    for(auto a:io.wordWrites){
        assert(a==4||a==0x70||a==0x74||a==0x1008||a==0x13f0||a==0x11d8||a==0x11c0||
               a==0x1000||a==0x8410||a==0x8414||a==0x8418||a==0x841c||a==0x8810||a==0x9a00);
    }
}
static void normal(){
    for(unsigned fallback=0;fallback<2;++fallback)for(unsigned speed=1;speed<=2;++speed)
    for(unsigned cut=0;cut<6;++cut)for(unsigned l1=0;l1<2;++l1){
        Io io;io.fallback=fallback;io.config[0x82]=uint8_t(speed|0x10);io.config[0x719]=l1?0xad:0xa5;
        Initialization<Io> init(io);assert(init.preInit(uint8_t(cut)));
        assert(init.result.preConfigured&&!init.result.postConfigured);
        assert(init.result.l1RestoreAttempted==bool(l1));assert(init.result.l1Restored==bool(l1));
        assert(io.config[0x719]==(l1?0xad:0xa5));
        io.time+=5000000; // External firmware/MAC initialization is outside each phase's budget.
        assert(init.postInit());assert(init.result.postConfigured);expected(io,speed);
        assert((init.result.dbiFallbacks>0)==bool(fallback));
        const auto calls=io.calls,writes=io.writes;const auto error=init.result.error;
        assert(!init.preInit(0));assert(!init.postInit());assert(io.calls==calls&&io.writes==writes);
        assert(init.result.error==error&&!init.result.requiresRecovery);
    }
}
static void failures(){
    unsigned points=0;
    for(unsigned fallback=0;fallback<2;++fallback){
        Io baseline;baseline.fallback=fallback;Initialization<Io> good(baseline);
        assert(good.preInit(1)&&good.postInit());const auto total=baseline.calls;
        for(unsigned after=0;after<2;++after)for(unsigned point=1;point<=total;++point){
            Io io;io.fallback=fallback;io.failAt=point;io.failAfter=after;Initialization<Io> init(io);
            const bool done=init.preInit(1)&&init.postInit();
            if(done){std::printf("unexpected success fallback=%u after=%u point=%u\n",fallback,after,point);}
            assert(!done);assert(init.result.error!=Error::none);assert(!init.result.postConfigured);
            assert(init.result.requiresRecovery==init.result.modified);++points;
        }
    }
    std::printf("PCI link failure points: %u\n",points);
    {Io io;Initialization<Io> init(io);assert(!init.postInit());assert(io.calls==0&&!init.result.modified);}
    {Io io;Initialization<Io> init(io);assert(!init.preInit(6));assert(io.calls==0&&!init.result.modified);}
    {Io io;io.gate=false;Initialization<Io> init(io);assert(!init.preInit(0));assert(init.result.error==Error::ownership&&io.calls==0);}
    {Io io;io.cancel=true;Initialization<Io> init(io);assert(!init.preInit(0));assert(init.result.error==Error::cancelled&&io.calls==0);}
    {Io io;io.config[0x82]=3;Initialization<Io> init(io);assert(!init.preInit(0));assert(init.result.error==Error::linkSpeed);}
    {Io io;io.ignoreWrites=true;Initialization<Io> init(io);assert(!init.preInit(0));assert(init.result.error==Error::readback);}
    {Io io;Initialization<Io> init(io);assert(init.preInit(0));io.put32(0x8418,0xeaeaeaea);assert(!init.postInit());assert(init.result.error==Error::io);}
    {Io io;Initialization<Io> init(io);assert(init.preInit(0));io.time=100;assert(init.postInit());}
    {Io io;io.time=100;Initialization<Io> init(io);assert(init.preInit(0));io.time=99;assert(!init.postInit());assert(init.result.error==Error::clock);}
}
static void timeouts(){
    for(unsigned frozen=0;frozen<2;++frozen){
        {Io io;io.mdioStuck=true;io.frozen=frozen;Initialization<Io> init(io);assert(!init.preInit(0));
         assert(init.result.error==Error::mdioTimeout);assert(io.delayCount==200);assert(io.time==(frozen?0:2000));}
        {Io io;io.put16(mdioConfig,0x100);io.frozen=frozen;Initialization<Io> init(io);assert(!init.preInit(0));
         assert(init.result.error==Error::mdioTimeout);assert(io.delayCount==200);}
        {Io io;io.fallback=true;io.dbiStuck=true;io.frozen=frozen;Initialization<Io> init(io);assert(!init.preInit(0));
         assert(init.result.error==Error::dbiTimeout);assert(io.delayCount==20);assert(io.time==(frozen?0:200));}
    }
    {Io io;io.failMdioAddress=0x30;Initialization<Io> init(io);assert(!init.preInit(0));
     assert(init.result.error==Error::mdioTimeout);assert(io.config[0x719]==0xad);
     assert(init.result.l1RestoreAttempted&&init.result.l1Restored&&!init.result.l1RestoreFailed);}
    // A read that itself exceeds the MDIO deadline may not authorize success.
    {Io io;io.ioCost=2100;Initialization<Io> init(io);assert(!init.preInit(0));assert(init.result.error==Error::mdioTimeout);}
    // DBI has its own shorter deadline, even when its flag is already clear.
    {Io io;io.fallback=true;io.ioCost=201;Initialization<Io> init(io);assert(!init.preInit(0));assert(init.result.error==Error::dbiTimeout);}
    {Io io;io.ioCost=100000;Initialization<Io> init(io);assert(!init.preInit(0));assert(init.result.error==Error::timeout);assert(!init.result.modified);}
}
int main(){normal();failures();timeouts();std::puts("PCI link pre/post initialization tests passed");}
