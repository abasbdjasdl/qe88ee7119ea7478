// SPDX-License-Identifier: GPL-2.0-or-later
#include "MacQueueService.hpp"
#include <sys/errno.h>
namespace rtl8852be { namespace network {
R16PciInterrupts::ServiceResult serviceNativeQueues(void *context,const InterruptStatus &){
    if(!context)return R16PciInterrupts::ServiceResult::fault;
    switch(static_cast<NativeQueueService *>(context)->drain()){
    case QueueServiceResult::drained:return R16PciInterrupts::ServiceResult::drained;
    case QueueServiceResult::more:return R16PciInterrupts::ServiceResult::more;
    default:return R16PciInterrupts::ServiceResult::fault;
    }
}
int submitNativeData(R16PciInterrupts::Runtime &runtime,MacTxDmaQueue &queue,unsigned ring,TxLease &lease,TxInfo info){
    if(ring>=6)return EINVAL;if(!runtime.running())return ENETDOWN;
    if(!runtime.ownsRing(ring,queue.ringMapping().physical))return EINVAL;
    uint16_t next=0;const auto error=queue.stage(lease,info,next);if(error)return error;
    return runtime.publish(ring,next)?0:EIO;
}
int submitNativeFirmware(R16PciInterrupts::Runtime &runtime,MacFirmwareDmaQueue &queue,const uint8_t *bytes,size_t length){
    if(!runtime.running())return ENETDOWN;
    if(!runtime.ownsRing(6,queue.ringMapping().physical))return EINVAL;
    uint16_t next=0;const auto error=queue.stage(bytes,length,next);if(error)return error;
    return runtime.publish(6,next)?0:EIO;
}
template class PciQueueService<R16PciInterrupts::Runtime,MacTxDmaQueue,MacFirmwareDmaQueue,RxDmaQueue<MacDmaBuffer>>;
} }
