// Offline callsite/mangling probe only. Never link into a driver or invoke.
// Same-KC evidence and exclusions: registration-evidence.md / symbols JSON.
#ifndef R16_NATIVE_ABI_AUDIT_ONLY
#error "Registration callsite declarations are for the offline ABI audit only"
#endif
#ifndef __x86_64__
#error "The registration evidence is pinned to the x86_64 target"
#endif

// Apple80211.h otherwise imports an old, unverified complete pool class.
// Suppress it in THIS translation unit. The minimal factory declaration below
// makes no claim about an instance's inheritance, size, fields or virtual slots.
#ifdef IOSkywalkPacketBufferPool_h
#error "Do not preload the old private packet-pool declaration in this probe"
#endif
#define IOSkywalkPacketBufferPool_h 1
#include <Airport/Apple80211.h>

class IOSkywalkPacket;

class IOSkywalkPacketBufferPool {
public:
    struct PoolOptions {
        unsigned packetCount;
        unsigned bufferCount;
        unsigned bufferSize;
        unsigned maxBuffersPerPacket;
        unsigned memorySegmentSize;
        unsigned poolFlags;
        // KC consumes this as an optional DMA specification pointer. The
        // pointed-to layout is deliberately absent; this probe never reads it.
        const void *dmaSpecification;
    };
    static IOSkywalkPacketBufferPool *withName(
        const char *, OSObject *, unsigned, const PoolOptions *);
};

class IOSkywalkTxSubmissionQueue;
class IOSkywalkRxCompletionQueue;

// PKP15IOSkywalkPacket = packet * const *, not const packet **.
using RegistrationTxAction = unsigned (*)(
    OSObject *, IOSkywalkTxSubmissionQueue *, IOSkywalkPacket * const *,
    unsigned, void *);
using RegistrationRxAction = unsigned (*)(
    OSObject *, IOSkywalkRxCompletionQueue *, IOSkywalkPacket **,
    unsigned, void *);

class IOSkywalkTxSubmissionQueue {
public:
    static IOSkywalkTxSubmissionQueue *withPool(
        IOSkywalkPacketBufferPool *, unsigned, unsigned, OSObject *,
        RegistrationTxAction, void *, unsigned);
};

class IOSkywalkRxCompletionQueue {
public:
    static IOSkywalkRxCompletionQueue *withPool(
        IOSkywalkPacketBufferPool *, unsigned, unsigned, OSObject *,
        RegistrationRxAction, void *, unsigned);
};

using RegistrationInfo = IOSkywalkEthernetInterface::RegistrationInfo;
template<class A, class B> struct RegistrationSameType { enum { value = false }; };
template<class A> struct RegistrationSameType<A, A> { enum { value = true }; };

static_assert(sizeof(unsigned) == 4 && sizeof(unsigned long) == 8 &&
              sizeof(void *) == 8 && sizeof(IOReturn) == 4,
              "Pinned scalar/pointer ABI changed");
static_assert(sizeof(IOSkywalkPacketBufferPool::PoolOptions) == 32,
              "PoolOptions must contain six words and one pointer");
static_assert(__builtin_offsetof(IOSkywalkPacketBufferPool::PoolOptions,
                                dmaSpecification) == 0x18,
              "Optional DMA specification offset changed");

using WorkQueueFactory = IO80211WorkQueue *(*)();
using PoolFactory = IOSkywalkPacketBufferPool *(*)(
    const char *, OSObject *, unsigned,
    const IOSkywalkPacketBufferPool::PoolOptions *);
using TxFactory = IOSkywalkTxSubmissionQueue *(*)(
    IOSkywalkPacketBufferPool *, unsigned, unsigned, OSObject *,
    RegistrationTxAction, void *, unsigned);
using RxFactory = IOSkywalkRxCompletionQueue *(*)(
    IOSkywalkPacketBufferPool *, unsigned, unsigned, OSObject *,
    RegistrationRxAction, void *, unsigned);
using InitInfo = bool (IOSkywalkEthernetInterface::*)(
    RegistrationInfo *, unsigned, unsigned long);
using RegisterEthernet = IOReturn (IOSkywalkEthernetInterface::*)(
    const RegistrationInfo *, IOSkywalkPacketQueue **, unsigned,
    IOSkywalkPacketBufferPool *, IOSkywalkPacketBufferPool *, unsigned);
