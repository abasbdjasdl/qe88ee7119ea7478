// SPDX-License-Identifier: BSD-3-Clause
// Register/PCI command model, not a physical Wi-Fi test.
#include "../src/network/PciRuntime.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
namespace n=rtl8852be::network;
struct Device {
    std::map<uint32_t,uint32_t> regs;
    std::vector<std::pair<uint32_t,uint32_t>> writes;
    uint16_t cmd=2;unsigned failWrite=0,ignoreWrite=0,barriers=0,pauseCount=0;
    uint64_t time=100;bool frozen=false,backwards=false,denyOff=false,partialOn=false;
    Device(){
        regs={{0x1000,0xc0d700},{0x1010,n::runtimeStopMask},{0x8380,3},
            {0x8400,0x60000000},{0xc000,0x40000000},{0x1e0,0xe0}};
        for(unsigned i=0;i<9;++i){const auto &r=n::ringRegisters[i];regs[r.low]=0x100000+i*4096;regs[r.count]=64;}
    }
    uint16_t command(){return cmd;}
    bool writeCommand(uint16_t c){
        writes.push_back({4,c});if(writes.size()==failWrite)return false;
        if(denyOff&&!(c&4))return false;
        if(writes.size()!=ignoreWrite)cmd=c;
        return !(partialOn&&(c&4));
    }
    uint32_t read32(uint32_t a){return regs[a];}
    uint16_t read16(uint32_t a){return uint16_t(regs[a]);}
    bool write32(uint32_t a,uint32_t v){
        writes.push_back({a,v});if(writes.size()==failWrite)return false;
        if(writes.size()==ignoreWrite)return true;
        for(unsigned i=0;i<3;++i)if(a==n::irqStatus[i]){regs[a]&=~v;return true;}
        regs[a]=v;return true;
    }
    bool write16(uint32_t a,uint16_t v){
        writes.push_back({a,v});if(writes.size()==failWrite)return false;
        assert((cmd&6)==6);if(writes.size()!=ignoreWrite)regs[a]=(regs[a]&0xffff0000)|v;return true;
    }
    void barrier(){++barriers;}
    uint64_t nowUs(){if(backwards&&pauseCount)return --time;return time;}
    void pauseUs(unsigned us){++pauseCount;if(!frozen)time+=us;}
};
void rings(n::RingMemory (&r)[9]){for(unsigned i=0;i<9;++i)r[i]={0x100000+i*4096,64};}
int main(){
    n::RingMemory r[9];rings(r);
    unsigned normalWrites=0;
    {
        Device d;n::PciRuntime<Device> p(d);assert(p.start(r)&&p.running()&&d.cmd==0x406);
        assert((d.regs[0x1010]&n::runtimeStopMask)==0&&(d.regs[0x1000]&0x2800)==0x2800);
        assert(p.enableInterrupts());normalWrites=unsigned(d.writes.size());
        d.regs[0x10b4]=0x00200005; // enabled RX/RPQ plus disabled retrain
        assert(!p.acknowledgeInterrupts().valid);assert(p.maskInterrupts());
        auto s=p.acknowledgeInterrupts();assert(s.valid&&s.pending()&&!s.fatal()&&s.dma==5&&d.regs[0x10b4]==0x00200000);
        // A new event during the masked drain survives rearming.
        d.regs[0x10b4]|=1;assert(p.enableInterrupts()&&(d.regs[0x10b4]&1));
        for(unsigned ring=0;ring<7;++ring){
            const auto a=n::ringRegisters[ring].index;
            for(unsigned i=0;i<10000;++i){const uint16_t next=(i+1)%64;assert(p.publish(ring,next));
                d.regs[a]=uint32_t(next)|(uint32_t(next)<<16);}
        }
        d.regs[0x1050]=5u<<16;assert(p.publish(7,3));assert(d.regs[0x1050]==(5u<<16|3));
        assert(p.publish(7,5));assert(p.stop()&&p.result.stopped&&p.result.masterOff&&p.result.idle);
        assert(!p.running()&&!(d.cmd&4)&&!p.publish(0,17)&&!p.start(r));
    }
    // Every start/IRQ-enable write failure, including BM activation, is stopped.
    for(unsigned at=1;at<=normalWrites;++at){
        Device d;d.failWrite=at;n::PciRuntime<Device> p(d);
        const bool ok=p.start(r)&&p.enableInterrupts();assert(!ok&&p.faulted());
        assert(p.stop()&&!(d.cmd&4));
    }
    // Ignored control writes must be caught by readback, not trusted blindly.
    for(unsigned at=4;at<=normalWrites;++at){
        Device d;d.ignoreWrite=at;n::PciRuntime<Device> p(d);
        assert(!(p.start(r)&&p.enableInterrupts()));assert(p.stop()&&!(d.cmd&4));
    }
    for(auto bad:{0xffffffffu,0xdeadbeefu}){
        Device d;d.regs[0x101c]=bad;n::PciRuntime<Device> p(d);assert(!p.start(r));
        assert(!p.stop()&&!p.result.stopped&&!(d.cmd&4));
    }
    for(unsigned ring=0;ring<9;++ring){
        Device d;d.regs[n::ringRegisters[ring].index]=1;n::PciRuntime<Device> p(d);assert(!p.start(r)&&!(d.cmd&4));
    }
    for(unsigned mode=0;mode<3;++mode){
        Device d;n::PciRuntime<Device> p(d);assert(p.start(r));d.regs[0x101c]=1;
        d.frozen=mode==1;d.backwards=mode==2;
        assert(!p.stop()&&!p.result.idle&&p.result.masterOff&&p.result.polls<=201);
    }
    {
        Device d;d.partialOn=true;n::PciRuntime<Device> p(d);assert(!p.start(r)&&(d.cmd&4));assert(p.stop()&&!(d.cmd&4));
    }
    {
        Device d;n::PciRuntime<Device> p(d);assert(p.start(r));d.denyOff=true;assert(!p.stop()&&!p.result.masterOff);
    }
    {
        Device d;n::PciRuntime<Device> p(d);assert(p.start(r));d.regs[0x10b4]=0xffffffff;
        assert(!p.acknowledgeInterrupts().valid&&p.faulted());assert(p.stop());
    }
    {
        Device d;n::PciRuntime<Device> p(d);assert(p.start(r));d.regs[0x1a4]=0x200000;
        auto s=p.acknowledgeInterrupts();assert(s.valid&&s.fatal());assert(p.stop());
    }
    {
        Device d;n::PciRuntime<Device> p(d);assert(p.start(r));assert(!p.publish(0,2)&&p.faulted());assert(p.stop());
    }
    {
        Device d;n::PciRuntime<Device> p(d);assert(p.start(r));
        for(unsigned i=1;i<64;++i)assert(p.publish(0,uint16_t(i)));
        assert(!p.publish(0,0));assert(p.stop());
    }
    {
        Device d;n::PciRuntime<Device> p(d);assert(p.start(r));d.regs[0x1054]=4u<<16;
        assert(!p.publish(8,5)&&p.faulted());assert(p.stop());
    }
    unsigned stopWrites=0;
    {Device d;n::PciRuntime<Device> p(d);assert(p.start(r));d.writes.clear();assert(p.stop());stopWrites=unsigned(d.writes.size());}
    for(unsigned at=1;at<=stopWrites;++at){Device d;n::PciRuntime<Device> p(d);assert(p.start(r));d.writes.clear();d.failWrite=at;
        assert(!p.stop()&&!p.result.stopped);if(at!=stopWrites)assert(!(d.cmd&4));}
    printf("PASS: PCI runtime model; 70000 TX wraps, RX bounds, W1C/rearm, %u start and %u stop write failures, idle/clock/partial-BM errors\n",normalWrites,stopWrites);
}
