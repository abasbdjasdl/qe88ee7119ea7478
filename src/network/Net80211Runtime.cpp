// SPDX-License-Identifier: GPL-2.0-or-later
// Host symbols expected by itlwm's protocol-only sources.
#include "Net80211Runtime.hpp"
#include <sys/CTimeout.hpp>
#include <libkern/OSAtomic.h>
OSDefineMetaClassAndStructors(CTimeout, OSObject)
IOWorkLoop *_fWorkloop=nullptr;
IOCommandGate *_fCommandGate=nullptr;
static void *volatile runtimeOwner=nullptr;
namespace rtl8852be { namespace network {
IOReturn Net80211Runtime::bind(IOWorkLoop *workloop,IOCommandGate *gate){
    if(!workloop||!gate||gate->getWorkLoop()!=workloop)return kIOReturnBadArgument;
    if(workloop_||!OSCompareAndSwapPtr(nullptr,this,&runtimeOwner))return kIOReturnExclusiveAccess;
    workloop->retain();gate->retain();workloop_=workloop;gate_=gate;
    _fWorkloop=workloop;_fCommandGate=gate;return kIOReturnSuccess;
}
IOReturn Net80211Runtime::unbindAfterProtocolDetached(){
    if(!workloop_)return kIOReturnNotReady;
    if(!workloop_->inGate())return kIOReturnNotPermitted;
    _fCommandGate=nullptr;_fWorkloop=nullptr;
    auto *gate=gate_;auto *workloop=workloop_;gate_=nullptr;workloop_=nullptr;
    gate->release();workloop->release();
    OSCompareAndSwapPtr(this,nullptr,&runtimeOwner);return kIOReturnSuccess;
}
} }
