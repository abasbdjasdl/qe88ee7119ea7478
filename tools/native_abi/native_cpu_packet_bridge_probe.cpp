// SPDX-License-Identifier: GPL-2.0-or-later
// COMPILE ONLY. Adapter functions below have NO implementation in this probe.
// No pool/queue method declaration, private field access, kext or runtime entry.
#include "native_cpu_packet_bridge.hpp"
#ifndef __x86_64__
#error "Pinned KC evidence is x86_64 only"
#endif
namespace r16_native_audit {
struct OpaquePort {
    using Packet = IOSkywalkPacket;
    PacketView view(Packet *);
    Status allocateRx(Packet **);
    Status prepareRx(Packet *);
    Status setRxLength(Packet *,unsigned);
    Status enqueueRx(Packet * const *,unsigned);
    Status completeTx(Packet * const *,unsigned);
    void reclaimRx(Packet *);
    void reclaimPreparedTx(Packet *);
};
using ProbeBridge = CpuPacketBridge<OpaquePort>;
struct ProbeContext {
    ProbeBridge *bridge;
    OpaquePort *port;
    OSObject *owner;
    IOSkywalkTxSubmissionQueue *queue;
};
// This wrapper has precisely the array-mode KC factory callback signature.
// Context is OUR durable storage; it is not an Apple instance or private tail.
extern "C" unsigned r16_cpu_bridge_tx_action(OSObject *owner,
    IOSkywalkTxSubmissionQueue *queue, IOSkywalkPacket * const *packets,
    unsigned offered, void *context) {
    auto *c=static_cast<ProbeContext*>(context);
    if (!c || !c->bridge || !c->port || owner!=c->owner || queue!=c->queue) return 0;
    return c->bridge->submitTx(*c->port,packets,offered);
}
static_assert(Same<decltype(&r16_cpu_bridge_tx_action),TxAction>::value,
              "TX action must return unsigned count and packet* const*");
extern "C" RxResult r16_cpu_bridge_receive(ProbeContext *c,
                                           const uint8_t *bytes,size_t length) {
    if (!c || !c->bridge || !c->port) return RxResult::stopped;
    return c->bridge->receive(*c->port,bytes,length);
}
extern "C" bool r16_cpu_bridge_take(ProbeContext *c,ProbeBridge::Ticket *ticket) {
    return c && c->bridge && ticket && c->bridge->takeTx(*ticket);
}
extern "C" bool r16_cpu_bridge_finish(ProbeContext *c,
                                      const ProbeBridge::Ticket *ticket,bool success) {
    return c && c->bridge && ticket && c->bridge->finishTx(*ticket,success);
}
extern "C" unsigned r16_cpu_bridge_return(ProbeContext *c,bool poolReclaim) {
    return c && c->bridge && c->port ?
           c->bridge->returnCompletedTx(*c->port,poolReclaim):0;
}
extern "C" bool r16_cpu_bridge_stop(ProbeContext *c,bool disabled,bool drained) {
    if (!c || !c->bridge) return false;
    c->bridge->beginStop();
    return c->bridge->finishStop(disabled,drained);
}
} // namespace r16_native_audit
