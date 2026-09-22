// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2020-2022 Realtek Corporation (upstream BSD option)
// 8852B-specific pci.c pre/post-init and DBI/MDIO, fixed rtw89 revision
// d1fced1b8a741dc9f92b47c69489c24385945f6e; values from pci.h/reg.h/rtw8852be.c.
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace rtl8852be { namespace network { namespace pcilink {
constexpr uint32_t mdioConfig=0x10a0,mdioWriteData=0x10a4,mdioReadData=0x10a6;
constexpr uint32_t dbiFlag=0x1090,dbiWriteData=0x1094,dbiReadData=0x1098;
enum class Error {none,invalid,ownership,cancelled,clock,timeout,io,readback,mdioTimeout,dbiTimeout,linkSpeed};
struct Result {
    Error error{Error::none};uint32_t address{},value{};unsigned operations{},polls{},dbiFallbacks{};
    uint8_t cut{},linkSpeed{};bool preConfigured{},postConfigured{},modified{},requiresRecovery{};
    bool l1RestoreAttempted{},l1Restored{},l1RestoreFailed{};
};
// Io: inGate/cancelled/nowUs/delayUs, boolean read/write8/16/32,
// readConfig8/writeConfig8 for full 12-bit PCI config offsets. False config
// access falls back to the real AX DBI engine. Caller owns device/PHY access;
// no allocation, bus-master, IRQ, DMA, ring, or BDRAM ownership is changed here.
template<class Io> class Initialization {
    Io &io_;bool busy_{},cleanup_{},clockStarted_{};uint64_t phaseStart_{},last_{};unsigned phaseOperations_{};
    bool l1Changed_{};uint8_t savedL1_{};
    bool fail(Error e,uint32_t a=0,uint32_t v=0){
        if(result.error==Error::none){result.error=e;result.address=a;result.value=v;}
        result.requiresRecovery=result.modified;return false;
    }
    bool check(){
        if(result.error!=Error::none&&!cleanup_)return false;
        if(!io_.inGate())return fail(Error::ownership);
        if(io_.cancelled())return fail(Error::cancelled);
        const auto now=io_.nowUs();if(clockStarted_&&now<last_)return fail(Error::clock);
        clockStarted_=true;last_=now;
        if(last_<phaseStart_)return fail(Error::clock);
        if(last_-phaseStart_>=(cleanup_?5000U:100000U)||phaseOperations_>=20000)return fail(Error::timeout);
        return true;
    }
    bool enter(){
        if(busy_)return fail(Error::ownership);
        if(result.error!=Error::none)return false;
        const auto now=io_.nowUs();if(clockStarted_&&now<last_)return fail(Error::clock);
        phaseStart_=now;phaseOperations_=0;if(!check())return false;busy_=true;return true;
    }
    struct Guard {bool &busy;~Guard(){busy=false;}};
    void operation(){++result.operations;++phaseOperations_;}
    bool r8(uint32_t a,uint8_t &v){v=0;if(!check())return false;operation();return (io_.read8(a,v)||fail(Error::io,a))&&check();}
    bool r16(uint32_t a,uint16_t &v){v=0;if(!check())return false;operation();return (io_.read16(a,v)||fail(Error::io,a))&&check();}
    bool r32(uint32_t a,uint32_t &v){v=0;if(!check())return false;operation();
        if(!io_.read32(a,v))return fail(Error::io,a);
        if(v==0xffffffff||v==0xeaeaeaea||v==0xdeadbeef)return fail(Error::io,a,v);
        return check();}
    bool w8(uint32_t a,uint8_t v){if(!check())return false;operation();result.modified=true;
        return (io_.write8(a,v)||fail(Error::io,a,v))&&check();}
    bool w16(uint32_t a,uint16_t v){if(!check())return false;operation();result.modified=true;
        return (io_.write16(a,v)||fail(Error::io,a,v))&&check();}
    bool w32(uint32_t a,uint32_t v){if(!check())return false;operation();result.modified=true;
        return (io_.write32(a,v)||fail(Error::io,a,v))&&check();}
    bool delay(unsigned us){return check()&&(io_.delayUs(us)||fail(Error::io))&&check();}
    bool update32(uint32_t a,uint32_t clear,uint32_t set,uint32_t verify=0xffffffff){
        uint32_t v=0,got=0;if(!r32(a,v))return false;v=(v&~clear)|set;
        if(!w32(a,v)||!r32(a,got))return false;
        return !((got^v)&verify)||fail(Error::readback,a,got);
    }
    bool full32(uint32_t a,uint32_t v){uint32_t got=0;
        if(!w32(a,v)||!r32(a,got))return false;return got==v||fail(Error::readback,a,got);}
    bool mdioIdle(uint16_t mask){
        const auto start=last_;
        for(unsigned p=0;p<=200;++p){uint16_t v=0;if(!r16(mdioConfig,v))return false;++result.polls;
            if(last_-start>2000)break;if(!(v&mask))return true;
            if(last_-start>=2000||p==200)break;if(!delay(10))return false;}
        return fail(Error::mdioTimeout,mdioConfig,mask);
    }
    bool mdioTrigger(uint8_t address,uint8_t speed,uint16_t flag){
        // Gen1 pages 0/1; Gen2 pages 2/3. Do not truncate address 0x30 to
        // page 0 when clearing calibration on the negotiated PCIe generation.
        const uint16_t page=uint16_t((speed==2?2:0)+(address>=0x20?1:0));
        uint16_t v=0;if(!w8(mdioConfig,address&31)||!r16(mdioConfig,v))return false;
        v=uint16_t((v&~0x3000)|(page<<12));
        if(!w16(mdioConfig,v)||!r16(mdioConfig,v)||!w16(mdioConfig,uint16_t(v|flag)))return false;
        return mdioIdle(flag);
    }
    bool readMdio(uint8_t address,uint8_t speed,uint16_t &v){
        return mdioIdle(0x300)&&mdioTrigger(address,speed,0x200)&&r16(mdioReadData,v);
    }
    bool writeMdio(uint8_t address,uint8_t speed,uint16_t v){
        return mdioIdle(0x300)&&w16(mdioWriteData,v)&&mdioTrigger(address,speed,0x100);
    }
    bool updateMdio(uint8_t address,uint8_t speed,uint16_t mask,uint16_t bits){
        uint16_t v=0,got=0;if(!readMdio(address,speed,v))return false;
        const auto next=uint16_t((v&~mask)|(bits&mask));
        if(!writeMdio(address,speed,next)||!readMdio(address,speed,got))return false;
        return !((got^next)&mask)||fail(Error::readback,address,got);
    }
    bool dbiIdle(){
        const auto start=last_;
        for(unsigned p=0;p<=20;++p){uint8_t v=0;if(!r8(dbiFlag+2,v))return false;++result.polls;
            if(last_-start>200)break;if(!v)return true;
            if(last_-start>=200||p==20)break;if(!delay(10))return false;}
        return fail(Error::dbiTimeout,dbiFlag+2);
    }
    bool readDbi(uint16_t address,uint8_t &v){
        if(!dbiIdle()||!w16(dbiFlag,uint16_t(address&0xffc))||!w8(dbiFlag+2,2)||!dbiIdle())return false;
        return r8(dbiReadData+(address&3),v);
    }
    bool writeDbi(uint16_t address,uint8_t v){
        const auto lane=address&3;const auto flag=uint16_t((address&0xffc)|((1U<<lane)<<12));
        return dbiIdle()&&w8(dbiWriteData+lane,v)&&w16(dbiFlag,flag)&&w8(dbiFlag+2,1)&&dbiIdle();
    }
    bool readConfig(uint16_t address,uint8_t &v){
        v=0;if(!check())return false;operation();
        const bool native=io_.readConfig8(address,v);if(!check())return false;
        if(native)return true;++result.dbiFallbacks;return readDbi(address,v);
    }
    bool writeConfig(uint16_t address,uint8_t v){
        if(!check())return false;operation();result.modified=true;
        const bool native=io_.writeConfig8(address,v);if(!check())return false;
        if(!native){++result.dbiFallbacks;if(!writeDbi(address,v))return false;}
        uint8_t got=0;if(!readConfig(address,got))return false;
        return got==v||fail(Error::readback,address,got);
    }
    void restoreL1(){
        if(!l1Changed_)return;
        result.l1RestoreAttempted=true;
        // Preserve a primary MDIO error even if restoring L1 succeeds. Upstream
        // overwrites ret in its cleanup path; that cannot authorize our phase.
        const auto savedStart=phaseStart_;const auto savedOps=phaseOperations_;
        cleanup_=true;phaseStart_=last_;phaseOperations_=0;
        result.l1Restored=writeConfig(0x719,savedL1_);
        result.l1RestoreFailed=!result.l1Restored;
        if(result.l1Restored)l1Changed_=false;
        cleanup_=false;phaseStart_=savedStart;phaseOperations_=savedOps;
    }
    bool disableRefclkAutok(){
        uint8_t rate=0;if(!readConfig(0x82,rate))return false;
        result.linkSpeed=rate&3;if(result.linkSpeed!=1&&result.linkSpeed!=2)return fail(Error::linkSpeed,0x82,rate);
        if(!readConfig(0x719,savedL1_))return false;
        if(savedL1_&8){
            // A failed write may have reached config space: restoration remains
            // required even if its transport/readback reports an error.
            l1Changed_=true;if(!writeConfig(0x719,uint8_t(savedL1_&~8))){restoreL1();return false;}}
        const bool ok=updateMdio(0x30,result.linkSpeed,0x2000,0);
        restoreL1();return ok&&result.error==Error::none&&!result.l1RestoreFailed;
    }
public:
    Result result{};
    explicit Initialization(Io &io):io_(io){}
    Initialization(const Initialization &)=delete;Initialization &operator=(const Initialization &)=delete;
    bool preInit(uint8_t actualCut){
        if(result.preConfigured)return false;
        if(!enter())return false;Guard guard{busy_};
        if(actualCut>=6)return fail(Error::invalid);result.cut=actualCut;
        // All defined 8852B cuts share this sequence. The neighboring upstream
        // CAV overrides are explicitly RTL8852C-only, never applied to 8852B.
        if(!update32(0x1008,0x20,0)||!update32(4,0x4000,0)||
           !update32(0x70,0,0x8000)||!update32(0x70,0x4000,0)||
           !updateMdio(0x1b,1,0xf000,0x1000)||!updateMdio(0x1d,1,0x0c,0x0c)||
           !disableRefclkAutok()||!update32(0x74,0,0x20)||!update32(0x13f0,0x10,0))return false;
        uint32_t lbc=0;if(!r32(0x11d8,lbc))return false;
        lbc=(lbc&~0xf0)|0x80|3; // rtw8852be: LBC enabled, timer enum 8 = 2 ms.
        if(!w32(0x11d8,lbc)||!update32(0x11d8,0,lbc,~2U)||
           !update32(0x11c0,0,3)||!update32(0x1000,0,0xc00000))return false;
        result.preConfigured=true;return true;
    }
    bool postInit(){
        if(result.postConfigured)return false;
        if(!enter())return false;Guard guard{busy_};
        if(!result.preConfigured)return fail(Error::invalid);
        constexpr uint32_t ltr[]={0x8410,0x8414,0x8418,0x841c};
        uint32_t value=0;for(auto address:ltr)
            if(!r32(address,value))return false;
        if(!update32(0x8410,0,0x43)||!update32(0x8410,0x3000,0x2000)||
           !update32(0x8410,0x700,0x700)||!update32(0x8414,0xfff,0x28)||
           !update32(0x8414,0xfff0000,0x280000)||!full32(0x8418,0x90039003)||
           !full32(0x841c,0x880b880b)||!update32(0x8810,0,1)||!update32(0x9a00,2,0))return false;
        result.postConfigured=true;return true;
    }
};
} } }
