// SPDX-License-Identifier: BSD-3-Clause
#include "MacDmaBuffer.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSAtomic.h>
namespace rtl8852be { namespace network {
bool MacDmaBuffer::cleanup(){
    if(visible_)return false;
    ready_=false;mapping_={};
    if(commandPrepared_){if(command_->complete()!=kIOReturnSuccess)return false;commandPrepared_=false;}
    if(attached_){if(command_->clearMemoryDescriptor(false)!=kIOReturnSuccess)return false;attached_=false;}
    if(command_){command_->release();command_=nullptr;}
    if(memoryPrepared_){if(memory_->complete()!=kIOReturnSuccess)return false;memoryPrepared_=false;}
    if(memory_){memory_->release();memory_=nullptr;}
    if(mapper_){mapper_->release();mapper_=nullptr;}
    return true;
}
bool MacDmaBuffer::fail(DmaStatus status){status_=cleanup()?status:DmaStatus::cleanupFailed;return false;}
MacDmaBuffer::~MacDmaBuffer(){
    // Do not turn a teardown bug into DMA use-after-free. Retain underlying
    // allocations on ambiguous shutdown/unmap failure and report the leak.
    if(!cleanup())IOLog("RTL8852BE: DMA allocation retained: shutdown or unmap not confirmed\n");
}
bool MacDmaBuffer::allocate(IOPCIDevice *device,IOWorkLoop *workloop,size_t bytes,uint32_t alignment){
    if(memory_||command_||mapper_||visible_){status_=DmaStatus::busy;return false;}
    if(!device||!workloop||workloop->inGate()||!bytes||bytes>65536||!alignment||
       alignment>4096||(alignment&(alignment-1))||
       device->configRead16(kIOPCIConfigVendorID)!=0x10ec||device->configRead16(kIOPCIConfigDeviceID)!=0xb852){
        status_=DmaStatus::invalid;return false;
    }
    mapper_=IOMapper::copyMapperForDevice(device);
    // Small wired allocations use a 32-bit physical mask as well as a mapped
    // 32-bit DMA command. Never truncate a high device address to fit the BD.
    memory_=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
        kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMapInhibitCache,bytes,0xffffffffULL);
    if(!memory_)return fail(DmaStatus::allocateFailed);
    if(memory_->prepare()!=kIOReturnSuccess)return fail(DmaStatus::prepareFailed);
    memoryPrepared_=true;
    command_=IODMACommand::withSpecification(kIODMACommandOutputHost64,32,bytes,
        IODMACommand::kMapped,0,alignment,mapper_);
    if(!command_)return fail(DmaStatus::commandFailed);
    const auto attached=command_->setMemoryDescriptor(memory_,false);
    attached_=command_->getMemoryDescriptor()!=nullptr;
    if(attached!=kIOReturnSuccess||!attached_)return fail(DmaStatus::attachFailed);
    if(command_->prepare()!=kIOReturnSuccess)return fail(DmaStatus::prepareFailed);
    commandPrepared_=true;
    IODMACommand::Segment64 segment{};UInt64 offset=0;UInt32 count=1;
    if(command_->gen64IOVMSegments(&offset,&segment,&count)!=kIOReturnSuccess||count!=1||offset!=bytes||
       segment.fLength!=bytes||segment.fIOVMAddr>0xffffffffULL||bytes-1>0xffffffffULL-segment.fIOVMAddr||
       (segment.fIOVMAddr&(alignment-1)))return fail(DmaStatus::segmentFailed);
    auto *cpu=static_cast<uint8_t *>(memory_->getBytesNoCopy());
    if(!cpu)return fail(DmaStatus::segmentFailed);
    mapping_={cpu,segment.fIOVMAddr,bytes};ready_=true;
    for(size_t i=0;i<bytes;++i)cpu[i]=0;
    if(!syncForDevice())return fail(DmaStatus::syncFailed);
    status_=DmaStatus::ok;return true;
}
bool MacDmaBuffer::syncForDevice(){
    if(!ready_||!commandPrepared_){status_=DmaStatus::invalid;return false;}
    __atomic_thread_fence(__ATOMIC_RELEASE);
    if(command_->synchronize(kIODirectionOut)!=kIOReturnSuccess){status_=DmaStatus::syncFailed;return false;}
    __atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();status_=DmaStatus::ok;return true;
}
bool MacDmaBuffer::syncForCpu(){
    if(!ready_||!commandPrepared_){status_=DmaStatus::invalid;return false;}
    OSSynchronizeIO();
    if(command_->synchronize(kIODirectionIn)!=kIOReturnSuccess){status_=DmaStatus::syncFailed;return false;}
    __atomic_thread_fence(__ATOMIC_ACQUIRE);status_=DmaStatus::ok;return true;
}
bool MacDmaBuffer::markDeviceVisible(){if(!ready_){status_=DmaStatus::invalid;return false;}visible_=true;status_=DmaStatus::ok;return true;}
bool MacDmaBuffer::release(){if(visible_){status_=DmaStatus::busy;return false;}const bool ok=cleanup();status_=ok?DmaStatus::ok:DmaStatus::cleanupFailed;return ok;}
bool MacDmaBuffer::releaseAfterDmaStopped(){visible_=false;return release();}
} }
