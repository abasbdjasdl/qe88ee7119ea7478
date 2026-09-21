// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/EfuseReader.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace rtl8852be::network;
struct Io {
    uint16_t iso=0x110;uint8_t pmc=0x80;uint32_t burst=0x123456,address=0;
    uint64_t clock{};size_t calls{},failAt=size_t(-1),cancelAt=size_t(-1),clockCalls{},backAt=size_t(-1),writes{},polls{};
    bool stuck{},freeze{},removed{},ignoreOff{},wrongCommand{};unsigned requested{},delayFailureAt=~0u,delays{};
    std::vector<uint32_t> requests;
    bool cancelled()const{return polls>=cancelAt;}
    bool access(){return !removed&&calls++!=failAt;}
    bool read8(uint32_t a,uint8_t &v){assert(a==0xcc);if(!access())return false;v=pmc;return true;}
    bool read16(uint32_t a,uint16_t &v){assert(a==0);if(!access())return false;v=iso;return true;}
    bool read32(uint32_t a,uint32_t &v){
        if(!access())return false;if(a==0x38)v=burst;
        else {assert(a==0x30);++polls;v=(address<<16)|(stuck?0:0x20000000)|uint8_t(address^0xa5);}
        return true;
    }
    bool write8(uint32_t a,uint8_t v){assert(a==0xcc);if(!access())return false;++writes;pmc=v;return true;}
    bool write16(uint32_t a,uint16_t v){assert(a==0);if(!access())return false;++writes;if(!(ignoreOff&&(iso&~v&0xc000)))iso=v;return true;}
    bool write32(uint32_t a,uint32_t v){
        if(!access())return false;++writes;
        if(a==0x38)burst=v;
        else {assert(a==0x30);wrongCommand|=(v&~0x07ff0000u)!=0||(iso&0xc100)!=0xc000||!(pmc&4);address=v>>16;requests.push_back(v);++requested;}
        return true;
    }
    bool delayUs(unsigned us){if(delays++==delayFailureAt)return false;if(!freeze)clock+=us;return true;}
    uint64_t nowUs(){if(clockCalls++==backAt)return 0;return clock+1;}
};
void success(){
    for(uint8_t cut:{uint8_t(0),uint8_t(1)}){
        Io io;EfuseReader<Io> reader(io);uint8_t out[1216];auto r=reader.readDdv(cut,0,sizeof out,out,sizeof out);
        assert(r.status==EfuseReadStatus::ok&&r.primary==EfuseReadStatus::ok&&r.restored&&r.cleanupAttempted&&r.validBytes==sizeof out);
        assert(io.iso==0x110&&io.pmc==0x80&&io.burst==0x123456&&!io.wrongCommand);
        for(unsigned i=0;i<sizeof out;++i)assert(out[i]==uint8_t(i^0xa5)&&io.requests[i]==i<<16);
        uint8_t phy[128];r=reader.readDdv(cut,0x580,sizeof phy,phy,sizeof phy);
        assert(r.status==EfuseReadStatus::ok&&r.validBytes==128&&phy[0]==uint8_t(0x580^0xa5));
    }
}
void failures(){
    uint8_t out[16];Io reference;EfuseReader<Io> reader(reference);
    auto ok=reader.readDdv(0,0,16,out,16);assert(ok.status==EfuseReadStatus::ok);
    const auto total=reference.calls;
    // Every raw read/write operation can fail once, including cleanup accesses.
    for(size_t at=0;at<total;++at){
        Io io;io.failAt=at;EfuseReader<Io> r(io);std::memset(out,0xcc,sizeof out);
        auto result=r.readDdv(0,0,16,out,16);
        assert(result.status!=EfuseReadStatus::ok&&result.validBytes==0);
        if(result.cleanupAttempted&&result.status!=EfuseReadStatus::cleanupFailed){
            assert(result.restored&&io.iso==0x110&&io.pmc==0x80&&io.burst==0x123456);
        }
        for(size_t i=0;i<result.bytesRead;++i)assert(out[i]==0);
    }
    for(unsigned at=0;at<reference.delays;++at){
        Io io;io.delayFailureAt=at;EfuseReader<Io> r(io);auto result=r.readDdv(0,0,16,out,16);
        assert(result.status!=EfuseReadStatus::ok&&!result.validBytes);
    }
    Io cancel;cancel.cancelAt=4;EfuseReader<Io> c(cancel);auto cr=c.readDdv(0,0,16,out,16);
    assert(cr.status==EfuseReadStatus::cancelled&&cr.bytesRead==4&&cr.validBytes==0&&cr.restored);
    Io before;before.cancelAt=0;EfuseReader<Io> b(before);assert(b.readDdv(0,0,16,out,16).status==EfuseReadStatus::cancelled&&!before.writes);
    for(bool freeze:{false,true}){
        Io timeout;timeout.stuck=true;timeout.freeze=freeze;EfuseReader<Io> t(timeout);auto tr=t.readDdv(0,0,16,out,16);
        assert(tr.status==EfuseReadStatus::timeout&&tr.restored&&!tr.validBytes&&tr.polls<=1000000);
    }
    Io backwards;backwards.backAt=5;EfuseReader<Io> back(backwards);auto br=back.readDdv(0,0,16,out,16);
    assert(br.status==EfuseReadStatus::clockError&&br.restored&&!br.validBytes);
    Io lost;lost.removed=true;EfuseReader<Io> l(lost);assert(l.readDdv(0,0,16,out,16).status==EfuseReadStatus::ioError);
    Io ignored;ignored.ignoreOff=true;EfuseReader<Io> i(ignored);auto ir=i.readDdv(0,0,16,out,16);
    assert(ir.status==EfuseReadStatus::cleanupFailed&&!ir.restored&&!ir.validBytes);
    Io busy;busy.iso|=0x4000;EfuseReader<Io> bi(busy);assert(bi.readDdv(0,0,16,out,16).status==EfuseReadStatus::busy&&!busy.writes);
    std::printf("DDV: full main/PHY reads and all %zu I/O failure points, cancelled/timeout/frozen/backwards clock and cleanup failures passed\n",total);
}
void invalid(){
    uint8_t out[16];Io io;EfuseReader<Io> r(io);
    assert(r.readDdv(0,0,17,out,16).status==EfuseReadStatus::invalid);
    assert(r.readDdv(0,0,0,out,16).status==EfuseReadStatus::invalid);
    assert(r.readDdv(0,0,16,nullptr,16).status==EfuseReadStatus::invalid);
    assert(r.readDdv(0,1215,2,out,16).status==EfuseReadStatus::invalid);
    assert(r.readDdv(0,0x500,2,out,16).status==EfuseReadStatus::invalid);
    assert(r.readDdv(0,0x5ff,2,out,16).status==EfuseReadStatus::invalid);
    assert(r.readDdv(0,0xffffffff,2,out,16).status==EfuseReadStatus::invalid&&!io.calls&&!io.writes);
}
int main(){success();failures();invalid();}
