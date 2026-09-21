// SPDX-License-Identifier: BSD-3-Clause
#include "../src/MmioProbe.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace rtl8852be;
struct Device {
    uint16_t cmd=0;
    uint64_t base=0x48d00000, bytes=0x100000;
    bool mapOk=true, enabled=true, restoreOk=true, mapped=false;
    unsigned maps=0, unmaps=0, reads=0;
    uint32_t cfg1=0x12341001, cfg2=0x12341001;
    std::vector<uint16_t> writes;
    uint16_t command() { return cmd; }
    void writeCommand(uint16_t v) {
        writes.push_back(v); assert(!(v & busMasterEnable));
        if ((v & memoryEnable) ? enabled : restoreOk) cmd=v;
    }
    uint64_t physical() { return base; }
    uint64_t length() { return bytes; }
    bool map() { ++maps; mapped=mapOk; return mapped; }
    void unmap() { assert(mapped); mapped=false; ++unmaps; }
    uint32_t read32(uint32_t offset) {
        assert(mapped && (cmd & memoryEnable) && !(cmd & busMasterEnable));
        assert(offset==sysCfg1 || offset==sysStatus1);
        ++reads;
        if (offset==sysStatus1) return 0x10;
        return reads==1 ? cfg1 : cfg2;
    }
};
Snapshot target() {
    Snapshot s{};
    s.vendorID=vendor; s.deviceID=device; s.subsystemVendor=0x1a3b; s.subsystemDevice=0x5470;
    s.capStatus=CapStatus::valid; s.pmControlReadable=true; s.bars[2]=0x48d00004;
    return s;
}
int main() {
    auto s=target(); Device d;
    auto r=sampleMmio(d,s);
    assert(r.status==MmioStatus::sampled && r.reads==3 && r.stableValue());
    assert(r.commandRestored && d.cmd==0 && d.writes==std::vector<uint16_t>({2,0}));
    assert(d.maps==1 && d.unmaps==1 && !d.mapped);
    d=Device{}; d.cmd=0x402;
    r=sampleMmio(d,s); assert(r.commandRestored && d.writes.empty() && d.reads==3);
    d=Device{}; d.cmd=0x400;
    r=sampleMmio(d,s); assert(r.commandRestored && d.writes==std::vector<uint16_t>({0x402,0x400}));
    d=Device{}; d.mapOk=false;
    r=sampleMmio(d,s); assert(r.status==MmioStatus::mapFailed && !d.reads && d.writes.empty());
    d=Device{}; d.enabled=false;
    r=sampleMmio(d,s); assert(r.status==MmioStatus::enableFailed && !d.reads && r.commandRestored && d.unmaps==1);
    d=Device{}; d.restoreOk=false;
    r=sampleMmio(d,s); assert(r.status==MmioStatus::restoreFailed && !r.commandRestored && d.unmaps==1);
    for (auto bad : {uint32_t(0),uint32_t(0xffffffff),uint32_t(0xdeadbeef)}) {
        d=Device{}; d.cfg1=d.cfg2=bad; r=sampleMmio(d,s); assert(!r.stableValue() && r.commandRestored);
    }
    d=Device{}; d.cfg2^=1; r=sampleMmio(d,s); assert(!r.stableValue());
    for (unsigned gate=0; gate<8; ++gate) {
        d=Device{}; s=target();
        switch(gate) {
        case 0: s.subsystemDevice=1; break;
        case 1: s.capStatus=CapStatus::cycle; break;
        case 2: s.pmControlReadable=false; break;
        case 3: s.pmControlStatus=3; break;
        case 4: d.cmd=4; break;
        case 5: s.bars[2]|=1; break;
        case 6: d.base+=4096; break;
        case 7: d.bytes=0x100; break;
        }
        r=sampleMmio(d,s); assert(d.maps==0 && d.writes.empty() && d.reads==0);
    }
    puts("PASS: MMIO gates, fixed reads, no DMA, mapping failures, command restoration and invalid values");
}
