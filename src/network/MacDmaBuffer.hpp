// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOMapper.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/pci/IOPCIDevice.h>
#include "DmaMapping.hpp"
namespace rtl8852be { namespace network {
enum class DmaStatus {ok,invalid,busy,allocateFailed,prepareFailed,commandFailed,attachFailed,segmentFailed,syncFailed,cleanupFailed};
// Single contiguous device segment, 32-bit RTL8852B descriptor addresses.
// Allocate/prepare outside the workloop gate (Apple says these may block).
// Caller serializes ownership; no interrupt-filter allocation or destruction.
class MacDmaBuffer {
    IOBufferMemoryDescriptor *memory_{};IODMACommand *command_{};IOMapper *mapper_{};
    bool memoryPrepared_{},commandPrepared_{},attached_{},visible_{},ready_{};
    DataMapping mapping_{};DmaStatus status_{DmaStatus::ok};
    bool cleanup();bool fail(DmaStatus status);
public:
    MacDmaBuffer()=default;
    ~MacDmaBuffer();
    MacDmaBuffer(const MacDmaBuffer &)=delete;MacDmaBuffer &operator=(const MacDmaBuffer &)=delete;
    bool allocate(IOPCIDevice *device,IOWorkLoop *workloop,size_t bytes,uint32_t alignment);
    DataMapping mapping()const{return ready_?mapping_:DataMapping{};}
    DmaStatus status()const{return status_;}
    bool syncForDevice();bool syncForCpu();
    // Call BEFORE publishing any address/doorbell that allows DMA access.
    bool markDeviceVisible();
    bool deviceVisible()const{return visible_;}
    bool release(); // refuses to free any possibly device-visible allocation
    // Controller must first mask/drain interrupts, stop queues, disable PCI
    // bus mastering and confirm idle. This function does not perform that work.
    bool releaseAfterDmaStopped();
};
} }
