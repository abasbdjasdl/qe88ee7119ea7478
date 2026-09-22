// SPDX-License-Identifier: BSD-3-Clause
// Actual native source under a local PCI/MMIO model. No shared fake modifications.
#define OSWriteLittleInt32 powerBaseWrite32
#include "network_rfk_fakes/Fake.hpp"
#undef OSWriteLittleInt32
#include "../src/network/Rtw8852bDevicePowerConstants.hpp"
#include <vector>
#include <cstdio>
#include <initializer_list>
namespace p=rtl8852be::powerseq;
namespace model {
struct Write {unsigned address;uint32_t value;};
static std::vector<Write> writes;
static bool effects{};
static uint8_t serial[256]{};
static void *owner{};
static void (*afterWrite)(void *,unsigned,uint32_t){};
inline void put(volatile void *base,unsigned a,uint32_t value){
    auto bytes=static_cast<volatile uint8_t *>(base)+a;
    for(unsigned i=0;i<4;++i)bytes[i]=uint8_t(value>>(8*i));
}
inline void reset(){writes.clear();effects=false;std::memset(serial,0,sizeof serial);owner=nullptr;afterWrite=nullptr;}
}
inline uint16_t OSReadLittleInt16(const volatile void *base,unsigned a){
    ++fakeRfk::accesses;auto bytes=static_cast<const volatile uint8_t *>(base)+a;
    return uint16_t(bytes[0])|(uint16_t(bytes[1])<<8);
}
inline void OSWriteLittleInt16(volatile void *base,unsigned a,uint16_t value){
    ++fakeRfk::accesses;auto bytes=static_cast<volatile uint8_t *>(base)+a;
    bytes[0]=uint8_t(value);bytes[1]=uint8_t(value>>8);
}
inline void OSWriteLittleInt32(volatile void *base,unsigned a,uint32_t value){
    powerBaseWrite32(base,a,value);model::writes.push_back({a,value});
    if(model::effects){
        if(a==p::R_AX_WLAN_XTAL_SI_CTRL){
            assert(value&0x80000000u);const uint8_t address=uint8_t(value),mask=uint8_t(value>>16),data=uint8_t(value>>8);
            if(value&0x01000000u)model::put(base,a,(value&~0x8000ff00u)|(uint32_t(model::serial[address])<<8));
            else {model::serial[address]=uint8_t((model::serial[address]&~mask)|(data&mask));model::put(base,a,value&~0x80000000u);}
        }
        if(a==p::R_AX_SYS_PW_CTRL&&(value&p::B_AX_APFN_ONMAC)){
            model::put(base,a,value&~p::B_AX_APFN_ONMAC);model::put(base,p::R_AX_IC_PWR_STATE,0x100);
        }
    }
    if(model::afterWrite)model::afterWrite(model::owner,a,value);
}
#include "../src/network/MacDevicePowerIo.cpp"
struct Fixture {
    IOPCIDevice device;IOMemoryMap map;IOWorkLoop loop;p::MacDevicePowerIo io{&device,&map,&loop};
    Fixture(){fakeRfk::reset();model::reset();}
};
static void widths(){
    Fixture f;assert(f.io.valid()&&f.io.inGate()&&f.io.command()==2);
    uint8_t byte=0;uint16_t half=0;uint32_t word=0;
    f.map.set(p::R_AX_SYS_FUNC_EN&~3u,0xaabbccdd);
    assert(f.io.read8(p::R_AX_SYS_FUNC_EN,byte)&&byte==0xbb);
    assert(f.io.write8(p::R_AX_SYS_FUNC_EN,0x55)&&f.map.get(0)==0xaa55ccdd);
    f.map.set(p::R_AX_PLATFORM_ENABLE,0x12345678);
    assert(f.io.write8(p::R_AX_PLATFORM_ENABLE,0xa5)&&f.map.get(p::R_AX_PLATFORM_ENABLE)==0x123456a5);
    f.map.set(p::R_AX_SCOREBOARD,0x12345678);
    assert(!f.io.write8(p::R_AX_SCOREBOARD+3,0x82)&&f.map.get(p::R_AX_SCOREBOARD)==0x12345678);
    assert(f.io.write8(p::R_AX_SCOREBOARD+3,p::MAC_AX_NOTIFY_TP_MAJOR)&&f.map.get(p::R_AX_SCOREBOARD)==0x81345678);
    assert(f.io.write8(p::R_AX_SCOREBOARD+3,p::MAC_AX_NOTIFY_PWR_MAJOR)&&f.map.get(p::R_AX_SCOREBOARD)==0x80345678);
    f.map.set(p::R_AX_HCI_LDO_CTRL&~3u,0xaabbccdd);
    assert(f.io.read16(p::R_AX_HCI_LDO_CTRL,half)&&half==0xaabb);
    assert(f.io.write16(p::R_AX_HCI_LDO_CTRL,0x1234)&&f.map.get(p::R_AX_HCI_LDO_CTRL&~3u)==0x1234ccdd);
    f.map.set(p::R_AX_SPS_ANA_ON_CTRL2,0xaabbccdd);
    assert(f.io.write16(p::R_AX_SPS_ANA_ON_CTRL2,0x5678)&&f.map.get(p::R_AX_SPS_ANA_ON_CTRL2)==0xaabb5678);
    const unsigned rw[]={p::R_AX_SYS_ISO_CTRL,p::R_AX_SYS_PW_CTRL,p::R_AX_SYS_SWR_CTRL1,
        p::R_AX_SYS_ADIE_PAD_PWR_CTRL,p::R_AX_AFE_LDO_CTRL,p::R_AX_SYS_SDIO_CTRL,p::R_AX_WLLPS_CTRL,
        p::R_AX_PMC_DBG_CTRL2,p::R_AX_SPS_DIG_ON_CTRL0,p::R_AX_EECS_EESK_FUNC_SEL,p::R_AX_WLRF_CTRL,
        p::R_AX_SPS_DIG_OFF_CTRL0,p::R_AX_DMAC_FUNC_EN,p::R_AX_CMAC_FUNC_EN};
    for(auto a:rw){assert(f.io.write32(a,0x12345678)&&f.io.read32(a,word)&&word==0x12345678);}
    for(auto a:{p::R_AX_IC_PWR_STATE,0x1000u,0x101cu,0x1a0u,0x10b0u,0x13b0u}){
        f.map.set(a,0x11223344);assert(f.io.read32(a,word)&&word==0x11223344);
        assert(!f.io.write32(a,0)&&f.map.get(a)==0x11223344);
    }
    f.map.set(p::R_AX_WLAN_XTAL_SI_CTRL,0x0012ff90);
    assert(f.io.read8(p::R_AX_WLAN_XTAL_SI_CTRL+1,byte)&&byte==0xff);
    assert(!f.io.write8(p::R_AX_WLAN_XTAL_SI_CTRL+1,0));
    for(auto a:{0x30u,0x38u,0x62u,0x63u,0x7au,0x8000u,0x10000u,0xffffffffu}){
        assert(!f.io.write32(a,0)&&!f.io.read32(a,word));
    }
    assert(!f.io.write16(p::R_AX_SYS_ISO_CTRL,0)&&!f.io.read16(p::R_AX_SYS_ISO_CTRL,half));
    assert(!f.io.write8(p::R_AX_SYS_PW_CTRL,0)&&!f.io.read8(p::R_AX_SYS_PW_CTRL,byte));
    assert(!f.io.write32(p::R_AX_HCI_LDO_CTRL,0)&&!f.io.read32(p::R_AX_HCI_LDO_CTRL,word));
    assert(!f.io.write16(p::R_AX_HCI_LDO_CTRL+1,0));
}
static void serialCommands(){
    Fixture f;
    const unsigned addresses[]={p::XTAL_SI_ANAPAR_WL,p::XTAL_SI_SRAM_CTRL,p::XTAL_SI_XTAL_XMD_2,
        p::XTAL_SI_XTAL_XMD_4,p::XTAL_SI_WL_RFC_S0,p::XTAL_SI_WL_RFC_S1};
    for(unsigned a=0;a<256;++a){
        bool allowed=false;for(auto target:addresses)allowed|=a==target;
        f.map.set(p::R_AX_WLAN_XTAL_SI_CTRL,0);
        assert(f.io.write32(p::R_AX_WLAN_XTAL_SI_CTRL,0x81000000u|a)==allowed);
    }
    for(unsigned bit=0;bit<8;++bit)for(unsigned data:{0u,1u<<bit}){
        const auto value=0x80000000u|((1u<<bit)<<16)|(data<<8)|p::XTAL_SI_ANAPAR_WL;
        assert(f.io.write32(p::R_AX_WLAN_XTAL_SI_CTRL,value));
    }
    for(auto command:{0x800200a1u,0x80700024u,0x800f0026u,0x80010080u,0x80010081u})
        assert(f.io.write32(p::R_AX_WLAN_XTAL_SI_CTRL,command));
    // DAV/OTP read and write register addresses are all forbidden on this adapter,
    // in addition to programming modes and malformed mask/data fields.
    for(auto bad:{0x80ff4063u,0x80ffc063u,0x80000063u,0x80ff0062u,0x8100007au,0x81000010u,
        0x82000090u,0x83000090u,0xc1000090u,0x01000090u,0x81010090u,0x81000190u,
        0x80000190u,0x80030390u,0x80000090u,0x800202a1u,0x80701024u,
        0x800f0126u,0x80010180u,0x80020081u}){
        f.map.set(p::R_AX_WLAN_XTAL_SI_CTRL,0x12345678);
        assert(!f.io.write32(p::R_AX_WLAN_XTAL_SI_CTRL,bad)&&f.map.get(p::R_AX_WLAN_XTAL_SI_CTRL)==0x12345678);
    }
}
static void accessAndCancellation(){
    for(unsigned command:{0u,4u,6u,7u,0xffffu}){
        Fixture f;f.device.command=uint16_t(command);const auto accesses=fakeRfk::accesses;
        uint8_t byte=9;uint16_t half=9;uint32_t word=9;
        assert(!f.io.read8(p::R_AX_SYS_FUNC_EN,byte)&&!byte);
        assert(!f.io.read16(p::R_AX_HCI_LDO_CTRL,half)&&!half);
        assert(!f.io.read32(p::R_AX_SYS_PW_CTRL,word)&&!word);
        assert(!f.io.write8(p::R_AX_SYS_FUNC_EN,1)&&!f.io.write16(p::R_AX_HCI_LDO_CTRL,1)&&!f.io.write32(p::R_AX_SYS_PW_CTRL,1));
        assert(!f.io.delayUs(1)&&accesses==fakeRfk::accesses);
    }
    {Fixture f;f.loop.gate=false;uint32_t word;assert(!f.io.read32(p::R_AX_SYS_PW_CTRL,word)&&!f.io.write32(p::R_AX_SYS_PW_CTRL,1)&&!f.io.delayUs(1));}
    {Fixture f;auto before=f.io.nowUs();assert(f.io.delayUs(1)&&f.io.delayUs(1000)&&f.io.nowUs()==before+1001);
        assert(!f.io.delayUs(1001));}
    {Fixture f;f.io.cancel();p::DevicePower<p::MacDevicePowerIo> power(f.io);
        assert(!power.start(0)&&power.result.on.error==p::Error::cancelled&&model::writes.empty());
        // Cancellation policy belongs to DevicePower. Raw I/O must remain
        // available for the narrow cleanup path while gate/PCI checks still hold.
        assert(f.io.write32(p::R_AX_PMC_DBG_CTRL2,0x100)&&f.io.cancelled());
        f.device.command=6;assert(!f.io.write32(p::R_AX_PMC_DBG_CTRL2,0));}
    // A command register or gate change during a write must be reported after
    // MMIO rather than returning success based on the pre-access observation.
    for(bool gateLoss:{false,true}){Fixture f;model::owner=&f;
        model::afterWrite=gateLoss?+[](void *p,unsigned,uint32_t){static_cast<Fixture *>(p)->loop.gate=false;}:
            +[](void *p,unsigned,uint32_t){static_cast<Fixture *>(p)->device.command=0xffff;};
        assert(!f.io.write32(p::R_AX_SYS_PW_CTRL,0x1234)&&f.map.get(p::R_AX_SYS_PW_CTRL)==0x1234);}
}
static void cleanup(){
    {Fixture f;model::effects=true;f.map.set(p::R_AX_SYS_PW_CTRL,p::B_AX_RDY_SYSPWR);
    f.map.set(p::R_AX_PMC_DBG_CTRL2,0x1000);model::owner=&f;
    model::afterWrite=[](void *p,unsigned address,uint32_t value){
        if(address==p::R_AX_PMC_DBG_CTRL2&&(value&p::B_AX_SYSON_DIS_PMCR_AX_WRMSK))static_cast<Fixture *>(p)->io.cancel();
    };
    p::DevicePower<p::MacDevicePowerIo> power(f.io);
    assert(!power.start(0)&&power.result.on.error==p::Error::cancelled&&f.io.cancelled());
    assert(power.result.on.pmcClosed&&power.result.requiresRecovery&&!power.result.powered);
    assert(f.map.get(p::R_AX_PMC_DBG_CTRL2)==0x1000);
    bool opened=false,closed=false;for(const auto &w:model::writes)if(w.address==p::R_AX_PMC_DBG_CTRL2){
        if(w.value&p::B_AX_SYSON_DIS_PMCR_AX_WRMSK)opened=true;
        else {assert(opened);closed=true;}
    }
    assert(opened&&closed);}
    // An already-open PMC gate belongs to somebody else. Do not close it.
    {Fixture f;f.map.set(p::R_AX_PMC_DBG_CTRL2,0x1004);p::DevicePower<p::MacDevicePowerIo> power(f.io);
        assert(!power.start(0)&&power.result.on.error==p::Error::precondition);
        assert(model::writes.empty()&&f.map.get(p::R_AX_PMC_DBG_CTRL2)==0x1004);}
    // Cleanup cannot bypass loss of DMA ownership even when this object opened
    // the PMC mask. Leave the hardware state marked as requiring recovery.
    {Fixture f;model::effects=true;f.map.set(p::R_AX_SYS_PW_CTRL,p::B_AX_RDY_SYSPWR);model::owner=&f;
        model::afterWrite=[](void *p,unsigned address,uint32_t value){
            if(address==p::R_AX_PMC_DBG_CTRL2&&(value&p::B_AX_SYSON_DIS_PMCR_AX_WRMSK))static_cast<Fixture *>(p)->device.command=6;
        };
        p::DevicePower<p::MacDevicePowerIo> power(f.io);
        assert(!power.start(0)&&!power.result.on.pmcClosed&&power.result.requiresRecovery);
        assert(f.map.get(p::R_AX_PMC_DBG_CTRL2)&p::B_AX_SYSON_DIS_PMCR_AX_WRMSK);}
}
static void binding(){
    Fixture f;p::MacDevicePowerIo noDevice(nullptr,&f.map,&f.loop),noMap(&f.device,nullptr,&f.loop),noLoop(&f.device,&f.map,nullptr);
    assert(!noDevice.valid()&&!noMap.valid()&&!noLoop.valid()&&noDevice.command()==0xffff);
    f.device.vendor=0;p::MacDevicePowerIo wrongVendor(&f.device,&f.map,&f.loop);assert(!wrongVendor.valid());
    f.device.vendor=0x10ec;f.device.device=0xffff;p::MacDevicePowerIo wrongChip(&f.device,&f.map,&f.loop);assert(!wrongChip.valid());
    f.device.device=0xb852;f.map.physical++;p::MacDevicePowerIo alias(&f.device,&f.map,&f.loop);assert(!alias.valid());
    f.map.physical--;f.map.length--;p::MacDevicePowerIo partial(&f.device,&f.map,&f.loop);assert(!partial.valid());
    f.map.length=f.device.bar.length=0xfffc;p::MacDevicePowerIo shortMap(&f.device,&f.map,&f.loop);assert(!shortMap.valid());
}
int main(){widths();serialCommands();accessAndCancellation();cleanup();binding();
    std::puts("PASS: actual native power source, widths/neighbors, register/SI whitelist without OTP, PCI removal/BM/gate/BAR guards, post-write loss and cancellation with owned PMC cleanup; modeled hardware");}
