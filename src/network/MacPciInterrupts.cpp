// SPDX-License-Identifier: BSD-3-Clause
#include "MacPciInterrupts.hpp"
#include <IOKit/IOLib.h>
OSDefineMetaClassAndStructors(R16PciInterrupts,OSObject)
bool R16PciInterrupts::attach(IOPCIDevice *device,IOWorkLoop *loop,int index){
    if(loop_||!device||!loop||index<0)return false;
    const auto command=device->configRead16(kIOPCIConfigCommand);
    if(command==0xffff||(command&6)!=2)return false;
    int type=0;if(device->getInterruptType(index,&type)!=kIOReturnSuccess||!(type&kIOInterruptTypePCIMessaged))return false;
    loop_=loop;loop_->retain();
    irq_=IOInterruptEventSource::interruptEventSource(this,interrupt,device,index);
    timer_=IOTimerEventSource::timerEventSource(this,timer);
    if(!irq_||!timer_)goto failed;
    irq_->disable();
    if(loop_->addEventSource(irq_)!=kIOReturnSuccess)goto failed;irqAdded_=true;
    if(loop_->addEventSource(timer_)!=kIOReturnSuccess)goto failed;timerAdded_=true;
    // IOPCIFamily MSI registration may set BusLead along with InterruptDisable.
    // This is pre-runtime attachment: no descriptors are published yet. Restore
    // BM-off before power preparation; runtime.start owns the later BM enable.
    device->setBusMasterEnable(false);
    if((device->configRead16(kIOPCIConfigCommand)&6)!=2)goto failed;
    return true;
failed:
    if(timer_){if(timerAdded_)loop_->removeEventSource(timer_);timer_->release();timer_=nullptr;}
    if(irq_){if(irqAdded_)loop_->removeEventSource(irq_);irq_->release();irq_=nullptr;}
    device->setBusMasterEnable(false);
    irqAdded_=false;loop_->release();loop_=nullptr;return false;
}
bool R16PciInterrupts::start(Runtime *runtime,Service service,Fault fault,void *context){
    if(!loop_||!loop_->inGate()||!irqAdded_||!timerAdded_||runtime_||servicing_||!runtime||!runtime->running()||!service||!fault)return false;
    runtime_=runtime;service_=service;fault_=fault;context_=context;enabled_=true;
    irq_->enable();
    if(!runtime_->enableInterrupts()){stop();return false;}
    // Also drain anything accumulated during DMA start before MSI was armed.
    if(timer_->setTimeoutMS(1)!=kIOReturnSuccess){stop();return false;}
    return true;
}
void R16PciInterrupts::interrupt(OSObject *owner,IOInterruptEventSource *,int){static_cast<R16PciInterrupts *>(owner)->dispatch();}
void R16PciInterrupts::timer(OSObject *owner,IOTimerEventSource *){static_cast<R16PciInterrupts *>(owner)->dispatch();}
void R16PciInterrupts::fail(const Causes &causes){
    enabled_=false;irq_->disable();timer_->cancelTimeout();
    runtime_->stop(); // Failure keeps DMA ownership with the controller.
    fault_(context_,causes);
}
void R16PciInterrupts::dispatch(){
    if(!enabled_||!runtime_||servicing_)return;
    servicing_=true;retain();
    struct Guard {R16PciInterrupts *self;~Guard(){self->servicing_=false;self->release();}} guard{this};
    Causes causes;
    if(!runtime_->maskInterrupts()){fail(causes);return;}
    causes=runtime_->acknowledgeInterrupts();
    if(!causes.valid||causes.fatal()){fail(causes);return;}
    const auto outcome=service_(context_,causes);
    if(!enabled_)return; // Callback requested stop, do not rearm it.
    if(outcome==ServiceResult::fault){fail(causes);return;}
    if(outcome==ServiceResult::more){
        if(timer_->setTimeoutMS(1)!=kIOReturnSuccess)fail(causes);
        return;
    }
    if(!runtime_->enableInterrupts()){fail(causes);return;}
    // 10 ms backstop: recover coalesced/lost edges and firmware completions
    // that have no dedicated enabled TX interrupt. Sticky causes are never
    // blindly cleared after draining; next dispatch acknowledges them.
    if(timer_->setTimeoutMS(10)!=kIOReturnSuccess)fail(causes);
}
bool R16PciInterrupts::stop(){
    if(!loop_||!loop_->inGate())return false;
    enabled_=false;if(irq_)irq_->disable();if(timer_)timer_->cancelTimeout();
    const bool stopped=!runtime_||runtime_->stop();return stopped&&!servicing_;
}
bool R16PciInterrupts::detach(){
    if(!loop_)return true;
    if(!loop_->inGate()||enabled_||servicing_)return false;
    if(timer_){timer_->cancelTimeout();if(timerAdded_)loop_->removeEventSource(timer_);timer_->release();timer_=nullptr;}
    if(irq_){irq_->disable();if(irqAdded_)loop_->removeEventSource(irq_);irq_->release();irq_=nullptr;}
    timerAdded_=irqAdded_=false;runtime_=nullptr;service_=nullptr;fault_=nullptr;context_=nullptr;
    auto *loop=loop_;loop_=nullptr;loop->release();return true;
}
void R16PciInterrupts::free(){
    // Explicit gated detach is preferred. Final-release fallback is serialized
    // against queued callbacks, and must precede destruction of borrowed runtime.
    if(loop_){auto *loop=loop_;loop->retain();
        loop->runAction([](OSObject *o,void *,void *,void *,void *)->IOReturn{
            auto *self=static_cast<R16PciInterrupts *>(o);self->stop();self->detach();return kIOReturnSuccess;
        },this);
        loop->release();
    }
    OSObject::free();
}
