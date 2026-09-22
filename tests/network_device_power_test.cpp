// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/DevicePower.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
namespace p=rtl8852be::powerseq;
namespace n=rtl8852be::network;
struct Device {
    struct Op {unsigned a,v,width;bool write;};
    std::map<unsigned,uint8_t> bytes,xtal;std::vector<Op> trace;
    unsigned operations{},failAt{},failDelay{},delayCalls{},ignoreAt{},cancelAt{},removeAt{};
    uint16_t cmd=2;uint64_t time=100;unsigned lateReadAt{},lateBy{};
    bool gate=true,cancel{},frozen{},backwards{},stuckOn{},stuckOff{},siStuck{},siWrong{},noPowerState{};
    Device(){put(4,0x28400,4);put(0,0xc000,4);put(0x88,0xa4,1);put(0x7a,0xbcd5,2);put(0x228,0x1122,2);
        put(0x200,0x410100c3,4);put(0x400,0xabcdef00,4);put(0x2d8,0x123400ef,4);
        for(unsigned a:{0x90u,0xa1u,0x24u,0x26u,0x80u,0x81u})xtal[a]=0xff;}
    void put(unsigned a,unsigned v,unsigned width){for(unsigned i=0;i<width;++i)bytes[a+i]=uint8_t(v>>(8*i));}
    unsigned get(unsigned a,unsigned width=4){unsigned v=0;for(unsigned i=0;i<width;++i)v|=unsigned(bytes[a+i])<<(8*i);return v;}
    bool read(unsigned a,unsigned width,unsigned &v){
        assert(p::powerAddress(a,width,false));++operations;v=get(a,width);trace.push_back({a,v,width,false});
        if(operations==lateReadAt)time+=lateBy;return operations!=failAt;
    }
    bool write(unsigned a,unsigned v,unsigned width){
        assert(p::powerAddress(a,width,true));++operations;trace.push_back({a,v,width,true});
        if(operations==failAt)return false;if(operations==ignoreAt)return true;put(a,v,width);
        if(a==4){
            if((v&0x100)&&!stuckOn){put(a,v&~0x100u,4);if(!noPowerState)put(0x3f0,0x100,4);}
            if((v&0x200)&&!stuckOff){put(a,v&~0x200u,4);if(!noPowerState)put(0x3f0,0,4);}
        }
        if(a==0x270){assert(p::powerSiCommand(v));if(!siStuck){
            const unsigned offset=v&255,mask=(v>>16)&255;
            if(!(v&0x03000000))xtal[offset]=uint8_t((xtal[offset]&~mask)|((v>>8)&mask));
            else v=(v&~0xff00u)|(unsigned(siWrong?0:xtal[offset])<<8);
            put(a,v&~0x80000000u,4);
        }}return true;
    }
    bool read8(unsigned a,uint8_t &v){unsigned x;const bool ok=read(a,1,x);v=uint8_t(x);return ok;}
    bool read16(unsigned a,uint16_t &v){unsigned x;const bool ok=read(a,2,x);v=uint16_t(x);return ok;}
    bool read32(unsigned a,uint32_t &v){return read(a,4,v);}
    bool write8(unsigned a,uint8_t v){return write(a,v,1);}bool write16(unsigned a,uint16_t v){return write(a,v,2);}
    bool write32(unsigned a,uint32_t v){return write(a,v,4);}
    bool inGate(){return gate;}bool cancelled(){return cancel||(cancelAt&&operations>=cancelAt);}
    uint16_t command(){return removeAt&&operations>=removeAt?0xffff:cmd;}
    uint64_t nowUs(){return backwards&&delayCalls?--time:time;}
    bool delayUs(unsigned us){++delayCalls;if(!frozen)time+=us;return delayCalls!=failDelay;}
};
n::CalibrationSnapshot snapshot(unsigned cut=1,bool power=false,unsigned rfe=5){
    n::CalibrationSnapshot s{};s.board.identityValid=true;s.board.rfe=uint8_t(rfe);s.cut=uint8_t(cut);s.phy.powerValid=power;return s;
}
void show(const p::ActionResult &r){fprintf(stderr,"power error=%u address=%x wanted=%x got=%x read=%u write=%u poll=%u\n",
    unsigned(r.error),r.address,r.expected,r.actual,r.reads,r.writes,r.polls);}
