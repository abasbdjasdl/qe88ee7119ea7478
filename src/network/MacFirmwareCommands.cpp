// SPDX-License-Identifier: GPL-2.0-or-later
#include "MacFirmwareCommands.hpp"
#include <kern/clock.h>
#include <sys/errno.h>
namespace rtl8852be { namespace network {
uint64_t MacCommandTransport::nowUs(){uint64_t t=0,n=0;clock_get_uptime(&t);absolutetime_to_nanoseconds(t,&n);return n/1000;}
bool MacCommandTransport::publish(const uint8_t *bytes,size_t length){
    return inGate()&&submitNativeFirmware(runtime_,queue_,bytes,length)==0;
}
int MacFirmwareEventBinding::receive(void *context,const FirmwareEvent &event){
    if(!context)return EINVAL;auto &b=*static_cast<MacFirmwareEventBinding *>(context);
    if(b.receiveEpoch_!=b.commands_.epoch())return ESTALE;
    switch(b.commands_.accept(event,b.receiveEpoch_)){
    case CommandEvent::fault:return EIO;
    case CommandEvent::received:case CommandEvent::completed:return 0;
    default:break;
    }
    FirmwareAck ack{};if(decodeAck(event,ack))return 0; // stale/unmatched ACK is not a notification
    return b.notification_?b.notification_(b.owner_,event):EOPNOTSUPP;
}
template class FirmwareCommands<MacCommandTransport>;
} }
