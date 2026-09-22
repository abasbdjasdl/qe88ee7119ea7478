// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include "MacPciRuntimeIo.hpp"
// MSI is deferred directly to the workloop; no protocol, allocation, PCI config
// access or lock acquisition in a driver primary-interrupt filter.
class R16PciInterrupts : public OSObject {
    OSDeclareDefaultStructors(R16PciInterrupts)
public:
    using Runtime=rtl8852be::network::PciRuntime<rtl8852be::network::MacPciRuntimeIo>;
    using Causes=rtl8852be::network::InterruptStatus;
    enum class ServiceResult {drained,more,fault};
    // Drain RXQ/RPQ/TX completions with a bounded packet budget. 'more' keeps
    // IRQs masked and schedules a timer continuation. Never sleep in callback.
    using Service=ServiceResult (*)(void *,const Causes &);
    using Fault=void (*)(void *,const Causes &);
private:
    IOWorkLoop *loop_{};IOInterruptEventSource *irq_{};IOTimerEventSource *timer_{};
    Runtime *runtime_{};Service service_{};Fault fault_{};void *context_{};
    bool enabled_{},servicing_{},irqAdded_{},timerAdded_{};
    static void interrupt(OSObject *,IOInterruptEventSource *,int);
    static void timer(OSObject *,IOTimerEventSource *);
    void dispatch();
    void fail(const Causes &);
public:
    bool attach(IOPCIDevice *,IOWorkLoop *,int interruptIndex);
    // All following calls require the supplied workloop gate. start after the
    // runtime DMA start; stop before freeing rings. detach after stop success
    // or failure, but preserve mappings if the runtime cannot prove idle.
    bool start(Runtime *,Service,Fault,void *);
    // Reentrant stop disables DMA immediately but returns false until the
    // current service/fault callback has returned; its borrowed buffers live.
    bool stop();
    bool servicing()const{return servicing_;}
    bool detach();
    void free() override;
};
