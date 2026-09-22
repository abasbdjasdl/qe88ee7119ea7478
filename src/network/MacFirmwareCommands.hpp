// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "MacQueueService.hpp"
#include "FirmwareCommands.hpp"
namespace rtl8852be { namespace network {
// Borrowed native runtime/CH12/workloop. Their lifetime and RX incarnation must
// cover this object, every in-flight command, and every completion callback.
class MacCommandTransport {
    IOWorkLoop &loop_;R16PciInterrupts::Runtime &runtime_;MacFirmwareDmaQueue &queue_;
public:
    MacCommandTransport(IOWorkLoop &loop,R16PciInterrupts::Runtime &runtime,MacFirmwareDmaQueue &queue):
        loop_(loop),runtime_(runtime),queue_(queue){}
    bool inGate()const{return loop_.inGate();}
    uint64_t nowUs();
    bool publish(const uint8_t *,size_t);
};
using NativeFirmwareCommands=FirmwareCommands<MacCommandTransport>;
extern template class FirmwareCommands<MacCommandTransport>;
// Stable callback context captured when the RX queue is initialized. Construct
// a new binding for a new physical firmware/RX incarnation; never overwrite the
// epoch on receipt. Chain non-ACK notifications to the controller's dispatcher.
class MacFirmwareEventBinding {
    NativeFirmwareCommands &commands_;const uint64_t receiveEpoch_;
    void *owner_{};int (*notification_)(void *,const FirmwareEvent &){};
public:
    MacFirmwareEventBinding(NativeFirmwareCommands &commands,uint64_t receiveEpoch,void *owner,
                            int (*notification)(void *,const FirmwareEvent &)):
        commands_(commands),receiveEpoch_(receiveEpoch),owner_(owner),notification_(notification){}
    MacFirmwareEventBinding(const MacFirmwareEventBinding&)=delete;
    MacFirmwareEventBinding&operator=(const MacFirmwareEventBinding&)=delete;
    static int receive(void *,const FirmwareEvent &);
};
} }
