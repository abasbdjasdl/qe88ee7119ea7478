// SPDX-License-Identifier: GPL-2.0-or-later
// Compile-only callsite probe for the pinned Darwin 24.4 Skywalk packet API.
// These deliberately incomplete classes MUST NOT be linked into a kext.
#ifndef R16_NATIVE_ABI_AUDIT_ONLY
#error "Native packet Port declarations are for offline ABI audit only"
#endif
#ifndef __x86_64__
#error "The packet API evidence is x86_64 only"
#endif
#include "native_cpu_packet_bridge.hpp"

class IOSkywalkPacketQueue;
class IOSkywalkPacketBufferPool;
class IOSkywalkRxCompletionQueue;
class IOSkywalkTxCompletionQueue;

// Methods are declared nonvirtual here to force ordinary compiler-mangled
// direct references. This DOES NOT certify a loaded object's virtual layout.
// No Apple object fields, size, constructor, retain or type hierarchy appear.
class IOSkywalkPacket {
public:
    unsigned getBufferSize() const;
    unsigned getPacketBufferCount() const;
    unsigned getDataLength() const;
    unsigned short getDataOffset() const;
    unsigned char *getDataVirtualAddress() const;
    IOSkywalkPacketQueue *getSourceQueue() const;
    unsigned getTransferDirection() const;
    void setTransferDirection(unsigned);
    IOSkywalkPacketBufferPool *getPacketBufferPool() const;
    r16_native_audit::Status prepareWithQueue(IOSkywalkPacketQueue *,unsigned,unsigned);
    r16_native_audit::Status setDataLength(unsigned);
};
class IOSkywalkPacketBufferPool {
public:
    r16_native_audit::Status allocatePacket(unsigned,IOSkywalkPacket **,unsigned);
    // The KC's null-input path does not initialize EAX. Never consume a return.
    void deallocatePacket(IOSkywalkPacket *);
};
class IOSkywalkRxCompletionQueue {
public:
    r16_native_audit::Status enqueuePackets(IOSkywalkPacket * const *,unsigned,unsigned);
};
class IOSkywalkTxCompletionQueue {
public:
    r16_native_audit::Status enqueuePackets(IOSkywalkPacket * const *,unsigned,unsigned);
};

namespace r16_native_audit {
static_assert(Same<decltype(((IOSkywalkPacketBufferPool*)0)->allocatePacket(1,(IOSkywalkPacket**)0,0)),Status>::value,
              "pool allocation must use full IOReturn");
static_assert(Same<decltype(((IOSkywalkPacket*)0)->prepareWithQueue((IOSkywalkPacketQueue*)0,2,0)),Status>::value,
              "RX preparation must use full IOReturn");
static_assert(Same<decltype(((IOSkywalkPacket*)0)->setDataLength(0)),Status>::value,
              "packet length must use full IOReturn");
static_assert(Same<decltype(((IOSkywalkRxCompletionQueue*)0)->enqueuePackets((IOSkywalkPacket* const*)0,1,0)),Status>::value,
              "RX enqueue must use full IOReturn");
static_assert(Same<decltype(((IOSkywalkTxCompletionQueue*)0)->enqueuePackets((IOSkywalkPacket* const*)0,1,0)),Status>::value,
              "TX completion enqueue must use full IOReturn");

// Our storage only. An owner would need to prove every pointer came from a
// matching, initialized and still-live target-KC factory before setting ready.
// In this probe ready and metadataProven start false and are never promoted.
struct NativePacketPortProbe {
    using Packet = IOSkywalkPacket;
    IOSkywalkPacketBufferPool *rxPool{};
    IOSkywalkPacketBufferPool *txPool{};
    IOSkywalkPacketQueue *rxSource{};
    IOSkywalkPacketQueue *txSource{};
    IOSkywalkRxCompletionQueue *rxCompletion{};
    IOSkywalkTxCompletionQueue *txCompletion{};
    bool ready{};
    bool metadataProven{};

