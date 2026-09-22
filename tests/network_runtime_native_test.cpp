// SPDX-License-Identifier: BSD-3-Clause
// Execute the actual native adapter with a PCI/MMIO model, not a replacement adapter.
#define IOPCIDevice RfkTestPciDevice
#include "network_rfk_fakes/Fake.hpp"
#undef IOPCIDevice
#include <cstdio>
#include <initializer_list>
class IOPCIDevice:public RfkTestPciDevice {
public:unsigned configWrites{};bool ignoreConfig{};
    void configWrite16(unsigned a,uint16_t v){assert(a==kIOPCIConfigCommand);++configWrites;if(!ignoreConfig)command=v;}
};
inline uint16_t OSReadLittleInt16(const volatile void *b,unsigned a){
    ++fakeRfk::accesses;const auto p=reinterpret_cast<const volatile uint8_t *>(b)+a;
    return uint16_t(p[0])|(uint16_t(p[1])<<8);
}
inline void OSWriteLittleInt16(volatile void *b,unsigned a,uint16_t v){
    ++fakeRfk::accesses;auto p=reinterpret_cast<volatile uint8_t *>(b)+a;p[0]=uint8_t(v);p[1]=uint8_t(v>>8);
}
#include "../src/network/MacPciRuntimeIo.cpp"
namespace n=rtl8852be::network;
int main(){
    IOPCIDevice device;IOMemoryMap map;n::MacPciRuntimeIo io(&device,&map);assert(io.valid());
    const auto index=n::ringRegisters[0].index;map.set(index,0x002a0001);
    fakeRfk::reset();assert(io.read16(index)==1&&io.read32(index)==0x002a0001&&fakeRfk::barriers==2);
    assert(!io.write16(index,2));device.command=6;assert(io.write16(index,2)&&map.get(index)==0x002a0002);
    assert(!io.write16(index+2,1)&&!io.write16(index,64));
    assert(io.write32(n::irqMasks[0],n::irqEnabled[0]));assert(!io.write32(n::irqMasks[0],~0u));
    // PCI removal returns all ones, including the MEM and BM bits. Those bits
    // must not accidentally authorize any access to the now-invalid BAR map.
    for(auto cmd:{uint16_t(0xffff),uint16_t(0),uint16_t(4)}){
        device.command=cmd;const auto accesses=fakeRfk::accesses,writes=device.configWrites;
        assert(io.read16(index)==0xffff&&io.read32(index)==0xffffffff);
        assert(!io.write16(index,3)&&!io.write32(n::irqMasks[0],0));
        assert(fakeRfk::accesses==accesses);
        if(cmd==0xffff)assert(!io.writeCommand(2)&&device.configWrites==writes);
    }
    device.command=6;const auto before=fakeRfk::accesses;
    assert(io.read16(index+1)==0xffff&&io.read32(index+1)==0xffffffff);
    assert(io.read32(0x20000)==0xffffffff&&io.read16(0x1ffff)==0xffff);
    assert(!io.write32(0x20000,0)&&!io.write16(0x20000,0)&&fakeRfk::accesses==before);
    assert(io.writeCommand(0x402)&&device.command==0x402); // stop BM, retain MEM, disable INTx
    assert(!io.writeCommand(0x400)&&!io.writeCommand(0x502));
    device.ignoreConfig=true;assert(!io.writeCommand(0x406));
    {IOPCIDevice wrong;wrong.device=0xffff;n::MacPciRuntimeIo invalid(&wrong,&map);assert(!invalid.valid());}
    puts("PASS: native PCI runtime rejects removed-device all-one command without touching MMIO, preserves device index half, orders reads, bounds/alignment and narrow PCI command writes; modeled hardware");
}
