// SPDX-License-Identifier: BSD-3-Clause
#include "../src/FirmwareBoot.hpp"
#include <assert.h>
#include <stdio.h>
#include <map>
#include <vector>
namespace b=rtl8852be::boot;
struct Device {
    std::map<uint32_t,uint32_t> regs{{0x1000,0xc15000},{0x1010,0xb0f00},{0xc00,0x30000},{0x1e6,3},{0x88,5},{8,0xc0202021},{0x1e0,0xe0}};
    std::vector<uint32_t> writeAddresses;
    unsigned writes=0,fail=0,romError=0,inaccessibleHfcReads=0,inaccessibleClockAccesses=0;uint64_t time=0;uint16_t cmd=2;bool stuck=false,noH2c=false,noWde=false,noPle=false,stale=false,loseStop=false,reservedLatched=false,loseClockClear=false,loseMacClear=false;
    uint16_t command(){return cmd;}
    uint64_t nowUs(){return time;}
    void pauseUs(unsigned us){assert(us==50);if(!stuck)time+=us;}
    uint32_t read32(uint32_t a){
        if(a==0x8404&&!(regs[0x8400]&0x40000000)){++inaccessibleClockAccesses;return 0xffffffff;}
        if(a==0x8a00&&(regs[0x8400]==0||regs[0x8404]==0)){++inaccessibleHfcReads;return 0xdeadbeef;}
        return regs[a];
    }
    uint16_t read16(uint32_t a){assert(a==0x1e6);return static_cast<uint16_t>(regs[a]);}
    bool bootWrite32(uint32_t a,uint32_t v){
        assert(b::allowed32(a));writeAddresses.push_back(a);++writes;if(writes==fail)return false;
        if(a==0x8404&&!(regs[0x8400]&0x40000000)){++inaccessibleClockAccesses;return true;}
        if(a==0x8404&&v==0&&loseClockClear)return true;
        if(a==0x8400&&v==0&&loseMacClear)return true;
        // Model the 8852B absent ACH4..7 bits, not an all-writable register file.
        if(a==0x1010){v&=~3u;if(!reservedLatched)v&=~0xf000u;if(loseStop)v&=~0x40000u;}regs[a]=v;
        if(a==0x8400&&(v&0x04800000)==0x04800000){regs[0x8d00]=noWde?0:3;regs[0x9100]=noPle?0:3;}
        if(a==0x88&&(v&2)){if(!noH2c)regs[0x1e0]|=2;if(stale)regs[0x1e0]|=0xe0;regs[0x1e0]|=romError<<5;}
        return true;
    }
    bool bootWrite16(uint32_t a,uint16_t v){assert(a==0x1e6);writeAddresses.push_back(a);++writes;if(writes==fail)return false;regs[a]=v;return true;}
};
int main(){
    Device d;auto r=b::probe(d);assert(r.status==b::Status::ready&&r.cleanupOK&&r.cpuStopped&&r.wde==3&&r.ple==3&&(r.control&2));
    assert(r.dmac==0x64c40000&&r.clock==0x04840000);
    assert(d.regs[0x1000]==0xc15000&&d.regs[0x1010]==0xb0f00&&d.regs[0xc00]==0x30000&&d.regs[0x1e6]==3);
    assert(!d.inaccessibleHfcReads&&!r.failureRecorded&&!r.cleanupFailures);
    assert(!d.inaccessibleClockAccesses&&r.cleanupClockChecked&&r.cleanupClock==0&&r.cleanupDmac==0);
    assert((r.stopPaused&0x70f00)==0x70f00&&(r.stopPaused&0xf003)==0);
    assert(r.writes==d.writes);
    assert(d.regs[0x8c08]==0&&d.regs[0x9008]==0x400801&&d.regs[0x8c44]==48);
    assert(d.regs[0x9048]==0x100010&&d.regs[0x904c]==0x300030&&d.regs[0x8a04]==40u<<16);
    const unsigned writes=d.writes;
    for(unsigned i=1;i<=writes;++i){d=Device{};d.fail=i;r=b::probe(d);assert(r.status!=b::Status::ready);}
    // Hardware 0.0.9: disabling the parent first makes CLK_EN inaccessible and
    // ignores its later write. Do not treat the all-ones read as a valid zero.
    d=Device{};d.regs[0x8400]=0x64c40000;d.regs[0x8404]=0x04840000;
    d.bootWrite32(0x8400,0);d.bootWrite32(0x8404,0);
    assert(d.read32(0x8404)==0xffffffff&&d.regs[0x8404]==0x04840000);
    d=Device{};d.loseClockClear=true;r=b::probe(d);
    assert(r.status==b::Status::cleanupFailed&&r.operationStatus==b::Status::ready&&(r.cleanupFailures&4));
    assert(r.failureRecorded&&r.failureAddress==0x8404&&r.failureActual==0x04840000&&r.cleanupClockChecked);
    assert(!d.inaccessibleClockAccesses&&r.cleanupDmac==0&&r.cpuStopped);
    d=Device{};d.loseMacClear=true;r=b::probe(d);
    assert(r.status==b::Status::cleanupFailed&&(r.cleanupFailures&2)&&r.failureAddress==0x8400);
    assert(r.cleanupClockChecked&&r.cleanupClock==0&&!d.inaccessibleClockAccesses);
    // Reproduce the old full-channel requirement: absent bits cannot read back.
    d=Device{};d.bootWrite32(0x1010,d.regs[0x1010]|0x7ff03);
    assert((d.read32(0x1010)&0x7ff03)!=0x7ff03);
    // A real supported-bit failure aborts without touching uninitialized blocks.
    d=Device{};d.loseStop=true;r=b::probe(d);
    assert(r.status==b::Status::writeFailed&&r.operationStatus==b::Status::writeFailed&&r.phase==1&&r.cleanupOK);
    assert(r.failureRecorded&&r.failureAddress==0x1010&&r.failureMask==0x70f00&&r.failureExpected==0x70f00);
    assert((r.failureActual&0x40000)==0&&!d.inaccessibleHfcReads);
    for(auto a:d.writeAddresses)assert(a==0x1000||a==0x1010);
    // 0.0.7 hardware had reserved bits set in STOP1 (0xfff00); preserve those
    // bits too, without requiring RX stop bits that the reference never uses.
    d=Device{};d.reservedLatched=true;d.regs[0x1010]=0xfff00;
    r=b::probe(d);assert(r.status==b::Status::ready&&r.stopAfter==0xfff00&&!r.cleanupFailures);
    // Ignore nonexistent channel busy bits, but retain real DMA/PCIIO checks.
    d=Device{};d.regs[0x101c]=0xf000;r=b::probe(d);assert(r.status==b::Status::ready);
    d=Device{};d.regs[0x101c]=0x40000;r=b::probe(d);assert(r.status==b::Status::timeout&&r.phase==1&&r.cleanupOK);
    for(auto a:d.writeAddresses)assert(a==0x1000||a==0x1010);
    d=Device{};d.noH2c=true;r=b::probe(d);assert(r.status==b::Status::timeout&&r.phase==6&&r.cleanupOK);
    d=Device{};d.noH2c=true;d.stuck=true;r=b::probe(d);assert(r.status==b::Status::timeout&&r.polls<=8004&&r.cleanupOK);
    d=Device{};d.noWde=true;r=b::probe(d);assert(r.status==b::Status::timeout&&r.phase==3&&r.cleanupOK);
    d=Device{};d.noPle=true;r=b::probe(d);assert(r.status==b::Status::timeout&&r.phase==3&&r.cleanupOK);
    d=Device{};d.stale=true;r=b::probe(d);assert(r.status==b::Status::staleReady&&r.cleanupOK);
    for(unsigned error:{2u,3u,4u}){d=Device{};d.romError=error;r=b::probe(d);assert(r.status==b::Status::romError&&r.cleanupOK);}
    d=Device{};d.cmd=6;r=b::probe(d);assert(r.status==b::Status::unsafeCommand&&!d.writes);
    d=Device{};d.regs[0x1000]=0xffffffff;r=b::probe(d);assert(r.status==b::Status::invalidRead&&!r.attempted&&!d.writes);
    assert(!b::allowed32(0x1080)&&!b::allowed32(0xc000)&&!b::allowed32(0x30)&&!b::allowed32(0x38));
    printf("PASS: 8852B stop mask, MAC-gated clock read/write regression, dropped cleanup writes, DLFW/ROM, %u write failures, bounded timeout; no DMA or RF\n",writes);
}
