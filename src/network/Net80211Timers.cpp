// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from itlwm/itl80211/openbsd/net80211/CTimeout.cpp at
// 53c51c2cdd6e4b69beb91f310d74c53422b0f8bd.
// Copyright (C) 2020 钟先耀
// Preserve its API, handling allocation/event-source failures explicitly.
#include <sys/CTimeout.hpp>
#include <IOKit/IOWorkLoop.h>
void CTimeout::timeoutOccurred(OSObject *owner,IOTimerEventSource *timer){
    auto *timeout=OSDynamicCast(CTimeout,owner);
    if(!timeout||timeout->tm!=timer||!timeout->isPending||!timeout->to_func)return;
    timeout->isPending=false;timeout->to_func(timeout->to_arg);
}
IOReturn CTimeout::timeout_set(OSObject *,void *arg0,void *arg1,void *arg2,void *){
    auto **slot=static_cast<CTimeout **>(arg0);
    if(!slot||!arg1)return kIOReturnBadArgument;
    if(!*slot){
        auto *created=new CTimeout;
        if(!created)return kIOReturnNoMemory;
        created->tm=nullptr;created->isPending=false;created->to_func=nullptr;created->to_arg=nullptr;
        *slot=created;
    }
    auto *timeout=*slot;
    if(timeout->tm)timeout->tm->cancelTimeout();
    timeout->isPending=false;timeout->to_func=reinterpret_cast<void (*)(void *)>(arg1);timeout->to_arg=arg2;
    return kIOReturnSuccess;
}
IOReturn CTimeout::timeout_add_msec(OSObject *,void *arg0,void *arg1,void *arg2,void *){
    auto **slot=static_cast<CTimeout **>(arg0);auto *workloop=static_cast<IOWorkLoop *>(arg1);
    if(!slot||!*slot||!workloop||!arg2||!(*slot)->to_func)return kIOReturnBadArgument;
    const int milliseconds=*static_cast<int *>(arg2);
    if(milliseconds<0)return kIOReturnBadArgument;
    auto *timeout=*slot;
    if(!timeout->tm){
        auto *timer=IOTimerEventSource::timerEventSource(timeout,&CTimeout::timeoutOccurred);
        if(!timer)return kIOReturnNoMemory;
        const auto result=workloop->addEventSource(timer);
        if(result!=kIOReturnSuccess){timer->release();return result;}
        timeout->tm=timer;timer->enable();
    }else if(timeout->tm->getWorkLoop()!=workloop)return kIOReturnBadArgument;
    timeout->tm->cancelTimeout();timeout->isPending=false;
    const auto result=timeout->tm->setTimeoutMS(static_cast<UInt32>(milliseconds));
    if(result==kIOReturnSuccess)timeout->isPending=true;
    return result;
}
IOReturn CTimeout::timeout_del(OSObject *,void *arg0,void *,void *,void *){
    auto **slot=static_cast<CTimeout **>(arg0);
    if(!slot||!*slot)return kIOReturnSuccess;
    auto *timeout=*slot;if(timeout->tm)timeout->tm->cancelTimeout();timeout->isPending=false;
    return kIOReturnSuccess;
}
IOReturn CTimeout::timeout_free(OSObject *,void *arg0,void *arg1,void *,void *){
    auto **slot=static_cast<CTimeout **>(arg0);auto *workloop=static_cast<IOWorkLoop *>(arg1);
    if(!slot||!*slot)return kIOReturnSuccess;
    auto *timeout=*slot;
    if(timeout->tm){
        if(!workloop||timeout->tm->getWorkLoop()!=workloop)return kIOReturnBadArgument;
        timeout->tm->cancelTimeout();timeout->isPending=false;
        workloop->removeEventSource(timeout->tm);timeout->tm->release();timeout->tm=nullptr;
    }
    timeout->to_func=nullptr;timeout->to_arg=nullptr;*slot=nullptr;timeout->release();return kIOReturnSuccess;
}
IOReturn CTimeout::timeout_pending(OSObject *,void *arg0,void *,void *,void *){
    auto **slot=static_cast<CTimeout **>(arg0);
    return slot&&*slot&&(*slot)->isPending?kIOReturnSuccess:kIOReturnNotReady;
}
