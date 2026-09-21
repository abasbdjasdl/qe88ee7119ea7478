// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/PciRingSetup.hpp"
#include <cassert>
#include <cstdio>
#include <map>
namespace n=rtl8852be::network;
struct Device {
    std::map<uint32_t,uint32_t> registers;
    uint16_t pciCommand=2;unsigned writes{},failAt{},barrierViolation{},clockReads{};uint64_t clock{};
    bool stuckReset{},backwards{},freezeClock{},ignoreResetClear{};
    Device(){registers[0x1000]=0xd700;registers[0x1010]=0x1f0f00;registers[0x8380]=3;}
    uint16_t command(){return pciCommand;}
    uint32_t read32(uint32_t a){return registers[a];}
    uint16_t read16(uint32_t a){return uint16_t(registers[a]);}
    bool write32(uint32_t a,uint32_t value){
        if((pciCommand&6)!=2||(registers[0x1000]&0x2800)||(registers[0x1010]&0x1f0f00)!=0x1f0f00)++barrierViolation;
        if(++writes==failAt)return false;
        if(a==0x1000&&ignoreResetClear&&(registers[a]&8)&&!(value&8))return true;
        registers[a]=value;
        if(a==0x1000&&(value&8)&&!stuckReset)registers[a]&=~8u;
        return true;
    }
    bool write16(uint32_t a,uint16_t value){return write32(a,value);}
    uint64_t nowUs(){return backwards&&clockReads++>0?0:clock;}
    void pauseUs(unsigned us){if(!freezeClock)clock+=us;}
};
static void rings(n::RingMemory (&out)[9]){for(unsigned i=0;i<9;++i)out[i]={0x100000u+i*0x10000u,64};}
static void verifyRestored(Device &d){
    assert(d.command()==2&&!d.barrierViolation);
    for(const auto &r:n::ringRegisters){assert(!d.read32(r.low)&&!d.read32(r.high)&&!d.read16(r.count));if(r.bdram)assert(!d.read32(r.bdram));}
    assert(d.read32(0x1000)==0xd700&&d.read32(0x1010)==0x1f0f00);
}
int main(){
    n::RingMemory memories[9];rings(memories);Device d;n::PciRingSetup<Device> setup(d);
    assert(setup.configure(memories)&&setup.result.programmed&&setup.result.polls==1);
    assert(d.read32(0x1200)==0x020500&&d.read32(0x1228)==0x01041c);
    assert(d.read32(0x1014)==0x70f&&d.read32(0x1018)==3);
    assert(d.read32(0x1110)==0x100000&&d.read32(0x1100)==0x170000&&d.read32(0x1108)==0x180000);
    const unsigned count=d.writes;
    assert(count==37&&setup.restore());verifyRestored(d);
    assert(!setup.configure(memories));
    for(unsigned failure=1;failure<=count;++failure){Device fault;fault.failAt=failure;n::PciRingSetup<Device> test(fault);
        assert(!test.configure(memories)&&!test.result.programmed&&test.result.failureAddress);
        assert(test.restore()&&test.result.restored);verifyRestored(fault);
    }
    // Failed restore is observable, not a successful cleanup report.
    Device failRestore;n::PciRingSetup<Device> fr(failRestore);assert(fr.configure(memories));
    failRestore.failAt=failRestore.writes+2;assert(!fr.restore()&&!fr.result.restored);
    for(unsigned gate=0;gate<5;++gate){Device blocked;
        switch(gate){case 0:blocked.pciCommand=6;break;case 1:blocked.registers[0x1000]|=0x2000;break;
            case 2:blocked.registers[0x1010]=0;break;case 3:blocked.registers[0x101c]=1;break;case 4:blocked.registers[0x1a0]=1;break;}
        n::PciRingSetup<Device> test(blocked);assert(!test.configure(memories)&&!blocked.writes);
    }
    for(unsigned invalid=0;invalid<4;++invalid){rings(memories);
        switch(invalid){case 0:memories[0].address=0x100000000ULL;break;case 1:memories[1].address=memories[0].address;break;
            case 2:memories[0].count=1;break;case 3:memories[0].address++;break;}
        Device bad;n::PciRingSetup<Device> test(bad);assert(!test.configure(memories)&&!bad.writes);
    }
    rings(memories);
    for(unsigned mode=0;mode<3;++mode){Device timeout;timeout.stuckReset=true;timeout.freezeClock=mode==1;timeout.backwards=mode==2;
        if(timeout.backwards)timeout.clock=100;
        n::PciRingSetup<Device> test(timeout);assert(!test.configure(memories)&&test.result.polls<=201);
        if(mode==2)assert(!test.result.polls);
        assert(test.restore());verifyRestored(timeout);
    }
    Device live;live.registers[0x1110]=0x12340000;n::PciRingSetup<Device> occupied(live);
    assert(!occupied.configure(memories)&&!live.writes);
    Device ignored;ignored.stuckReset=true;ignored.ignoreResetClear=true;n::PciRingSetup<Device> stuck(ignored);
    assert(!stuck.configure(memories)&&!stuck.restore()&&!stuck.result.restored);
    puts("PASS: 9 stopped 8852BE PCI rings, all 37 setup-write failures, rollback failure, DMA/IRQ gates, bounded reset timeout and frozen clock");
}
