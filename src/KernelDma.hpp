// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOMapper.h>
#include <kern/task.h>
#include "DmaProbe.hpp"

class KernelDma {
    struct Slot {
        IOBufferMemoryDescriptor *memory{};
        IODMACommand *dma{};
        bool memoryPrepared{};
        rtl8852be::dma::Mapping mapping{};
    } slots[2];
    IOPCIDevice *pci;
    IOMapper *mapper{};
    IOReturn error{kIOReturnSuccess};
public:
    explicit KernelDma(IOPCIDevice *p):pci(p),mapper(IOMapper::copyMapperForDevice(p)){}
    bool deviceMapper() const {return mapper!=nullptr;}
    uint16_t command(){return pci->configRead16(kIOPCIConfigCommand);}
    bool allocate(unsigned i){
        auto &s=slots[i];
        s.memory=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,kIODirectionInOut,
            rtl8852be::dma::pageBytes,0xfffff000ULL);
        if(!s.memory){error=kIOReturnNoMemory;return false;}
        return true;
    }
    bool prepare(unsigned i){
        auto &s=slots[i];
        error=s.memory->prepare(kIODirectionInOut);
        if(error!=kIOReturnSuccess)return false;
        s.memoryPrepared=true;
        s.dma=IODMACommand::withSpecification(IODMACommand::OutputHost64,32,4096,
            IODMACommand::kMapped,0,4096,mapper);
        if(!s.dma){error=kIOReturnNoMemory;return false;}
        error=s.dma->setMemoryDescriptor(s.memory,false);
        if(error!=kIOReturnSuccess)return false;
        error=s.dma->prepare(0,4096);
        if(error!=kIOReturnSuccess)return false;
        IODMACommand::Segment64 segments[2]{};UInt32 count=2;UInt64 offset=0;
        error=s.dma->gen64IOVMSegments(&offset,segments,&count);
        if(error!=kIOReturnSuccess)return false;
        s.mapping={segments[0].fIOVMAddr,segments[0].fLength,offset,count};
        return true;
    }
    rtl8852be::dma::Mapping mapping(unsigned i){return slots[i].mapping;}
    uint8_t *bytes(unsigned i){return static_cast<uint8_t *>(slots[i].memory->getBytesNoCopy());}
    bool synchronize(unsigned i){
        error=slots[i].dma->synchronize(kIODirectionOut);return error==kIOReturnSuccess;
    }
    uint32_t lastError()const{return static_cast<uint32_t>(error);}
    bool close(unsigned i){
        auto &s=slots[i];bool ok=true;
        if(s.dma){
            if(s.dma->getMemoryDescriptor())ok=s.dma->clearMemoryDescriptor(true)==kIOReturnSuccess;
            s.dma->release();s.dma=nullptr;
        }
        if(s.memory){
            if(s.memoryPrepared){ok=(s.memory->complete(kIODirectionInOut)==kIOReturnSuccess)&&ok;s.memoryPrepared=false;}
            s.memory->release();s.memory=nullptr;
        }
        return ok;
    }
    ~KernelDma(){close(1);close(0);if(mapper)mapper->release();}
};
