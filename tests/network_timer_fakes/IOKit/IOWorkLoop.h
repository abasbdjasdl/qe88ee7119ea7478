// SPDX-License-Identifier: BSD-3-Clause
// Test-only IOKit failure injection. Never included by the kernel build.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstddef>
using IOReturn=int;using UInt32=uint32_t;
constexpr IOReturn kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNoMemory=2,kIOReturnNotReady=3;
struct OSObject {virtual ~OSObject()=default;};
struct IOWorkLoop;
struct IOTimerEventSource {
    using Action=void (*)(OSObject *,IOTimerEventSource *);
    static bool failAllocation;static int armResult,live;
    OSObject *owner;Action action;IOWorkLoop *loop{};bool armed{},enabled{};
    IOTimerEventSource(OSObject *o,Action a):owner(o),action(a){++live;}
    static IOTimerEventSource *timerEventSource(OSObject *o,Action a){return failAllocation?nullptr:new IOTimerEventSource(o,a);}
    void enable(){enabled=true;}
    void cancelTimeout(){armed=false;}
    IOReturn setTimeoutMS(UInt32){if(!armResult)armed=true;return armResult;}
    IOWorkLoop *getWorkLoop(){return loop;}
    void release(){--live;delete this;}
};
struct IOWorkLoop {
    int addResult{},sources{};
    IOReturn addEventSource(IOTimerEventSource *timer){if(addResult)return addResult;timer->loop=this;++sources;return 0;}
    void removeEventSource(IOTimerEventSource *timer){timer->loop=nullptr;--sources;}
};
#define OSDynamicCast(type,object) dynamic_cast<type *>(object)
