// SPDX-License-Identifier: BSD-3-Clause
#include "network_rfk_fakes/Fake.hpp"
#include "../src/network/MacDavEfuseIo.cpp"
#include <cstdio>
#include <vector>
#include <algorithm>
using namespace rtl8852be::network;
struct DavIo {
    uint8_t bank[96],control=0x40,address{},data{};
    uint32_t command{};uint64_t clock=1;
    size_t calls{},failAt=size_t(-1),cancelAt=size_t(-1),clockCalls{},backAt=size_t(-1),lastDataReadCall{};
    unsigned delays{},failDelay=~0u,siWait{},siWaitDefault=1,siWriteWait=~0u,davWait{},davWaitDefault=2;
    size_t slowReadAt=size_t(-1);unsigned slowReadUs{},slowReadyUs{};
    bool pending{},stuckSi{},stuckDav{},frozen{},removed{},failAfterWrite{};
    std::vector<uint32_t> requests;
    DavIo(){for(unsigned i=0;i<96;++i)bank[i]=uint8_t(i^0xa5);}
    bool cancelled(){return calls>=cancelAt;}
    bool access(){return calls++!=failAt&&!removed;}
    bool read32(uint32_t reg,uint32_t &value){
        if(calls==slowReadAt)clock+=slowReadUs;
        if(command==0x01000063u&&slowReadyUs){clock+=slowReadyUs;slowReadyUs=0;}
        assert(reg==0x270);if(!access())return false;
        if(pending&&!stuckSi&&!siWait--)pending=false;
        value=command|(pending?0x80000000u:0);return true;
    }
    bool read8(uint32_t reg,uint8_t &value){assert(reg==0x271&&!pending);if(uint8_t(command)==0x7a)lastDataReadCall=calls;if(!access())return false;value=data;return true;}
    bool write32(uint32_t reg,uint32_t value){
        assert(reg==0x270&&!pending&&davCommandAllowed(value));
        bool ok=access();if(!ok&&!failAfterWrite)return false;
        requests.push_back(value);command=value&~0x80000000u;pending=true;siWait=siWaitDefault;
        if(!(value&0x01000000u)&&siWriteWait!=~0u)siWait=siWriteWait;
        const auto offset=uint8_t(value),mask=uint8_t(value>>16),bits=uint8_t(value>>8);
        if(value&0x01000000u){
            if(offset==0x63){
                if(!(control&0xc0)&&!(control&0x20)&&!stuckDav){if(!davWait--)control|=0x20;}
                data=control;
            }else {assert(offset==0x7a&&(control&0x20));data=bank[address];}
        }else if(offset==0x62)address=bits;
        else {control=uint8_t((control&~mask)|(bits&mask));if(mask==0xc0)davWait=davWaitDefault;}
        return ok;
    }
    bool delayUs(unsigned us){assert(us==1||us==50);if(delays++==failDelay)return false;if(!frozen)clock+=us;return true;}
    uint64_t nowUs(){return clockCalls++==backAt?0:clock;}
};
static void complete(){
    uint8_t out[96];DavIo io;DavEfuseReader<DavIo> reader(io);
    auto r=reader.readPhysical(0,96,out,96);
    assert(r.status==DavStatus::ok&&r.validBytes==96&&r.bytesRead==96);
    assert(r.busIdle&&r.readEngineIdle&&r.cleanupAttempted&&!r.requiresReset);
    assert(std::equal(out,out+96,io.bank));
    // Each byte follows the pinned four-write DAV read request sequence.
    size_t cursor=0;
    for(unsigned i=0;i<96;++i){
        assert(io.requests[cursor++]==0x80ff4063);
        assert(io.requests[cursor++]==(0x80ff0062u|(i<<8)));
        assert(io.requests[cursor++]==0x80070063);
        assert(io.requests[cursor++]==0x80c00063);
        for(unsigned p=0;p<3;++p)assert(io.requests[cursor++]==0x81000063);
        assert(io.requests[cursor++]==0x8100007a);
    }
    assert(io.requests[cursor++]==0x81000063&&cursor==io.requests.size());
    r=reader.readPhysical(95,1,out,96);assert(r.status==DavStatus::ok&&out[0]==io.bank[95]);
}
static void faults(){
    uint8_t out[16];DavIo reference;DavEfuseReader<DavIo> ref(reference);
    assert(ref.readPhysical(0,16,out,16).status==DavStatus::ok);
    for(bool applied:{false,true})for(size_t at=0;at<reference.calls;++at){
        DavIo io;io.failAt=at;io.failAfterWrite=applied;DavEfuseReader<DavIo> reader(io);
        std::fill(out,out+16,0xcc);const auto r=reader.readPhysical(0,16,out,16);
        assert(r.status!=DavStatus::ok&&!r.validBytes);
        for(size_t i=0;i<r.bytesRead;++i)assert(out[i]==0);
        if(r.status==DavStatus::cleanupFailed){assert(r.requiresReset);auto again=reader.readPhysical(0,1,out,16);assert(again.status==DavStatus::requiresReset);}
        else if(r.cleanupAttempted)assert(r.busIdle&&r.readEngineIdle&&!io.pending);
    }
    for(unsigned at=0;at<reference.delays;++at){
        DavIo io;io.failDelay=at;DavEfuseReader<DavIo> reader(io);
        assert(reader.readPhysical(0,16,out,16).status!=DavStatus::ok);
    }
    for(size_t at=0;at<reference.calls-10;++at){
        DavIo io;io.cancelAt=at;DavEfuseReader<DavIo> reader(io);
        auto r=reader.readPhysical(0,16,out,16);assert(r.status==DavStatus::cancelled&&!r.validBytes);
        if(r.cleanupAttempted)assert(r.readEngineIdle&&r.busIdle);
    }
    for(bool frozen:{false,true}){
        DavIo io;io.stuckSi=io.frozen=frozen;io.stuckSi=true;DavEfuseReader<DavIo> reader(io);
        auto r=reader.readPhysical(0,16,out,16);assert(r.primary==DavStatus::timeout&&r.status==DavStatus::cleanupFailed&&r.requiresReset);
        assert(r.polls<=2003&&io.delays<=2000);
        DavIo dav;dav.stuckDav=true;dav.frozen=frozen;DavEfuseReader<DavIo> dr(dav);
        r=dr.readPhysical(0,16,out,16);assert(r.primary==DavStatus::timeout&&r.status==DavStatus::cleanupFailed&&r.requiresReset);
        assert(dav.delays<100000);
    }
    {DavIo io;io.backAt=20;DavEfuseReader<DavIo> reader(io);auto r=reader.readPhysical(0,16,out,16);assert(r.status==DavStatus::clockError&&!r.validBytes&&r.readEngineIdle);}
    {DavIo io;io.siWriteWait=900;DavEfuseReader<DavIo> reader(io);uint8_t all[96];auto r=reader.readPhysical(0,96,all,96);assert(r.primary==DavStatus::timeout&&!r.validBytes&&io.clock>=5000000&&io.clock<5200000);}
    // Slow nested SI time counts toward the DAV 10 ms deadline, even if the
    // returned control value already carries RDY. Cleanup has its own budget.
    {DavIo io;io.slowReadyUs=12000;io.davWaitDefault=0;DavEfuseReader<DavIo> reader(io);
        auto r=reader.readPhysical(0,16,out,16);assert(r.status==DavStatus::timeout&&r.primary==DavStatus::timeout);
        assert(!r.validBytes&&!r.bytesRead&&r.cleanupAttempted&&r.busIdle&&r.readEngineIdle&&!r.requiresReset);
        assert(io.clock>=12000&&io.clock<50000);}
    // A single MMIO read can consume more than 50 ms; counting delay calls
    // alone would miss this. The completed prepared command remains drainable.
    {DavIo io;io.slowReadAt=2;io.slowReadUs=60000;io.siWaitDefault=0;DavEfuseReader<DavIo> reader(io);
        auto r=reader.readPhysical(0,16,out,16);assert(r.status==DavStatus::timeout&&r.primary==DavStatus::timeout);
        assert(!r.validBytes&&!r.bytesRead&&r.cleanupAttempted&&r.busIdle&&r.readEngineIdle&&!r.requiresReset);
        assert(io.clock==60001&&io.requests.size()==2);}
    {DavIo io;io.pending=io.stuckSi=true;DavEfuseReader<DavIo> reader(io);auto r=reader.readPhysical(0,16,out,16);assert(r.status==DavStatus::busy&&!r.cleanupAttempted&&io.requests.empty());}
    {DavIo io;io.removed=true;DavEfuseReader<DavIo> reader(io);assert(reader.readPhysical(0,16,out,16).status==DavStatus::ioError&&io.requests.empty());}
    std::printf("DAV: %zu I/O failures (before/after write), %u delay failures, cancellation and bounded SI/DAV cleanup passed\n",reference.calls,reference.delays);
}
static void decode(){
    DavIo io;std::fill(io.bank,io.bank+96,0xff);
    // Two 8-byte blocks; an appended word supersedes the earlier word.
    uint8_t encoded[]={0,0,1,2,3,4,5,6,7,8,0,0x10,9,10,11,12,13,14,15,16,0,0x0e,21,22};
    std::copy(encoded,encoded+sizeof encoded,io.bank+4);
    uint8_t physical[96],logical[16];DavEfuseReader<DavIo> reader(io);
    auto r=reader.readLogical(physical,96,logical,16);assert(r.status==DavStatus::ok&&r.logicalValid);
    assert(logical[0]==21&&logical[1]==22);for(unsigned i=2;i<16;++i)assert(logical[i]==i+1);
    std::fill(io.bank,io.bank+96,0xff);io.bank[4]=0;io.bank[5]=0x2e;io.bank[6]=1;io.bank[7]=2;
    std::fill(logical,logical+16,0xcc);r=reader.readLogical(physical,96,logical,16);
    assert(r.status==DavStatus::decodeFailed&&!r.logicalValid&&r.decode==EfuseStatus::outOfRange);
    for(auto value:logical)assert(value==0xcc);
    std::fill(io.bank,io.bank+96,0xff);r=reader.readLogical(physical,96,logical,16);assert(r.logicalValid);
    for(auto value:logical)assert(value==0xff); // blank is blank, never fabricated calibration
    const auto calls=io.calls;
    assert(reader.readLogical(physical,95,logical,16).status==DavStatus::invalid);
    assert(reader.readLogical(physical,96,physical+80,16).status==DavStatus::invalid);
    assert(reader.readPhysical(96,1,physical,96).status==DavStatus::invalid);
    assert(reader.readPhysical(95,2,physical,96).status==DavStatus::invalid);
    assert(reader.readPhysical(0,0,physical,96).status==DavStatus::invalid);
    assert(io.calls==calls);
    DavIo broken;broken.failAt=20;DavEfuseReader<DavIo> bad(broken);std::fill(logical,logical+16,0xcc);
    r=bad.readLogical(physical,96,logical,16);assert(!r.logicalValid);for(auto value:logical)assert(value==0xcc);
    DavIo reference;std::fill(reference.bank,reference.bank+96,0xff);DavEfuseReader<DavIo> rr(reference);
    assert(rr.readLogical(physical,96,logical,16).logicalValid);
    DavIo last;last.failAt=reference.lastDataReadCall;DavEfuseReader<DavIo> lastReader(last);
    std::fill(logical,logical+16,0xcc);r=lastReader.readLogical(physical,96,logical,16);
    assert(r.bytesRead==95&&!r.validBytes&&!r.logicalValid&&r.status==DavStatus::ioError);
    for(auto value:logical)assert(value==0xcc);for(unsigned i=0;i<95;++i)assert(physical[i]==0);
}
static void native(){
    fakeRfk::reset();IOPCIDevice device;IOMemoryMap map;MacDavEfuseIo io(&device,&map);assert(io.valid());
    map.set(0x270,0x00123456);uint8_t byte;uint32_t word;
    assert(io.read8(0x271,byte)&&byte==0x34&&io.read32(0x270,word)&&word==0x00123456);
    map.data[0x271]=0xff;assert(io.read8(0x271,byte)&&byte==0xff);
    assert(!io.read8(0x270,byte)&&!io.read32(0x274,word));
    assert(io.write32(0x270,0x80ff4063)&&map.get(0x270)==0x80ff4063);
    assert(!io.write32(0x270,0x81000063)); // busy SI must never be overwritten
    map.set(0x270,0);io.cancel();assert(io.cancelled()&&io.write32(0x270,0x81000063)); // cleanup after cancel
    for(uint32_t bad:{0x00000063u,0x82000063u,0x83000063u,0xc0ff4063u,0x80ff8063u,0x80ffc063u,0x80ff0063u,0x80ff6062u,0x80ff007au,0x81010063u,0x81000062u}){
        map.set(0x270,0);assert(!io.write32(0x270,bad)&&!davCommandAllowed(bad)&&map.get(0x270)==0);
    }
    assert(!io.write32(0x30,0x80000000u));assert(!io.delayUs(51)&&io.delayUs(50));
    for(unsigned config:{0u,0xffffu}){device.command=config;assert(!io.read32(0x270,word)&&!io.write32(0x270,0x81000063));}
    device.command=2;map.set(0x270,0xffffffffu);assert(!io.write32(0x270,0x81000063));
    device.vendor=0;MacDavEfuseIo wrong(&device,&map);assert(!wrong.valid());
    device.vendor=0x10ec;map.physical++;MacDavEfuseIo alias(&device,&map);assert(!alias.valid());
    map.physical--;map.length=0xff;MacDavEfuseIo shortMap(&device,&map);assert(!shortMap.valid());
    std::puts("DAV: native PCI/BAR/port/opcode whitelist and idle ownership checks passed");
}
int main(){complete();faults();decode();native();}