    PacketView view(Packet *packet) {
        PacketView result{};
        if (!ready || !metadataProven || !packet) return result;
        const auto *source=packet->getSourceQueue();
        const unsigned direction=packet->getTransferDirection();
        const bool tx=source==txSource && source && direction==1 &&
                      packet->getPacketBufferPool()==txPool;
        const bool rx=source==rxSource && source && direction==2 &&
                      packet->getPacketBufferPool()==rxPool;
        if ((!tx && !rx) || packet->getPacketBufferCount()!=1) return result;
        const unsigned size=packet->getBufferSize();
        const unsigned offset=packet->getDataOffset();
        const unsigned length=packet->getDataLength();
        if (offset>size || length>size-offset) return result;
        result.base=packet->getDataVirtualAddress();
        result.bufferSize=size;
        result.dataOffset=offset;
        result.dataLength=length;
        result.bufferCount=1;
        result.prepared=true;
        result.supportedMetadata=true;
        return result;
    }
    Status allocateRx(Packet **out) {
        if (!out) return -1;
        *out=nullptr;
        if (!ready || !rxPool) return -1;
        return rxPool->allocatePacket(1,out,0);
    }
    Status prepareRx(Packet *packet) {
        if (!ready || !packet || !rxSource) return -1;
        const Status status=packet->prepareWithQueue(rxSource,2,0);
        if (status==success) packet->setTransferDirection(2);
        return status;
    }
    Status setRxLength(Packet *packet,unsigned length) {
        return ready && packet ? packet->setDataLength(length):-1;
    }
    Status enqueueRx(Packet * const *packets,unsigned count) {
        return ready && rxCompletion && packets && count==1 ?
            rxCompletion->enqueuePackets(packets,count,0):-1;
    }
    Status completeTx(Packet * const *packets,unsigned count) {
        return ready && txCompletion && packets && count==1 ?
            txCompletion->enqueuePackets(packets,count,0):-1;
    }
    // These methods are only symbol callsites. A real adapter must retain a
    // quarantine ledger on unexpected pool provenance or teardown races.
    void reclaimRx(Packet *packet) {
        if (ready && rxPool && packet && packet->getPacketBufferPool()==rxPool)
            rxPool->deallocatePacket(packet);
    }
    void reclaimPreparedTx(Packet *packet) {
        if (ready && txPool && packet && packet->getPacketBufferPool()==txPool)
            txPool->deallocatePacket(packet);
    }
};

using NativePacketBridgeProbe=CpuPacketBridge<NativePacketPortProbe>;
// Force all Port methods and the actual API callsites into O0 and O2 objects.
extern "C" void r16_native_port_view(NativePacketPortProbe *p,IOSkywalkPacket *packet,PacketView *out) {
    if (out) *out=p->view(packet);
}
extern "C" Status r16_native_port_allocate(NativePacketPortProbe *p,IOSkywalkPacket **out) {
    return p->allocateRx(out);
}
extern "C" Status r16_native_port_prepare(NativePacketPortProbe *p,IOSkywalkPacket *packet) {
    return p->prepareRx(packet);
}
extern "C" Status r16_native_port_length(NativePacketPortProbe *p,IOSkywalkPacket *packet,unsigned length) {
    return p->setRxLength(packet,length);
}
extern "C" Status r16_native_port_rx(NativePacketPortProbe *p,IOSkywalkPacket * const *packets,unsigned count) {
    return p->enqueueRx(packets,count);
}
extern "C" Status r16_native_port_tx(NativePacketPortProbe *p,IOSkywalkPacket * const *packets,unsigned count) {
    return p->completeTx(packets,count);
}
extern "C" void r16_native_port_reclaim_rx(NativePacketPortProbe *p,IOSkywalkPacket *packet) {
    p->reclaimRx(packet);
}
extern "C" void r16_native_port_reclaim_tx(NativePacketPortProbe *p,IOSkywalkPacket *packet) {
    p->reclaimPreparedTx(packet);
}
} // namespace r16_native_audit
