// SPDX-License-Identifier: BSD-3-Clause
// Test-only secondary-interrupt/workloop model. No OS interrupt is registered.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <vector>
#include <map>
#include <cassert>
using IOReturn=int;
constexpr int kIOReturnSuccess=0,kIOInterruptTypePCIMessaged=4;
constexpr unsigned kIOPCIConfigCommand=4;
struct OSObject {
    unsigned refs=1;virtual ~OSObject()=default;
    void retain(){++refs;}void release(){assert(refs);if(!--refs)free();}
    virtual void free(){delete this;}
};
#define OSDeclareDefaultStructors(c)
#define OSDefineMetaClassAndStructors(c,s)
inline void IOLog(const char *,...){}
struct IOMemoryMap {};struct IOMemoryDescriptor {};
struct IOPCIDevice {
    int interruptType=kIOInterruptTypePCIMessaged,typeResult=0;
    uint16_t cmd=2;std::map<uint32_t,uint32_t> regs;
    bool ignoreBusMasterDisable=false;
    uint16_t configRead16(unsigned a){assert(a==4);return cmd;}
    void setBusMasterEnable(bool b){if(b)cmd|=4;else if(!ignoreBusMasterDisable)cmd&=~4;}
    IOReturn getInterruptType(int,int *type){*type=interruptType;return typeResult;}
};
struct IOWorkLoop;
struct IOEventSource:OSObject {
    OSObject *owner{};IOWorkLoop *loop{};bool enabled=true;
    void enable(){enabled=true;}void disable(){enabled=false;}
};
namespace irqfake {static int allocation=0,failAllocation=0,armError=0,live=0;}
struct IOInterruptEventSource:IOEventSource {
    using Action=void (*)(OSObject *,IOInterruptEventSource *,int);Action action{};
    static IOInterruptEventSource *interruptEventSource(OSObject *o,Action a,IOPCIDevice *d,int){
        if(++irqfake::allocation==irqfake::failAllocation)return nullptr;
        d->cmd|=0x404; // Model native MSI's BusLead + legacy InterruptDisable.
        auto *s=new IOInterruptEventSource;s->owner=o;s->action=a;++irqfake::live;return s;
    }
    ~IOInterruptEventSource(){--irqfake::live;}
    void fire(){if(enabled)action(owner,this,1);}
    void stale(){action(owner,this,1);}
};
struct IOTimerEventSource:IOEventSource {
    using Action=void (*)(OSObject *,IOTimerEventSource *);Action action{};bool armed{};unsigned delay{};
    static IOTimerEventSource *timerEventSource(OSObject *o,Action a){
        if(++irqfake::allocation==irqfake::failAllocation)return nullptr;
        auto *s=new IOTimerEventSource;s->owner=o;s->action=a;++irqfake::live;return s;
    }
    ~IOTimerEventSource(){--irqfake::live;}
    void cancelTimeout(){armed=false;}
    IOReturn setTimeoutMS(unsigned ms){if(irqfake::armError)return irqfake::armError;armed=true;delay=ms;return 0;}
    void fire(){if(armed&&enabled){armed=false;action(owner,this);}}
    void stale(){action(owner,this);}
};
struct IOWorkLoop:OSObject {
    using Action=IOReturn (*)(OSObject *,void *,void *,void *,void *);
    bool gate=true;unsigned adds{},failAdd{};std::vector<IOEventSource *> sources;
    bool inGate(){return gate;}
    IOReturn addEventSource(IOEventSource *s){if(++adds==failAdd)return 5;s->loop=this;sources.push_back(s);return 0;}
    void removeEventSource(IOEventSource *s){for(auto it=sources.begin();it!=sources.end();++it)if(*it==s){sources.erase(it);s->loop=nullptr;return;}assert(false);}
    IOReturn runAction(Action a,OSObject *o,void *a0=nullptr,void *a1=nullptr,void *a2=nullptr,void *a3=nullptr){
        const bool previous=gate;gate=true;const auto r=a(o,a0,a1,a2,a3);gate=previous;return r;
    }
};
