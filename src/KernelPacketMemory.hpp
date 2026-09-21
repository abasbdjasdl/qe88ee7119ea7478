// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOMapper.h>
#include <kern/task.h>
#include "DmaProbe.hpp"
class KernelPacketMemory {
    struct Slot {IOBufferMemoryDescriptor *memory;IODMACommand *dma;bool prepared;uint64_t address,length,offset;unsigned segments;};
    Slot *slots{};unsigned count{};IOPCIDevice *pci;IOMapper *mapper{};IOReturn error{kIOReturnSuccess};
    bool released{},cleanupOK{},hardwareAttempted{},stoppedProven{};
public:
    KernelPacketMemory(IOPCIDevice *p,unsigned n):pci(p){
        if(n<2||n>256){error=kIOReturnBadArgument;return;}
        count=n;slots=static_cast<Slot *>(IOMallocZero(sizeof(Slot)*count));
        if(!slots){error=kIOReturnNoMemory;return;}mapper=IOMapper::copyMapperForDevice(pci);
    }
    KernelPacketMemory(const KernelPacketMemory &)=delete;
    KernelPacketMemory &operator=(const KernelPacketMemory &)=delete;
    uint16_t command(){return pci->configRead16(kIOPCIConfigCommand);}
    bool deviceMapper()const{return mapper!=nullptr;}
    uint32_t lastError()const{return static_cast<uint32_t>(error);}
    bool allocate(unsigned i){
        if(!slots||i>=count||slots[i].memory||(command()&4)){error=kIOReturnNotReady;return false;}
        slots[i].memory=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,kIODirectionInOut,4096,0xfffff000ULL);
        if(!slots[i].memory){error=kIOReturnNoMemory;return false;}return true;
    }
    bool prepare(unsigned i){
        if(!slots||i>=count||!slots[i].memory||slots[i].prepared){error=kIOReturnNotReady;return false;}
        auto &s=slots[i];error=s.memory->prepare(kIODirectionInOut);if(error!=kIOReturnSuccess)return false;s.prepared=true;
        s.dma=IODMACommand::withSpecification(IODMACommand::OutputHost64,32,4096,IODMACommand::kMapped,0,4096,mapper);
        if(!s.dma){error=kIOReturnNoMemory;return false;}
        error=s.dma->setMemoryDescriptor(s.memory,false);if(error!=kIOReturnSuccess)return false;
        error=s.dma->prepare(0,4096);if(error!=kIOReturnSuccess)return false;
        IODMACommand::Segment64 segments[2]{};UInt32 n=2;UInt64 offset=0;
        error=s.dma->gen64IOVMSegments(&offset,segments,&n);if(error!=kIOReturnSuccess)return false;
        s.address=segments[0].fIOVMAddr;s.length=segments[0].fLength;s.offset=offset;s.segments=n;return true;
    }
    rtl8852be::dma::Mapping mapping(unsigned i)const{
        if(!slots||i>=count)return {};const auto &s=slots[i];return {s.address,s.length,s.offset,s.segments};
    }
    uint8_t *bytes(unsigned i){return slots&&i<count&&slots[i].memory?static_cast<uint8_t *>(slots[i].memory->getBytesNoCopy()):nullptr;}
    bool synchronize(unsigned i){
        if(!slots||i>=count||!slots[i].dma){error=kIOReturnNotReady;return false;}
        error=slots[i].dma->synchronize(kIODirectionOut);return error==kIOReturnSuccess;
    }
    void markHardwareAttempt(){hardwareAttempted=true;}
    bool confirmStopped(bool idleOrPowerOff){stoppedProven=idleOrPowerOff&&!(command()&4);return stoppedProven;}
    // This stage never publishes addresses/enables DMA. Future DMA users must
    // additionally prove idle and keep this object alive until that proof.
    bool releaseUnsubmitted(){
        if(released)return cleanupOK;
        released=true;cleanupOK=false;
        if((command()&4)||(hardwareAttempted&&!stoppedProven)){error=kIOReturnBusy;return false;}
        bool ok=true;
        if(slots)for(unsigned n=count;n>0;--n){auto &s=slots[n-1];bool slotOK=true;
            if(s.dma&&s.dma->getMemoryDescriptor()){
                const auto result=s.dma->clearMemoryDescriptor(true);
                if(result!=kIOReturnSuccess){error=result;slotOK=false;}
            }
            if(slotOK&&s.memory&&s.prepared){const auto result=s.memory->complete(kIODirectionInOut);
                if(result==kIOReturnSuccess)s.prepared=false;else{error=result;slotOK=false;}}
            if(slotOK){if(s.dma){s.dma->release();s.dma=nullptr;}if(s.memory){s.memory->release();s.memory=nullptr;}}
            ok=slotOK&&ok;
        }
        cleanupOK=ok;return ok;
    }
    ~KernelPacketMemory(){
        // On failed unmap/complete or unexpected bus-master activation retain
        // unresolved objects until reboot rather than freeing referenced pages.
        if(releaseUnsubmitted()){if(slots)IOFree(slots,sizeof(Slot)*count);if(mapper)mapper->release();}
    }
};
