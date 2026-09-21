// Test-only model of the IOKit methods used by the real MacDmaBuffer.cpp.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cassert>
#include <vector>
#include <string>
using UInt8=uint8_t;using UInt32=uint32_t;using UInt64=uint64_t;using IOReturn=int;
constexpr int kIOReturnSuccess=0,kIODirectionIn=1,kIODirectionOut=2,kIODirectionInOut=3;
constexpr int kIOMemoryPhysicallyContiguous=4,kIOMapInhibitCache=8,kIODMACommandOutputHost64=64;
constexpr int kIOPCIConfigVendorID=0,kIOPCIConfigDeviceID=2,kernel_task=0;
struct DmaFake {
    enum Failure {none,allocation,memoryPrepare,command,attach,dmaPrepare,segments,cpuPointer,syncOut,syncIn,commandComplete,clear,memoryComplete};
    Failure failure=none;unsigned memories{},commands{},mappers{},memoryPrepared{},dmaPrepared{},logs{};
    bool noMapper{},partialAttach{};uint64_t bus=0x120000,lengthOverride{},offsetOverride{};uint32_t segmentCount=1;
    uint32_t requestedAlignment{};size_t requestedBytes{};std::vector<std::string> trace;
    void record(const char *s){trace.emplace_back(s);}
};
extern DmaFake fake;
struct IOPCIDevice {uint16_t vendor=0x10ec,device=0xb852;uint16_t configRead16(int key){return key?device:vendor;}};
struct IOWorkLoop {bool gated{};bool inGate()const{return gated;}};
struct IOMapper {
    unsigned refs=1;
    static IOMapper *copyMapperForDevice(IOPCIDevice *){fake.record("mapper");if(fake.noMapper)return nullptr;++fake.mappers;return new IOMapper;}
    void retain(){++refs;}void release(){fake.record("mapperRelease");if(!--refs){--fake.mappers;delete this;}}
};
struct IOBufferMemoryDescriptor {
    std::vector<uint8_t> bytes;unsigned refs=1;bool prepared{};
    explicit IOBufferMemoryDescriptor(size_t n):bytes(n,0xa5){}
    static IOBufferMemoryDescriptor *inTaskWithPhysicalMask(int,int flags,size_t bytes,uint64_t mask){
        fake.record("allocate");assert(flags==15&&mask==0xffffffffULL);fake.requestedBytes=bytes;
        if(fake.failure==DmaFake::allocation)return nullptr;++fake.memories;return new IOBufferMemoryDescriptor(bytes);
    }
    int prepare(){fake.record("memoryPrepare");if(fake.failure==DmaFake::memoryPrepare)return 1;prepared=true;++fake.memoryPrepared;return 0;}
    int complete(){fake.record("memoryComplete");assert(prepared);if(fake.failure==DmaFake::memoryComplete)return 1;prepared=false;--fake.memoryPrepared;return 0;}
    void *getBytesNoCopy(){return fake.failure==DmaFake::cpuPointer?nullptr:bytes.data();}
    void retain(){++refs;}void release(){fake.record("memoryRelease");if(!--refs){assert(!prepared);--fake.memories;delete this;}}
};
struct IODMACommand {
    struct Segment64 {uint64_t fIOVMAddr{},fLength{};};enum {kMapped=1};
    IOBufferMemoryDescriptor *memory{};IOMapper *mapper{};bool prepared{};
    static IODMACommand *withSpecification(int output,int bits,uint64_t maxSegment,int mapping,uint64_t maxTransfer,uint32_t alignment,IOMapper *mapper){
        fake.record("command");assert(output==64&&bits==32&&maxSegment==fake.requestedBytes&&mapping==kMapped&&!maxTransfer);
        fake.requestedAlignment=alignment;if(fake.failure==DmaFake::command)return nullptr;
        auto *c=new IODMACommand;c->mapper=mapper;if(mapper)mapper->retain();++fake.commands;return c;
    }
    int setMemoryDescriptor(IOBufferMemoryDescriptor *m,bool autoPrepare){
        fake.record("attach");assert(!autoPrepare&&!memory);
        if(fake.failure==DmaFake::attach&&!fake.partialAttach)return 1;
        memory=m;m->retain();return fake.failure==DmaFake::attach?1:0;
    }
    IOBufferMemoryDescriptor *getMemoryDescriptor(){return memory;}
    int prepare(){fake.record("dmaPrepare");assert(memory&&memory->prepared);if(fake.failure==DmaFake::dmaPrepare)return 1;prepared=true;++fake.dmaPrepared;return 0;}
    int gen64IOVMSegments(UInt64 *offset,Segment64 *s,UInt32 *count){
        fake.record("segments");assert(prepared&&!*offset&&*count==1);if(fake.failure==DmaFake::segments)return 1;
        *offset=fake.offsetOverride?fake.offsetOverride:memory->bytes.size();*count=fake.segmentCount;
        *s={fake.bus,fake.lengthOverride?fake.lengthOverride:memory->bytes.size()};return 0;
    }
    int synchronize(int direction){
        assert(prepared);fake.record(direction==kIODirectionOut?"out":"in");
        return fake.failure==(direction==kIODirectionOut?DmaFake::syncOut:DmaFake::syncIn)?1:0;
    }
    int complete(){fake.record("dmaComplete");assert(prepared);if(fake.failure==DmaFake::commandComplete)return 1;prepared=false;--fake.dmaPrepared;return 0;}
    int clearMemoryDescriptor(bool autoComplete){
        fake.record("clear");assert(!autoComplete&&!prepared&&memory);if(fake.failure==DmaFake::clear)return 1;
        memory->release();memory=nullptr;return 0;
    }
    void release(){fake.record("commandRelease");assert(!memory&&!prepared);if(mapper)mapper->release();--fake.commands;delete this;}
};
inline void OSSynchronizeIO(){fake.record("barrier");}
inline void IOLog(const char *){++fake.logs;}
