// SPDX-License-Identifier: BSD-3-Clause
// Compile actual native interrupt owner against explicit OS/MMIO models.
#include "../src/network/MacPciInterrupts.cpp"
#include <cstdio>
namespace n=rtl8852be::network;
namespace rtl8852be { namespace network {
MacPciRuntimeIo::MacPciRuntimeIo(IOPCIDevice *d,IOMemoryMap *m):device_(d),mapping_(m){}
uint16_t MacPciRuntimeIo::command(){return device_->cmd;}
bool MacPciRuntimeIo::writeCommand(uint16_t c){device_->cmd=c;return true;}
uint16_t MacPciRuntimeIo::read16(uint32_t a){return uint16_t(device_->regs[a]);}
uint32_t MacPciRuntimeIo::read32(uint32_t a){return device_->regs[a];}
bool MacPciRuntimeIo::write16(uint32_t a,uint16_t v){device_->regs[a]=(device_->regs[a]&0xffff0000)|v;return true;}
bool MacPciRuntimeIo::write32(uint32_t a,uint32_t v){
    for(auto s:irqStatus)if(a==s){device_->regs[a]&=~v;return true;}device_->regs[a]=v;return true;
}
void MacPciRuntimeIo::barrier(){}uint64_t MacPciRuntimeIo::nowUs(){return 0;}void MacPciRuntimeIo::pauseUs(unsigned){}
template class PciRuntime<MacPciRuntimeIo>;
} }
struct Context {
    IOPCIDevice *d;R16PciInterrupts *pump;unsigned calls{},faults{};
    R16PciInterrupts::ServiceResult outcome=R16PciInterrupts::ServiceResult::drained;
    bool stopInCallback{},eventDuringDrain{};
    static R16PciInterrupts::ServiceResult service(void *p,const n::InterruptStatus &){
        auto &c=*static_cast<Context *>(p);++c.calls;
        for(auto a:n::irqMasks)assert(c.d->regs[a]==0);
        assert(!c.pump->detach()); // A live callback forbids source destruction.
        if(c.eventDuringDrain)c.d->regs[0x10b4]|=4;
        if(c.stopInCallback){assert(!c.pump->stop());assert(c.pump->servicing());assert(!c.pump->detach());}
        return c.outcome;
    }
    static void fault(void *p,const n::InterruptStatus &){auto &c=*static_cast<Context *>(p);++c.faults;assert(!(c.d->cmd&4));assert(!c.pump->detach());}
};
void setup(IOPCIDevice &d,n::RingMemory (&r)[9]){
    d.regs={{0x1000,0xc0d700},{0x1010,n::runtimeStopMask},{0x8380,3},{0x8400,0x60000000},{0xc000,0x40000000},{0x1e0,0xe0}};
    for(unsigned i=0;i<9;++i){r[i]={0x100000+i*4096,64};d.regs[n::ringRegisters[i].low]=uint32_t(r[i].address);d.regs[n::ringRegisters[i].count]=64;}
}
int main(){
    {IOPCIDevice d;IOWorkLoop l;auto *p=new R16PciInterrupts;
     assert(p->attach(&d,&l,0)&&d.cmd==0x402);assert(p->detach());p->release();}
    {IOPCIDevice d;IOWorkLoop l;auto *p=new R16PciInterrupts;d.ignoreBusMasterDisable=true;
     assert(!p->attach(&d,&l,0)&&l.sources.empty());p->release();}
    // Failure after both sources were added must clear both flags. A retry
    // that fails timer addition must not remove an unregistered timer.
    {IOPCIDevice d;IOWorkLoop l;auto *p=new R16PciInterrupts;d.ignoreBusMasterDisable=true;
     assert(!p->attach(&d,&l,0)&&l.sources.empty());
     d.ignoreBusMasterDisable=false;d.cmd=2;l.failAdd=l.adds+2;
     assert(!p->attach(&d,&l,0)&&l.sources.empty());p->release();}
    {IOPCIDevice d;IOWorkLoop l;auto *p=new R16PciInterrupts;d.cmd=0x406;
     assert(!p->attach(&d,&l,0)&&d.cmd==0x406);p->release();}
    for(unsigned failure=1;failure<=4;++failure){
        IOPCIDevice d;IOWorkLoop l;auto *p=new R16PciInterrupts;
        irqfake::allocation=0;irqfake::failAllocation=failure<=2?int(failure):0;l.failAdd=failure>2?failure-2:0;
        assert(!p->attach(&d,&l,0)&&l.sources.empty()&&!irqfake::live&&l.refs==1);p->release();
    }
    irqfake::failAllocation=0;
    {IOPCIDevice d;IOWorkLoop l;auto *p=new R16PciInterrupts;d.interruptType=0;assert(!p->attach(&d,&l,0));p->release();}
    for(unsigned mode=0;mode<7;++mode){
        IOPCIDevice d;IOWorkLoop l;IOMemoryMap map;n::RingMemory r[9];setup(d,r);
        n::MacPciRuntimeIo io(&d,&map);R16PciInterrupts::Runtime runtime(io);
        auto *pump=new R16PciInterrupts;assert(pump->attach(&d,&l,0)&&l.sources.size()==2);
        auto *irq=static_cast<IOInterruptEventSource *>(l.sources[0]);auto *timer=static_cast<IOTimerEventSource *>(l.sources[1]);
        Context c{&d,pump};assert(runtime.start(r));
        if(mode==6)irqfake::armError=7;
        const bool started=pump->start(&runtime,Context::service,Context::fault,&c);
        irqfake::armError=0;
        if(mode==6){assert(!started&&!(d.cmd&4));assert(pump->detach());pump->release();continue;}
        assert(started&&irq->enabled&&timer->armed&&timer->delay==1);
        if(mode==0){c.eventDuringDrain=true;irq->fire();assert(c.calls==1&&d.regs[0x10b4]==4&&timer->delay==10);
            c.eventDuringDrain=false;timer->fire();assert(c.calls==2&&d.regs[0x10b4]==0);}
        if(mode==1){c.outcome=R16PciInterrupts::ServiceResult::more;irq->fire();assert(c.calls==1&&timer->delay==1&&runtime.result.irqMasked);
            c.outcome=R16PciInterrupts::ServiceResult::drained;timer->fire();assert(c.calls==2&&!runtime.result.irqMasked);}
        if(mode==2){d.regs[0x1a4]=0x200000;irq->fire();assert(c.faults==1&&c.calls==0&&!irq->enabled&&!timer->armed);}
        if(mode==3){c.stopInCallback=true;irq->fire();assert(c.calls==1&&!irq->enabled&&!timer->armed&&!(d.cmd&4));}
        if(mode==4){irqfake::armError=8;irq->fire();irqfake::armError=0;assert(c.calls==1&&c.faults==1&&!(d.cmd&4));}
        if(mode==5){c.outcome=R16PciInterrupts::ServiceResult::fault;irq->fire();assert(c.calls==1&&c.faults==1);}
        assert(pump->stop());const auto calls=c.calls;irq->stale();timer->stale();assert(c.calls==calls);
        assert(pump->detach()&&l.sources.empty()&&!irqfake::live&&l.refs==1);pump->release();
    }
    // Last-release fallback must serialize source removal even off the gate.
    {IOPCIDevice d;IOWorkLoop l;auto *p=new R16PciInterrupts;assert(p->attach(&d,&l,0));l.gate=false;p->release();assert(!l.gate&&l.sources.empty()&&!irqfake::live&&l.refs==1);}
    puts("PASS: native MSI workloop owner with model APIs; allocation/add/arm failures, budget continuation, reentrant stop, W1C race, fatal event, stale callback, gated teardown");
}
