// VM-only static factory declaration. No instance layout or guessed vtable.
#ifndef R16_VM_SKYWALK_POOL_HPP
#define R16_VM_SKYWALK_POOL_HPP
#ifndef R16_VM_LIFECYCLE_EXPERIMENT
#error "Disposable VM only"
#endif
#ifdef IOSkywalkPacketBufferPool_h
#error "Old pool declaration already included"
#endif
#define IOSkywalkPacketBufferPool_h 1
class OSObject;
class IOSkywalkPacketBufferPool {
public:
    struct PoolOptions {
        unsigned packetCount,bufferCount,bufferSize,maxBuffersPerPacket;
        unsigned memorySegmentSize,poolFlags;
        const void *dmaSpecification;
    };
    static IOSkywalkPacketBufferPool *withName(const char *,OSObject *,unsigned,const PoolOptions *);
};
static_assert(sizeof(IOSkywalkPacketBufferPool::PoolOptions)==32,"Pool ABI");
static_assert(__builtin_offsetof(IOSkywalkPacketBufferPool::PoolOptions,dmaSpecification)==24,"DMA pointer ABI");
#endif
