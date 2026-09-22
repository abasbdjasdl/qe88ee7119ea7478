// SPDX-License-Identifier: BSD-3-Clause
// Actual native adapter, with a modeled CMAC clock-gated bus response.
#define OSReadLittleInt32 baseRead32
#define OSWriteLittleInt32 baseWrite32
#include "network_rfk_fakes/Fake.hpp"
#undef OSReadLittleInt32
#undef OSWriteLittleInt32
#include <cstdio>
static unsigned deadReads,reads,writes;
static bool stuck,dropMaster;
static IOPCIDevice *activeDevice;
inline uint32_t OSReadLittleInt32(const volatile void *b,unsigned a){
    ++reads;
    if(a>=0xc000&&(stuck||deadReads)){if(deadReads)--deadReads;return 0xdeadbeef;}
    return baseRead32(b,a);
}
inline void OSWriteLittleInt32(volatile void *b,unsigned a,uint32_t v){
    ++writes;baseWrite32(b,a,v);if(dropMaster)activeDevice->command=6;
}
inline uint16_t OSReadLittleInt16(const volatile void *b,unsigned a){
    auto p=static_cast<const volatile uint8_t *>(b)+a;return uint16_t(p[0])|(uint16_t(p[1])<<8);
}
inline void OSWriteLittleInt16(volatile void *b,unsigned a,uint16_t v){
    auto p=static_cast<volatile uint8_t *>(b)+a;p[0]=uint8_t(v);p[1]=uint8_t(v>>8);
}
#include "../src/network/MacInitializationIo.cpp"
namespace m=rtl8852be::macinit;
int main(){
    IOPCIDevice device;IOMemoryMap map;activeDevice=&device;
    m::MacInitializationIo io(&device,&map);assert(io.valid());uint32_t v=0;
    deadReads=1;assert(io.read32(0xc004,v)&&v==0xffffffff&&writes==1&&reads==2);
    deadReads=10;reads=writes=0;assert(io.read32(0xc004,v)&&reads==11&&writes==10);
    stuck=true;reads=writes=0;assert(!io.read32(0xc004,v)&&v==0xdeadbeef&&reads==11&&writes==10);stuck=false;
    // Width conversion must happen after DEAD detection, not return EF/BEEF.
    map.set(0xc000,0x12345678);uint8_t b=0;uint16_t h=0;
    deadReads=1;assert(io.read8(0xc000,b)&&b==0x78);
    deadReads=1;assert(io.read16(0xc000,h)&&h==0x5678);
    map.set(0x1000,0xdeadbeef);writes=0;assert(io.read32(0x1000,v)&&v==0xdeadbeef&&!writes);
    writes=0;assert(!io.read32(0xe004,v)&&!writes);
    deadReads=1;dropMaster=true;assert(!io.read32(0xc004,v));dropMaster=false;device.command=2;
    io.cancel();writes=0;assert(!io.read32(0xc004,v)&&!writes);
    puts("PASS: native CMAC DEAD recovery, 10-retry bound, narrow reads, no non-CMAC recovery, ownership/cancel rejection");
}
