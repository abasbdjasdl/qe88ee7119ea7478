// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
constexpr unsigned kIOPCIConfigVendorID=0,kIOPCIConfigDeviceID=2,kIOPCIConfigCommand=4,kIOPCIConfigBaseAddress2=0x18;
namespace fakeRfk {
static unsigned accesses{},badRead{},ignoredWrite{},delays{},barriers{};
static uint64_t time=100;
inline void reset(){accesses=badRead=ignoredWrite=delays=barriers=0;time=100;}
}
class IOMemoryDescriptor {
public: uint64_t physical=0x10000000;uint64_t length=0x20000;
    uint64_t getPhysicalAddress(){return physical;}uint64_t getLength(){return length;}
};
class IOMemoryMap:public IOMemoryDescriptor {
public: alignas(8) uint8_t data[0x20000]{};
    uintptr_t getVirtualAddress(){return reinterpret_cast<uintptr_t>(data);}
    void set(unsigned a,uint32_t v){assert(a+4<=sizeof(data));std::memcpy(data+a,&v,4);}
    uint32_t get(unsigned a){uint32_t v;assert(a+4<=sizeof(data));std::memcpy(&v,data+a,4);return v;}
};
class IOPCIDevice {
public: uint16_t vendor=0x10ec,device=0xb852,command=2;IOMemoryDescriptor bar;
    uint16_t configRead16(unsigned a){return a==0?vendor:a==2?device:a==4?command:0xffff;}
    IOMemoryDescriptor *getDeviceMemoryWithRegister(unsigned a){return a==kIOPCIConfigBaseAddress2?&bar:nullptr;}
};
inline uint32_t OSReadLittleInt32(const volatile void *b,unsigned a){
    const auto count=++fakeRfk::accesses;if(count==fakeRfk::badRead)return 0xffffffff;
    const auto p=reinterpret_cast<const volatile uint8_t *>(b)+a;
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
inline void OSWriteLittleInt32(volatile void *b,unsigned a,uint32_t v){
    if(++fakeRfk::accesses==fakeRfk::ignoredWrite)return;
    auto p=reinterpret_cast<volatile uint8_t *>(b)+a;for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(i*8));
}
inline void OSSynchronizeIO(){++fakeRfk::barriers;}
inline void IODelay(unsigned us){++fakeRfk::delays;fakeRfk::time+=us;}
inline void IOSleep(unsigned ms){IODelay(ms*1000);}
inline void clock_get_uptime(uint64_t *v){*v=fakeRfk::time;}
inline void absolutetime_to_nanoseconds(uint64_t t,uint64_t *v){*v=t*1000;}
