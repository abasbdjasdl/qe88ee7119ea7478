// SPDX-License-Identifier: BSD-3-Clause
// Compiles the actual native source against an explicit IOKit allocation model.
#include "network_dma_fakes/DmaFake.hpp"
#include "../src/network/MacDmaBuffer.cpp"
#include <cstdio>
#include <algorithm>
DmaFake fake;
using namespace rtl8852be::network;
void empty(){assert(!fake.memories&&!fake.commands&&!fake.mappers&&!fake.memoryPrepared&&!fake.dmaPrepared&&!fake.logs);}
void reset(){empty();fake={};}
void life(){
    IOPCIDevice device;IOWorkLoop loop;
    {MacDmaBuffer b;assert(b.allocate(&device,&loop,16384,4096));auto m=b.mapping();
        assert(m.physical==fake.bus&&m.capacity==16384&&fake.requestedAlignment==4096);
        for(size_t i=0;i<m.capacity;++i)assert(m.bytes[i]==0);
        assert(!b.allocate(&device,&loop,16384,4096)&&b.status()==DmaStatus::busy);
        m.bytes[25]=0x5a;assert(b.syncForDevice()&&b.markDeviceVisible()&&b.deviceVisible());
        assert(!b.release()&&b.status()==DmaStatus::busy&&fake.memories==1);
        assert(b.syncForCpu()&&b.releaseAfterDmaStopped()&&!b.mapping().bytes);empty();
        assert(b.release());assert(!b.syncForCpu()&&!b.markDeviceVisible());
        assert(b.allocate(&device,&loop,512,8));
    }empty();
    reset();fake.noMapper=true;{MacDmaBuffer b;assert(b.allocate(&device,&loop,512,8));}empty();
}
void failures(){
    IOPCIDevice device;IOWorkLoop loop;
    for(auto error:{DmaFake::allocation,DmaFake::memoryPrepare,DmaFake::command,DmaFake::attach,DmaFake::dmaPrepare,DmaFake::segments,DmaFake::cpuPointer,DmaFake::syncOut}){
        reset();fake.failure=error;{MacDmaBuffer b;assert(!b.allocate(&device,&loop,4096,8));assert(!b.mapping().bytes);empty();}empty();
    }
    reset();fake.failure=DmaFake::attach;fake.partialAttach=true;
    {MacDmaBuffer b;assert(!b.allocate(&device,&loop,4096,8));empty();}
    for(auto error:{DmaFake::commandComplete,DmaFake::clear,DmaFake::memoryComplete}){
        reset();MacDmaBuffer b;assert(b.allocate(&device,&loop,512,8));fake.failure=error;
        assert(!b.release()&&b.status()==DmaStatus::cleanupFailed&&fake.memories==1&&!b.mapping().bytes);
        assert(!b.allocate(&device,&loop,512,8));fake.failure=DmaFake::none;assert(b.release());empty();
    }
    for(auto error:{DmaFake::syncOut,DmaFake::syncIn}){
        reset();MacDmaBuffer b;assert(b.allocate(&device,&loop,512,8));assert(b.markDeviceVisible());fake.failure=error;
        assert(!(error==DmaFake::syncOut?b.syncForDevice():b.syncForCpu()));assert(!b.release()&&fake.memories==1);
        fake.failure=DmaFake::none;assert(b.releaseAfterDmaStopped());empty();
    }
}
void boundaries(){
    IOPCIDevice device;IOWorkLoop loop;
    for(uint64_t address:{0x100000000ULL,0xfffffff8ULL,0x120001ULL}){
        reset();fake.bus=address;MacDmaBuffer b;assert(!b.allocate(&device,&loop,512,8)&&b.status()==DmaStatus::segmentFailed);empty();
    }
    for(unsigned mode=0;mode<3;++mode){
        reset();if(mode==0)fake.segmentCount=2;if(mode==1)fake.lengthOverride=128;if(mode==2)fake.offsetOverride=128;
        MacDmaBuffer b;assert(!b.allocate(&device,&loop,512,8));empty();
    }
    reset();MacDmaBuffer b;
    for(uint32_t alignment:{0u,3u,8192u})assert(!b.allocate(&device,&loop,512,alignment));
    assert(!b.allocate(&device,&loop,0,8)&&!b.allocate(&device,&loop,65537,8));
    assert(!b.allocate(nullptr,&loop,512,8)&&!b.allocate(&device,nullptr,512,8));
    loop.gated=true;assert(!b.allocate(&device,&loop,512,8));loop.gated=false;
    device.device=0;assert(!b.allocate(&device,&loop,512,8));assert(fake.trace.empty());
}
int main(){life();failures();boundaries();empty();std::puts("Native DMA: lifecycle, mapper/no-mapper, all allocation/unwind failures, sync failure ownership, invalid/fragmented/high addresses and gated-context rejection passed");}
