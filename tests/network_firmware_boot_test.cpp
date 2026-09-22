// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/FirmwareBootPreparation.hpp"
#include <assert.h>
#include <map>
#include <stdio.h>
namespace f=rtl8852be::firmwareboot;
namespace t=rtl8852be::transport;
struct Device {
    std::map<uint32_t,uint32_t> regs{{0x3f0,0x100}};
    uint64_t time{};unsigned ops{},failAt{},writes{},lateRead{};
    bool gate=true,cancel{},cleanup{},dleStuck{},cpuStuck{},frozen{};
    uint16_t cmd=2;
    bool inGate(){return gate;}bool cancelled(){return cancel;}
    uint16_t command(){return cmd;}uint64_t nowUs(){return time;}
    void beginCleanup(){cleanup=true;}void endCleanup(){cleanup=false;}
    bool access(){return ++ops!=failAt&&gate&&(!cancel||cleanup);}
    bool read32(uint32_t a,uint32_t &v){bool ok=access();v=regs[a];if(a==lateRead)time+=400000;return ok;}
    bool read16(uint32_t a,uint16_t &v){uint32_t x;bool ok=read32(a,x);v=uint16_t(x);return ok;}
    bool write32(uint32_t a,uint32_t v){if(!access())return false;++writes;regs[a]=v;
        if(a==0x8400&&(v&0x4800000)==0x4800000&&!dleStuck)regs[0x8d00]=regs[0x9100]=3;
        if(a==0x88&&(v&2)&&!cpuStuck)regs[0x1e0]|=2;
        return true;}
    bool write16(uint32_t a,uint16_t v){return write32(a,v);}
    bool delayUs(unsigned us){if(!frozen)time+=us;return gate&&(!cancel||cleanup);}
};
t::TransferResult completed(){t::TransferResult r{};r.status=t::TransferStatus::complete;r.quiesced=r.buffersReleased=true;return r;}
bool ready(Device &d,f::Preparation<Device> &p){
    if(!p.prepareDmac()||!p.enableCpuForDownload())return false;
    d.regs[0x1e0]=0xe6;return p.acceptDownload(completed());
}
int main(){
    Device d;f::Preparation<Device> p(d);assert(ready(d,p));const auto operations=d.ops;
    assert(p.result.cpuRunning&&p.result.downloadReleased&&p.result.stage==f::Stage::ready);
    assert((d.regs[0x88]&2)&&(d.regs[8]&0x4000)&&d.cmd==2);
    assert(!p.enableCpuForDownload()&&!p.prepareDmac());
    assert(p.stopCpu()&&!p.result.cpuRunning&&!(d.regs[0x88]&2)&&!(d.regs[8]&0x4000));
    for(unsigned n=1;n<=operations;++n){Device x;x.failAt=n;f::Preparation<Device> q(x);
        assert(!ready(x,q)&&!q.result.cpuRunning&&q.result.error!=f::Error::none);
        if(q.result.modified){x.failAt=0;assert(q.stopCpu());}}
    for(unsigned mode=0;mode<5;++mode){Device x;f::Preparation<Device> q(x);
        if(mode==0)x.gate=false;if(mode==1)x.cancel=true;if(mode==2)x.cmd=6;
        if(mode==3)x.cmd=0xffff;if(mode==4)x.regs[0x101c]=1;
        assert(!q.prepareDmac()&&!x.writes);}
    for(unsigned mode=0;mode<4;++mode){Device x;x.frozen=mode&1;x.dleStuck=mode<2;x.cpuStuck=mode>=2;
        f::Preparation<Device> q(x);assert(!ready(x,q)&&q.result.error==f::Error::timeout&&q.result.polls<=8003);}
    {Device x;f::Preparation<Device> q(x);assert(q.prepareDmac()&&q.enableCpuForDownload());
        x.regs[0x1e0]=0xe6;auto r=completed();r.retainBuffers=true;assert(!q.acceptDownload(r)&&q.result.error==f::Error::download);}
    {Device x;f::Preparation<Device> q(x);assert(ready(x,q));x.cancel=true;assert(q.stopCpu());}
    {Device x;f::Preparation<Device> q(x);assert(ready(x,q));x.regs[0x101c]=1;
        assert(!q.stopCpu()&&q.result.requiresRecovery&&q.result.cpuRunning);}
    {Device x;f::Preparation<Device> q(x);assert(q.prepareDmac());x.lateRead=0x1e0;
        assert(!q.enableCpuForDownload()&&q.result.error==f::Error::timeout);}
    assert(f::prepAddress(0xf0,4,false)&&!f::prepAddress(0xf0,4,true));
    printf("PASS: persistent firmware CPU handoff, explicit idle shutdown, %u I/O faults, cancellation and bounded readiness\n",operations);
}
