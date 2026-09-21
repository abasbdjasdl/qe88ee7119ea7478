// SPDX-License-Identifier: BSD-3-Clause
#include "../src/XtalProbe.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace rtl8852be;
struct Device {
    uint16_t cmd=0;
    uint32_t cfg=0x0c491d39, power=1u<<17, initial=0, final=0x01001241;
    uint8_t data=0x12;
    uint64_t time=0, jump=0;
    unsigned readyAfter=2, polls=0, writes=0, waits=0, reads=0, byteReads=0, unmaps=0;
    bool mapped=false, mapOk=true, allowWrite=true, stuckClock=false, restoreOk=true;
    std::vector<uint16_t> commands;
    uint16_t command() { return cmd; }
    void writeCommand(uint16_t v) { commands.push_back(v);if((v&2)||restoreOk) cmd=v; }
    uint64_t physical() {return 0x48d00000;}
    uint64_t length() {return 0x100000;}
    bool map() {mapped=mapOk;return mapped;}
    void unmap() {assert(mapped);mapped=false;++unmaps;}
    uint64_t nowUs() {return time;}
    void pause50Us() {assert(mapped);++waits;if(!stuckClock)time+=50+jump;}
    uint32_t read32(uint32_t offset) {
        assert(mapped && (cmd&2) && !(cmd&4));++reads;
        switch(offset) {
        case sysCfg1: return cfg;
        case sysStatus1: return 0x1401f278;
        case 0: return 0;
        case sysPower: return power;
        case sysClock: return 0x4000;
        case firmwareControl: return 0;
        case xtalControl:
            if(!writes) return initial;
            ++polls;return polls<readyAfter?xtalCvReadCommand:final;
        default: assert(false);return 0;
        }
    }
    bool startXtalCvRead() {
        assert(mapped && (cmd&2) && !(cmd&4));
        if(!allowWrite)return false;
        assert(writes==0);++writes;return true;
    }
    uint8_t readXtalData() {assert(mapped && writes==1);++byteReads;return data;}
};
Snapshot target() {
    Snapshot s{};s.vendorID=vendor;s.deviceID=device;s.subsystemVendor=0x1a3b;s.subsystemDevice=0x5470;
    s.capStatus=CapStatus::valid;s.pmControlReadable=true;s.bars[2]=0x48d00004;return s;
}
XtalResult run(Device &d, bool expectRestore=true) {
    XtalResult x;const auto before=d.cmd;
    const auto r=sampleMmio(d,target(),[&](Device &dev,const MmioResult &b){x=sampleXtal(dev,b);});
    assert(!d.mapped && d.unmaps==1 && d.writes<=1);
    assert(r.commandRestored==expectRestore);
    if(expectRestore)assert(d.cmd==before);
    else assert(r.status==MmioStatus::restoreFailed);
    assert(d.reads==3+x.reads32 && d.byteReads==x.reads8 && d.writes==x.writes);
    return x;
}
int main() {
    static_assert(xtalCvReadCommand==((1u<<31)|(1u<<24)|0x41),"fixed analog read command");
    Device d;auto x=run(d);
    assert(x.status==XtalStatus::complete && x.revisionValid && x.rawRevision==0x12);
    assert(d.writes==1 && x.polls==2 && x.elapsedUs==50 && x.powerAfterSampled);
    assert(d.commands==std::vector<uint16_t>({2,0}));
    d=Device{};d.cmd=0x402;x=run(d);assert(x.revisionValid && d.commands.empty());
    for(unsigned gate=0;gate<6;++gate) {
        d=Device{};
        switch(gate) {
        case 0:d.cfg=0;break;
        case 1:d.cfg^=0x3000;break;
        case 2:d.power=0xffffffff;break;
        case 3:d.power=0;break;
        case 4:d.power|=1u<<22;break;
        case 5:d.initial=xtalCvReadCommand;break;
        }
        x=run(d);assert(d.writes==0 && d.polls==0 && !x.revisionValid);
    }
    for(auto bad:{uint32_t(0xffffffff),uint32_t(0xdeadbeef)}) {
        d=Device{};d.initial=bad;x=run(d);assert(x.status==XtalStatus::invalidControl && d.writes==0);
        d=Device{};d.final=bad;x=run(d);assert(x.status==XtalStatus::invalidControl && !x.powerAfterSampled && d.byteReads==0);
    }
    d=Device{};d.allowWrite=false;x=run(d);assert(x.status==XtalStatus::commandRejected && !x.writes);
    d=Device{};d.readyAfter=2000;x=run(d);
    assert(x.status==XtalStatus::timeout && x.elapsedUs==50000 && x.polls==1000 && !d.byteReads);
    assert(!x.powerAfterSampled && d.writes==1);
    d=Device{};d.readyAfter=2000;d.stuckClock=true;x=run(d);
    assert(x.status==XtalStatus::timeout && x.polls==1001 && d.waits==1000);
    d=Device{};d.jump=100000;x=run(d);
    assert(x.status==XtalStatus::timeout && x.polls==1 && !d.byteReads);
    d=Device{};d.data=255;x=run(d);assert(x.status==XtalStatus::invalidData && !x.revisionValid);
    d=Device{};d.data=0;x=run(d);assert(x.revisionValid && x.rawRevision==0);
    d=Device{};d.restoreOk=false;x=run(d,false);assert(x.status==XtalStatus::complete);
    d=Device{};d.readyAfter=2000;d.restoreOk=false;x=run(d,false);assert(x.status==XtalStatus::timeout);
    d=Device{};d.mapOk=false;bool called=false;
    auto result=sampleMmio(d,target(),[&](Device &,const MmioResult &){called=true;});
    assert(result.status==MmioStatus::mapFailed && !called && !d.reads && !d.writes);
    d=Device{};d.cmd=4;
    result=sampleMmio(d,target(),[&](Device &,const MmioResult &){called=true;});
    assert(result.status==MmioStatus::busMasterActive && !called && !d.reads && !d.writes);
    puts("PASS: fixed XTAL read, power/busy gates, timeout, stuck clock, invalid data and PCI restoration");
}
