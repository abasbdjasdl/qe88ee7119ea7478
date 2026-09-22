// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "MacTxDmaQueue.hpp"
#include "RxDmaQueue.hpp"
#include "MacPciInterrupts.hpp"
#include "PciQueueService.hpp"
namespace rtl8852be { namespace network {
using NativeQueueService=PciQueueService<R16PciInterrupts::Runtime,MacTxDmaQueue,MacFirmwareDmaQueue,RxDmaQueue<MacDmaBuffer>>;
// Suitable as R16PciInterrupts::Service; context points at NativeQueueService.
R16PciInterrupts::ServiceResult serviceNativeQueues(void *,const InterruptStatus &);
// On publish error the staged lease remains DMA-owned until confirmed stop.
int submitNativeData(R16PciInterrupts::Runtime &,MacTxDmaQueue &,unsigned ring,TxLease &,TxInfo);
int submitNativeFirmware(R16PciInterrupts::Runtime &,MacFirmwareDmaQueue &,const uint8_t *,size_t);
extern template class PciQueueService<R16PciInterrupts::Runtime,MacTxDmaQueue,MacFirmwareDmaQueue,RxDmaQueue<MacDmaBuffer>>;
} }
