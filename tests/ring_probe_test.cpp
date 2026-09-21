// SPDX-License-Identifier: BSD-3-Clause
#include "../src/RingProbe.hpp"
#include <assert.h>
#include <stdio.h>
#include <map>
#include <vector>
namespace q=rtl8852be::ring;
struct Device {
    uint16_t cmd=2;
    std::map<uint32_t,uint32_t> regs{{q::init,0x00202830},{q::stop,0x80000},
        {q::busy,0},{q::index,0},{q::low,0x12345000},{q::high,0},
        {q::ram,0x81020506},{q::num,0xa020}};
    unsigned writes=0,fail=0,drop=0;
    bool busyOnStop=false;
    std::vector<uint32_t> addresses;
    uint16_t command(){return cmd;}
    uint32_t read32(uint32_t a){return regs.at(a);}
    uint16_t read16(uint32_t a){assert(a==q::num);return static_cast<uint16_t>(regs.at(a));}
    bool write(uint32_t a,uint32_t v){
        assert(cmd==2);++writes;addresses.push_back(a);
        if(writes==fail)return false;
        if(writes!=drop)regs[a]=v;
        if(busyOnStop&&a==q::stop)regs[q::busy]=q::stopCh12;
        return true;
    }
    bool ringWrite32(uint32_t a,uint32_t v){assert(q::allowed32(a));return write(a,v);}
    bool ringWrite16(uint32_t a,uint16_t v){assert(q::allowed16(a));return write(a,v);}
};
const rtl8852be::dma::Mapping mapping{0x100000,4096,4096,1};
int main(){
    Device d;const auto initial=d.regs;auto r=q::probe(d,mapping);
    assert(r.status==q::Status::validated&&r.readbackOK&&r.restored&&d.regs==initial);
    assert(r.writeAttempts==6&&r.restoreAttempts==6&&d.writes==12);
    assert(r.configured.num==0xa100&&r.configured.ram==0x8101041c&&r.configured.low==mapping.address);
    assert(!(r.configured.init&q::hciEnable)&&(r.configured.stop&q::stopCh12));
    for(unsigned i=1;i<=12;++i){
        d=Device{};d.fail=i;r=q::probe(d,mapping);
        assert(r.status!=q::Status::validated&&r.restoreAttempts==6);
        if(i<=6)assert(r.restored&&d.regs==initial);
        else assert(r.status==q::Status::restoreFailed&&!r.restored);
    }
    for(unsigned i:{1u,2u,4u,5u,6u,7u,8u,9u,11u,12u}){
        // High is zero both before and after, so dropping that write is benign.
        d=Device{};d.drop=i;r=q::probe(d,mapping);
        assert(r.status!=q::Status::validated);
        if(i<=6)assert(r.restored&&d.regs==initial);
    }
    for(uint32_t address:{q::init,q::stop,q::busy,q::index,q::low,q::high,q::ram,q::num}){
        d=Device{};d.regs[address]=0xffffffff;r=q::probe(d,mapping);
        assert(!r.attempted&&!d.writes&&r.status==q::Status::invalidRead);
    }
    d=Device{};d.regs[q::busy]=q::stopCh12;r=q::probe(d,mapping);assert(r.status==q::Status::busy&&!d.writes);
    d=Device{};d.regs[q::index]=0x10001;r=q::probe(d,mapping);assert(r.status==q::Status::dirtyIndex&&!d.writes);
    d=Device{};d.cmd=6;r=q::probe(d,mapping);assert(r.status==q::Status::badCommand&&!d.writes);
    d=Device{};auto bad=mapping;bad.address=0x100000000ULL;r=q::probe(d,bad);assert(r.status==q::Status::badMapping&&!d.writes);
    d=Device{};d.busyOnStop=true;r=q::probe(d,mapping);assert(r.status==q::Status::readbackFailed&&r.restored&&r.writeAttempts==2);
    assert(!q::allowed32(q::index)&&!q::allowed32(0x1014)&&!q::allowed32(0x1018));
    assert(!q::allowed32(q::num)&&!q::allowed16(q::low));
    puts("PASS: stopped command ring, bounded register widths, 12 write failures, lost writes, restored state, no doorbell/DMA");
}