int main(){
    auto s=snapshot();Device d;p::DevicePower<Device> power(d);
    const bool ok=power.start(1,&s);if(!ok)show(power.result.on);assert(ok);
    const unsigned onOps=d.operations;const auto onTrace=d.trace;
    assert(power.result.powered&&power.result.on.complete&&power.result.on.pmcClosed&&!power.result.requiresRecovery);
    assert(d.get(0x3f0)==0x100&&d.get(0x8400)==0x7fff8000&&d.get(0xc000)==0x7000803f);
    assert(d.get(0x7a,2)==0xbcda&&d.get(0x200)==0x428100c9&&!(d.get(0xcc)&4));
    assert(d.get(0x400)==0xabcdef31&&d.get(0x2d8)==0x1234001f&&d.get(0xaf,1)==0x81);
    assert(d.get(0x88,1)==0xa5&&d.xtal[0x90]==0x6f&&d.xtal[0xa1]==0xfd&&d.xtal[0x24]==0x8f&&d.xtal[0x26]==0xf0);
    std::vector<unsigned> platform;for(auto op:d.trace)if(op.write&&op.a==0x88)platform.push_back(op.v&1);
    assert((platform==std::vector<unsigned>{1,0,1,0,1}));
    assert(!power.start(1,&s));
    const bool stopped=power.stop(&s,true);if(!stopped)show(power.result.off);assert(stopped);
    const unsigned offOps=d.operations-onOps;
    assert(power.result.returnedOff&&!power.result.powered&&!power.result.requiresRecovery&&power.result.off.pmcClosed);
    assert(d.get(0x3f0)==0&&d.get(0x228,2)==0x4a82&&d.get(0x90)==0x1a0b2&&(d.get(4)&0x400));
    assert(d.get(0xaf,1)==0x80&&d.xtal[0x90]==0x90&&d.xtal[0x80]==0xfe&&d.xtal[0x81]==0xfe);
    assert(!power.stop(&s,true));
    // Fault every physical read/write including the final cleanup read. Failure
    // must not be turned into readiness. A later explicit off can recover a
    // partially written on sequence, with DMA/interrupt state verified first.
    for(unsigned i=1;i<=onOps;++i){Device f;f.failAt=i;p::DevicePower<Device> q(f);
        assert(!q.start(1,&s)&&!q.result.powered&&!q.result.on.complete&&q.result.on.error!=p::Error::none);
        assert(!(f.get(0xcc)&4));f.failAt=0;
        if(q.result.on.writes)assert(q.stop(&s,true)&&q.result.returnedOff&&!q.result.requiresRecovery);
        else assert(!q.stop(&s,true));
    }
    for(unsigned i=1;i<=offOps;++i){Device f;p::DevicePower<Device> q(f);assert(q.start(1,&s));f.failAt=f.operations+i;
        assert(!q.stop(&s,true)&&!q.result.returnedOff&&q.result.requiresRecovery&&q.result.off.error!=p::Error::none);
        assert(!(f.get(0xcc)&4));}
    for(unsigned cut=0;cut<2;++cut)for(unsigned calibrated=0;calibrated<2;++calibrated)for(unsigned known=0;known<2;++known){
        Device f;auto c=snapshot(cut,calibrated);p::DevicePower<Device> q(f);assert(q.start(uint8_t(cut),known?&c:nullptr));
        assert(f.get(0x200)==(known&&!calibrated?0x428100c9u:0x410100c3u));
        assert(f.get(0x7a,2)==(known&&!calibrated&&cut?0xbcdau:0xbcd5u));assert(q.stop(&c,false)&&f.get(0x228,2)==0x1122);
    }
    for(unsigned rfe:{0u,4u,5u,6u}){Device f;auto c=snapshot(1,false,rfe);p::DevicePower<Device> q(f);
        assert(q.start(1,&c)&&q.stop(&c,true));assert(f.get(0x228,2)==(rfe==5?0x4a82u:0x1122u));}
    // A still-active DMA/interrupt engine must block shutdown before any writes.
    for(unsigned a:{0x1000u,0x101cu,0x1a0u,0x10b0u,0x13b0u}){Device f;p::DevicePower<Device> q(f);assert(q.start(1,&s));
        f.put(a,a==0x1000?0x2800:a==0x101c?0x100:1,4);assert(!q.stop(&s,false)&&!q.result.off.writes&&q.result.requiresRecovery);}
    for(unsigned mode=0;mode<9;++mode){Device f;
        if(mode==0)f.stuckOn=true;if(mode==1){f.stuckOn=true;f.frozen=true;}if(mode==2)f.siStuck=true;
        if(mode==3){f.siStuck=true;f.frozen=true;}if(mode==4)f.siWrong=true;if(mode==5)f.noPowerState=true;
        if(mode==6){f.stuckOn=true;f.backwards=true;}if(mode==7)f.failDelay=1;if(mode==8)f.put(0x270,0x80000090,4);
        p::DevicePower<Device> q(f);assert(!q.start(1,&s)&&q.result.on.error!=p::Error::none&&q.result.on.polls<1200);
        assert(!(f.get(0xcc)&4));
    }
    {Device f;p::DevicePower<Device> q(f);assert(q.start(1,&s));f.stuckOff=true;assert(!q.stop(&s,false)&&q.result.off.error==p::Error::timeout);}
    // Cancellation is honored immediately by on; the bounded off cleanup still
    // runs on a live device after cancel. PCI removal never permits cleanup I/O.
    for(unsigned i=1;i<onOps;++i){Device f;f.cancelAt=i;p::DevicePower<Device> q(f);assert(!q.start(1,&s));
        assert(!(f.get(0xcc)&4));if(q.result.on.writes)assert(q.stop(&s,true));}
    {Device f;f.removeAt=10;p::DevicePower<Device> q(f);assert(!q.start(1,&s));assert(f.operations==10&&!q.stop(&s,true));}
    for(unsigned cmd:{0u,4u,6u,0xffffu}){Device f;f.cmd=uint16_t(cmd);p::DevicePower<Device> q(f);assert(!q.start(1,&s)&&!f.operations);
        assert(q.result.on.error==p::Error::precondition&&q.result.on.address==4&&q.result.on.expected==2&&q.result.on.actual==cmd);}
    for(unsigned mode=0;mode<5;++mode){Device f;auto c=s;
        if(mode==0)f.gate=false;if(mode==1)f.cancel=true;if(mode==2)f.put(0x3f0,0x100,4);
        if(mode==3)f.put(0xcc,4,4);if(mode==4)c.board.identityValid=false;
        p::DevicePower<Device> q(f);assert(!q.start(1,&c)&&!q.result.on.writes);if(mode==3)assert(f.get(0xcc)&4);
        if(mode==0||mode==2||mode==3){assert(q.result.on.error==p::Error::precondition);
            assert(q.result.on.address==(mode==0?0u:mode==2?0x3f0u:0xccu));
            assert(q.result.on.expected==0&&q.result.on.actual==(mode==0?0u:mode==2?0x100u:4u));}}
    // Ready sampled only after the source's 20 ms poll timeout is not accepted.
    {Device f;unsigned ready=0;for(unsigned i=0;i<onTrace.size();++i)
        if(onTrace[i].write&&onTrace[i].a==4&&(onTrace[i].v&0x100)){ready=i+2;break;}
        assert(ready);f.lateReadAt=ready;f.lateBy=20001;p::DevicePower<Device> q(f);const bool lateOk=q.start(1,&s);
        if(lateOk||q.result.on.error!=p::Error::timeout){fprintf(stderr,"late ready operation=%u time=%llu ok=%u\n",ready,(unsigned long long)f.time,unsigned(lateOk));show(q.result.on);}
        assert(!lateOk&&q.result.on.error==p::Error::timeout);}
    // A write reported successful but physically ignored cannot fake on completion.
    for(unsigned address:{0x8400u,0xc000u,0x7au,0x200u}){Device f;unsigned index=0;
        for(unsigned i=0;i<onTrace.size();++i)if(onTrace[i].write&&onTrace[i].a==address){index=i+1;break;}
        assert(index);f.ignoreAt=index;p::DevicePower<Device> q(f);assert(!q.start(1,&s)&&q.result.on.error==p::Error::readback);}
    printf("PASS: complete source chip power on/off, %u on + %u off I/O faults, RFE5/probe/cut/power-calibration branches, serial readback/timeouts, cancellation cleanup and DMA/IRQ shutdown prerequisites; modeled hardware\n",onOps,offOps);
}
