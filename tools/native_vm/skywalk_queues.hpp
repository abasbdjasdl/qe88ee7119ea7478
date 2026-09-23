// VM factory experiment: real queues remain disabled and unregistered.
#ifndef R16_VM_SKYWALK_QUEUES_HPP
#define R16_VM_SKYWALK_QUEUES_HPP
#include "skywalk_pool.hpp"
class IOSkywalkPacket;
class IOSkywalkTxSubmissionQueue;
class IOSkywalkTxCompletionQueue;
class IOSkywalkRxCompletionQueue;
using R16VMTxAction=unsigned(*)(OSObject*,IOSkywalkTxSubmissionQueue*,IOSkywalkPacket *const*,unsigned,void*);
using R16VMTxCompletionAction=unsigned(*)(OSObject*,IOSkywalkTxCompletionQueue*,IOSkywalkPacket**,unsigned,void*);
using R16VMRxAction=unsigned(*)(OSObject*,IOSkywalkRxCompletionQueue*,IOSkywalkPacket**,unsigned,void*);
class IOSkywalkTxSubmissionQueue {
public:
    static IOSkywalkTxSubmissionQueue *withPool(IOSkywalkPacketBufferPool*,unsigned,unsigned,OSObject*,R16VMTxAction,void*,unsigned);
};
class IOSkywalkTxCompletionQueue {
public:
    static IOSkywalkTxCompletionQueue *withPool(IOSkywalkPacketBufferPool*,unsigned,unsigned,OSObject*,R16VMTxCompletionAction,void*,unsigned);
};
class IOSkywalkRxCompletionQueue {
public:
    static IOSkywalkRxCompletionQueue *withPool(IOSkywalkPacketBufferPool*,unsigned,unsigned,OSObject*,R16VMRxAction,void*,unsigned);
};
#endif
