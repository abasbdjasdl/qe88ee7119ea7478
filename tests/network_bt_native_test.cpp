// SPDX-License-Identifier: BSD-3-Clause
#include "network_rfk_fakes/Fake.hpp"
inline uint16_t OSReadLittleInt16(const volatile void *base,unsigned a){
    ++fakeRfk::accesses;auto p=static_cast<const volatile uint8_t *>(base)+a;return uint16_t(p[0])|(uint16_t(p[1])<<8);}
inline void OSWriteLittleInt16(volatile void *base,unsigned a,uint16_t v){
    ++fakeRfk::accesses;auto p=static_cast<volatile uint8_t *>(base)+a;p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
#include "../src/network/MacBtRfkIo.cpp"
#include <cstdio>
#include <vector>
#include <initializer_list>
namespace b=rtl8852be::bt;namespace n=rtl8852be::network;
struct Transport {
    IOWorkLoop &loop;unsigned calls{},callbacks{};std::vector<uint8_t> bytes;uint64_t token{};
    bool inGate(){return loop.inGate();}uint64_t nowUs(){return fakeRfk::time;}
    bool publish(const uint8_t *p,size_t size){++calls;bytes.assign(p,p+size);return true;}
    static bool event(void *p,const n::FirmwareEvent &event,uint64_t epoch,uint64_t token){
        auto &t=*static_cast<Transport *>(p);n::FirmwareAck ack{};assert(n::decodeAck(event,ack)&&ack.done&&epoch==1);
        ++t.callbacks;t.token=token;return true;
    }
};
struct Fixture {
    IOPCIDevice device;IOMemoryMap map;IOWorkLoop loop;Transport transport{loop,0,0,{},0};
    n::FirmwareCommands<Transport> commands{transport,1};
    n::FirmwareCommandClient<Transport> client{commands,{&transport,Transport::event},23,300000};
    b::MacBtRfkIo io{&device,&map,&loop,client.link()};
    Fixture(){fakeRfk::reset();}
};
int main(){
    {Fixture f;assert(f.io.valid()&&f.io.inGate());uint8_t byte=0;uint16_t half=0;uint32_t word=0;
        f.map.set(0x70,0xaabbccdd);assert(f.io.read8(b::controlPathRegister,byte)&&byte==0xaa);
        assert(f.io.write8(b::controlPathRegister,0x55)&&f.map.get(0x70)==0x55bbccdd);
        f.map.set(b::priorityRegister,0xaabbccdd);assert(f.io.read16(b::priorityRegister,half)&&half==0xccdd);
        assert(f.io.write16(b::priorityRegister,0x1234)&&f.map.get(b::priorityRegister)==0xaabb1234);
        f.map.set(b::schedulerRegister,0xffff0000);assert(f.io.read16(b::schedulerRegister,half)&&half==0);
        for(auto a:{b::scoreboardRegister,b::firmwareControl,b::cmacFunctionRegister,b::lteReadData}){
            f.map.set(a,0x10203040);assert(f.io.read32(a,word)&&word==0x10203040);}
        f.map.set(b::lteControl,0x20000000);assert(f.io.read8(b::lteControl+3,byte)&&byte==0x20);
        assert(f.io.write32(b::lteControl,0x800f0038)&&f.io.write32(b::lteControl,0xc00f0038));
        assert(f.io.write32(b::lteWriteData,0xffffffff));assert(f.io.write32(b::scoreboardRegister,0x81234567));
        for(auto a:{0u,1u,0x10000u,b::firmwareControl,b::schedulerRegister,b::lteControl+1})
            assert(!f.io.write32(a,0)&&!f.io.write16(a,0)&&!f.io.write8(a,0));
        assert(!f.io.write32(b::lteControl,0xc00f0039)&&!f.io.write32(b::scoreboardRegister,0x80000000));
        assert(!f.io.read32(b::lteControl,word)&&!f.io.read16(b::priorityRegister+1,half)&&!f.io.read8(b::priorityRegister,byte));
        auto before=fakeRfk::time;assert(f.io.delayUs(1000)&&fakeRfk::time==before+1000);
        assert(!f.io.delayUs(1001));
    }
    for(unsigned command:{0u,0xffffu}){Fixture f;f.device.command=uint16_t(command);const auto before=fakeRfk::accesses;
        uint32_t word=99;assert(!f.io.read32(b::scoreboardRegister,word)&&word==0&&!f.io.write32(b::lteWriteData,1));
        assert(!f.io.delayUs(1)&&fakeRfk::accesses==before);}
    for(unsigned failure=0;failure<3;++failure){Fixture f;
        if(failure==0)f.loop.gate=false;else if(failure==1)f.io.cancel();else f.commands.invalidate();
        assert(!f.io.write8(b::controlPathRegister,0xff)&&f.map.get(0x70)==0);
        assert(!f.io.write16(b::priorityRegister,0xffff)&&f.map.get(b::priorityRegister)==0);
        assert(!f.io.delayUs(1));}
    {Fixture f;uint8_t payload[26]{};uint8_t sequence=88;
        assert(b::encodePolicy({3,1},b::calibrationPolicy(),payload));
        assert(!f.io.submitH2c({1,8,4},true,payload,26,sequence));
        assert(!f.io.submitH2c(b::policyCommand,false,payload,26,sequence));
        assert(!f.io.submitH2c(b::policyCommand,true,payload,25,sequence)&&!f.transport.calls);
        assert(f.io.submitH2c(b::policyCommand,true,payload,26,sequence)&&sequence==0&&f.transport.calls==1);
        payload[0]=99;assert(f.transport.bytes.size()==34&&f.transport.bytes[8]==0);
        uint8_t ackBytes[4]{};n::store32(ackBytes,2|(16<<2)|(3<<8));n::FirmwareEvent ack{{1,0,1},ackBytes,4};
        assert(f.commands.accept(ack,1)==n::CommandEvent::completed&&f.transport.callbacks==1&&f.transport.token==23);
        f.io.invalidateFirmwareEpoch();assert(f.commands.faulted()&&!f.io.write32(b::lteWriteData,1));}
    {Fixture f;b::MacBtRfkIo missing(&f.device,&f.map,&f.loop,{});assert(!missing.valid());
        b::MacBtRfkIo noLoop(&f.device,&f.map,nullptr,f.client.link());assert(!noLoop.valid());
        f.device.vendor=0;b::MacBtRfkIo wrong(&f.device,&f.map,&f.loop,f.client.link());assert(!wrong.valid());}
    {Fixture f;f.map.physical+=4096;b::MacBtRfkIo wrong(&f.device,&f.map,&f.loop,f.client.link());assert(!wrong.valid());}
    {Fixture f;f.map.length=f.device.bar.length=0x8000;b::MacBtRfkIo shortMap(&f.device,&f.map,&f.loop,f.client.link());assert(!shortMap.valid());}
    puts("PASS: actual native BT MMIO source, register widths/allowlist, PCI/BAR/gate/cancel checks, shared command client copying/ACK delivery and global invalidation; modeled hardware");
}
