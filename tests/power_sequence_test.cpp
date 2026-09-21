// SPDX-License-Identifier: BSD-3-Clause
#include "../src/PowerSequence.hpp"
#include <assert.h>
#include <stdio.h>
#include <map>
#include <vector>
using namespace rtl8852be;
namespace p=rtl8852be::power;
struct Device {
    uint16_t cmd=0;
    uint64_t time=0;
    std::map<uint32_t,uint32_t> regs{{0x4,0x50270482},{0x0,0x51dcfee1},{0x2,0x51},
        {0x88,0},{0xcc,0},{0x3f0,0},{0x270,0x01001141}};
    std::map<uint32_t,uint8_t> analog;
    std::vector<uint32_t> addresses;
    unsigned writeCalls=0,failWrite=0,unmaps=0;
    bool mapped=false,noOnAck=false,noOffAck=false,xtalStuck=false,stuckClock=false;
    bool jumpOnce=false,invalidControl=false;
    uint16_t command(){return cmd;}
    void writeCommand(uint16_t value){assert(!(value&4));cmd=value;}
    uint64_t physical(){return 0x48d00000;}
    uint64_t length(){return 0x100000;}
    bool map(){mapped=true;return true;}
    void unmap(){assert(mapped);mapped=false;++unmaps;}
    uint64_t nowUs(){return time;}
    void pauseUs(unsigned us){assert(us<=1000);if(!stuckClock)time+=us;if(jumpOnce){time+=2000000;jumpOnce=false;}}
    uint32_t read32(uint32_t a){
        assert(mapped&&(cmd&2)&&!(cmd&4));
        if(a==sysCfg1)return 0x0c491d39;
        if(a==sysStatus1)return 0x1401f278;
        if(a==xtalControl&&invalidControl)return 0xffffffff;
        return regs[a];
    }
    uint8_t read8(uint32_t a){return static_cast<uint8_t>(read32(a));}
    bool powerWrite32(uint32_t a,uint32_t value){
        assert(mapped&&(cmd&2)&&!(cmd&4)&&p::allowed32(a));
        // DMA/ring, EFUSE programming and RF/DMAC enable registers are forbidden.
        assert(a!=0x8400&&a!=0xc000&&a!=0x30&&a!=0x38);
        ++writeCalls;if(writeCalls==failWrite)return false;
        addresses.push_back(a);regs[a]=value;
        if(a==sysPower){
            if(value&0x100){if(!noOnAck){regs[a]&=~0x100u;regs[p::stateRegister]=0x100;}}
            if(value&0x200){if(!noOffAck){regs[a]&=~0x200u;regs[p::stateRegister]=0;}}
        }
        if(a==xtalControl){
            assert((value&0x83000000u)==0x80000000u); // analog WRITE mode only in this test
            const auto address=value&255, mask=(value>>16)&255,data=(value>>8)&255;
            assert(address==0x90||address==0xa1||address==0x24||address==0x26||address==0x80||address==0x81);
            analog[address]=static_cast<uint8_t>((analog[address]&~mask)|(data&mask));
            if(!xtalStuck)regs[a]&=~0x80000000u;
        }
        return true;
    }
    bool powerWrite8(uint32_t a,uint8_t value){
        assert(mapped&&(cmd&2)&&!(cmd&4)&&p::allowed8(a));
        ++writeCalls;if(writeCalls==failWrite)return false;
        addresses.push_back(a);regs[a]=value;return true;
    }
};
Snapshot target(){Snapshot s{};s.vendorID=vendor;s.deviceID=device;s.subsystemVendor=0x1a3b;
    s.subsystemDevice=0x5470;s.capStatus=CapStatus::valid;s.pmControlReadable=true;s.bars[2]=0x48d00004;return s;}
XtalResult preflight(){XtalResult x;x.status=XtalStatus::complete;x.revisionValid=true;x.rawRevision=0x11;return x;}
p::Result cycle(Device &d,XtalResult x=preflight()){
    p::Result result;const auto before=d.cmd;
    const auto mmio=sampleMmio(d,target(),[&](Device &dev,const MmioResult &m){result=p::cycle(dev,m,x);});
    assert(mmio.commandRestored&&d.cmd==before&&!d.mapped&&d.unmaps==1);
    return result;
}
int main(){
    Device d;auto r=cycle(d);
    assert(r.status==p::Status::completed&&r.activeObserved&&r.returnedOff&&r.cleanupAttempted);
    assert(r.on.step==sizeof(p::on)/sizeof(p::on[0])&&r.off.step==sizeof(p::off)/sizeof(p::off[0]));
    assert((d.regs[4]&0x400)&&!(d.regs[0xcc]&4)&&d.regs[p::stateRegister]==0);
    assert((d.analog[0x90]&0xff)==0x90); // OFF: SRAM2RFC/RFC2RF only
    const unsigned writes=d.writeCalls;
    // Every on/off write can fail once; no missed cleanup or false success.
    for(unsigned i=1;i<=writes;++i){
        d=Device{};d.failWrite=i;r=cycle(d);
        assert(r.cleanupAttempted&&r.status!=p::Status::completed);
        assert(r.on.error!=p::Error::none||r.off.error!=p::Error::none);
    }
    d=Device{};d.noOnAck=true;r=cycle(d);
    assert(r.on.error==p::Error::timeout&&r.cleanupAttempted&&r.status==p::Status::onFailedOff);
    d=Device{};d.noOffAck=true;r=cycle(d);
    assert(r.off.error==p::Error::timeout&&!r.returnedOff&&r.status==p::Status::cleanupFailed);
    d=Device{};d.xtalStuck=true;r=cycle(d);
    assert(r.on.error==p::Error::timeout&&r.off.error==p::Error::busy&&!r.returnedOff);
    d=Device{};d.xtalStuck=true;d.stuckClock=true;r=cycle(d);
    assert(r.on.error==p::Error::timeout&&r.on.polls<=1005&&!r.returnedOff);
    d=Device{};d.noOnAck=true;d.jumpOnce=true;r=cycle(d);
    assert(r.on.error==p::Error::deadline&&r.cleanupAttempted);
    d=Device{};d.invalidControl=true;r=cycle(d);
    assert(r.on.error==p::Error::invalidRead&&r.off.error==p::Error::invalidRead);
    for(auto state:{uint32_t(0x100),uint32_t(0x200),uint32_t(0x300),uint32_t(0xffffffff)}){
        d=Device{};d.regs[p::stateRegister]=state;r=cycle(d);
        assert(!r.attempted&&!r.cleanupAttempted&&d.writeCalls==0);
    }
    d=Device{};d.regs[0xcc]=4;r=cycle(d);assert(r.status==p::Status::dirtyWriteMask&&!d.writeCalls);
    d=Device{};auto x=preflight();x.rawRevision=0x12;r=cycle(d,x);
    assert(r.status==p::Status::preflightFailed&&!d.writeCalls);
    d=Device{};d.cmd=0x402;r=cycle(d);assert(r.returnedOff&&d.cmd==0x402);
    // No low-level writes to unlisted address widths.
    assert(!p::allowed32(0x88)&&!p::allowed8(0x4)&&!p::allowed32(0x8400));
    printf("PASS: supply on/off, %u injected write failures, timeout/deadline, no DMA and PCI restoration\n",writes);
}
