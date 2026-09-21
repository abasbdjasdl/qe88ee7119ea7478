// SPDX-License-Identifier: BSD-3-Clause
#include "../src/FirmwareBoot.hpp"
#include <assert.h>
#include <stdio.h>
#include <map>
namespace b=rtl8852be::boot;
struct Device {
    std::map<uint32_t,uint32_t> regs{{0x1000,0xc15000},{0x1010,0xbff00},{0xc00,0x30000},{0x1e6,3},{0x88,5},{8,0xc0202021},{0x1e0,0xe0}};
    unsigned writes=0,fail=0,romError=0;uint64_t time=0;uint16_t cmd=2;bool stuck=false,noH2c=false,noWde=false,noPle=false,stale=false;
    uint16_t command(){return cmd;}
    uint64_t nowUs(){return time;}
    void pauseUs(unsigned us){assert(us==50);if(!stuck)time+=us;}
    uint32_t read32(uint32_t a){return regs[a];}
    uint16_t read16(uint32_t a){assert(a==0x1e6);return static_cast<uint16_t>(regs[a]);}
    bool bootWrite32(uint32_t a,uint32_t v){
        assert(b::allowed32(a));++writes;if(writes==fail)return false;regs[a]=v;
        if(a==0x8400&&(v&0x04800000)==0x04800000){regs[0x8d00]=noWde?0:3;regs[0x9100]=noPle?0:3;}
        if(a==0x88&&(v&2)){if(!noH2c)regs[0x1e0]|=2;if(stale)regs[0x1e0]|=0xe0;regs[0x1e0]|=romError<<5;}
        return true;
    }
    bool bootWrite16(uint32_t a,uint16_t v){assert(a==0x1e6);++writes;if(writes==fail)return false;regs[a]=v;return true;}
};
int main(){
    Device d;auto r=b::probe(d);assert(r.status==b::Status::ready&&r.cleanupOK&&r.cpuStopped&&r.wde==3&&r.ple==3&&(r.control&2));
    assert(r.dmac==0x64c40000&&r.clock==0x04840000);
    assert(d.regs[0x1000]==0xc15000&&d.regs[0x1010]==0xbff00&&d.regs[0xc00]==0x30000&&d.regs[0x1e6]==3);
    assert(d.regs[0x8c08]==0&&d.regs[0x9008]==0x400801&&d.regs[0x8c44]==48);
    assert(d.regs[0x9048]==0x100010&&d.regs[0x904c]==0x300030&&d.regs[0x8a04]==40u<<16);
    const unsigned writes=d.writes;
    for(unsigned i=1;i<=writes;++i){d=Device{};d.fail=i;r=b::probe(d);assert(r.status!=b::Status::ready);}
    d=Device{};d.noH2c=true;r=b::probe(d);assert(r.status==b::Status::timeout&&r.phase==6&&r.cleanupOK);
    d=Device{};d.noH2c=true;d.stuck=true;r=b::probe(d);assert(r.status==b::Status::timeout&&r.polls<=8004&&r.cleanupOK);
    d=Device{};d.noWde=true;r=b::probe(d);assert(r.status==b::Status::timeout&&r.phase==3&&r.cleanupOK);
    d=Device{};d.noPle=true;r=b::probe(d);assert(r.status==b::Status::timeout&&r.phase==3&&r.cleanupOK);
    d=Device{};d.stale=true;r=b::probe(d);assert(r.status==b::Status::staleReady&&r.cleanupOK);
    for(unsigned error:{2u,3u,4u}){d=Device{};d.romError=error;r=b::probe(d);assert(r.status==b::Status::romError&&r.cleanupOK);}
    d=Device{};d.cmd=6;r=b::probe(d);assert(r.status==b::Status::unsafeCommand&&!d.writes);
    d=Device{};d.regs[0x1000]=0xffffffff;r=b::probe(d);assert(r.status==b::Status::invalidRead&&!r.attempted&&!d.writes);
    assert(!b::allowed32(0x1080)&&!b::allowed32(0xc000)&&!b::allowed32(0x30)&&!b::allowed32(0x38));
    printf("PASS: DLFW layout, WDE/PLE/HFC, ROM handshake, %u write failures, bounded timeout, shutdown; no DMA or RF\n",writes);
}