using RegisterInfra = IOReturn (IO80211InfraInterface::*)(
    RegistrationInfo *, IOSkywalkPacketQueue **, unsigned,
    IOSkywalkPacketBufferPool *, IOSkywalkPacketBufferPool *);
using DeregisterEthernet = IOReturn (IOSkywalkEthernetInterface::*)(unsigned);

static_assert(RegistrationSameType<decltype(&IO80211WorkQueue::workQueue),
                                  WorkQueueFactory>::value, "workQueue signature");
static_assert(RegistrationSameType<decltype(&IOSkywalkPacketBufferPool::withName),
                                  PoolFactory>::value, "pool factory signature");
static_assert(RegistrationSameType<decltype(&IOSkywalkTxSubmissionQueue::withPool),
                                  TxFactory>::value, "TX factory signature");
static_assert(RegistrationSameType<decltype(&IOSkywalkRxCompletionQueue::withPool),
                                  RxFactory>::value, "RX factory signature");
static_assert(RegistrationSameType<decltype(&IOSkywalkEthernetInterface::initRegistrationInfo),
                                  InitInfo>::value, "init info signature");
static_assert(RegistrationSameType<decltype(&IOSkywalkEthernetInterface::registerEthernetInterface),
                                  RegisterEthernet>::value, "const Ethernet info signature");
static_assert(RegistrationSameType<decltype(&IO80211InfraInterface::registerInfraEthernetInterface),
                                  RegisterInfra>::value, "mutable Infra info signature");
static_assert(RegistrationSameType<decltype(&IOSkywalkEthernetInterface::deregisterEthernetInterface),
                                  DeregisterEthernet>::value, "deregister signature");

// Each external wrapper emits one ordinary compiler-mangled undefined helper
// reference. No asm names, casts to function addresses, new or object-field
// access is used. Only compile these wrappers; invoking one would call the
// actual factory/registration operation and is outside the offline audit.
extern "C" IO80211WorkQueue *r16_registration_work_queue() {
    return IO80211WorkQueue::workQueue();
}

extern "C" IOSkywalkPacketBufferPool *r16_registration_pool(
    const char *name, OSObject *owner, unsigned packetType,
    const IOSkywalkPacketBufferPool::PoolOptions *options) {
    return IOSkywalkPacketBufferPool::withName(name, owner, packetType, options);
}

extern "C" IOSkywalkTxSubmissionQueue *r16_registration_tx_queue(
    IOSkywalkPacketBufferPool *pool, unsigned capacity, unsigned queueId,
    OSObject *owner, RegistrationTxAction action, void *context, unsigned options) {
    return IOSkywalkTxSubmissionQueue::withPool(
        pool, capacity, queueId, owner, action, context, options);
}

extern "C" IOSkywalkRxCompletionQueue *r16_registration_rx_queue(
    IOSkywalkPacketBufferPool *pool, unsigned capacity, unsigned queueId,
    OSObject *owner, RegistrationRxAction action, void *context, unsigned options) {
    return IOSkywalkRxCompletionQueue::withPool(
        pool, capacity, queueId, owner, action, context, options);
}

extern "C" bool r16_registration_init_info(
    IOSkywalkEthernetInterface *interface, RegistrationInfo *info,
    unsigned version, unsigned long size) {
    return interface->initRegistrationInfo(info, version, size);
}

extern "C" IOReturn r16_registration_ethernet(
    IOSkywalkEthernetInterface *interface, const RegistrationInfo *info,
    IOSkywalkPacketQueue **queues, unsigned count,
    IOSkywalkPacketBufferPool *txPool, IOSkywalkPacketBufferPool *rxPool,
    unsigned options) {
    return interface->registerEthernetInterface(
        info, queues, count, txPool, rxPool, options);
}

extern "C" IOReturn r16_registration_infra(
    IO80211InfraInterface *interface, RegistrationInfo *info,
    IOSkywalkPacketQueue **queues, unsigned count,
    IOSkywalkPacketBufferPool *txPool, IOSkywalkPacketBufferPool *rxPool) {
    return interface->registerInfraEthernetInterface(
        info, queues, count, txPool, rxPool);
}

extern "C" IOReturn r16_registration_deregister(
    IOSkywalkEthernetInterface *interface, unsigned options) {
    return interface->deregisterEthernetInterface(options);
}
