// SPDX-License-Identifier: BSD-3-Clause
// RTL8852B SCC/DLFW setup and CPU sequence from pinned rtw89 mac.c/fw.c,
// d1fced1b8a741dc9f92b47c69489c24385945f6e, Realtek BSD option.
#pragma once
#include "../FirmwareTransfer.hpp"
namespace rtl8852be { namespace firmwareboot {
enum class Error {none,precondition,io,readback,timeout,clock,cancelled,download};
enum class Stage {idle,dmac,cpu,ready,stopped,fault};
struct Result {Error error{};Stage stage{};uint32_t address{},wanted{},actual{};
    unsigned operations{},writes{},polls{};bool modified{},requiresRecovery{},cpuRunning{},downloadReleased{};};
inline bool prepAddress(uint32_t a,unsigned width,bool write){
    if(width==2)return a==0x1e6;
    if(width!=4)return false;
    if(a>=0x9040&&a<=0x9068&&!(a&3))return true;
    switch(a){case 8:case 0x88:case 0x160:case 0x164:case 0x168:case 0x16c:case 0x1e0:
    case 0x1f4:case 0x1f8:case 0xc00:case 0x8380:case 0x8400:case 0x8404:
    case 0x8a00:case 0x8a04:case 0x8c08:case 0x9008:case 0x8c40:case 0x8c44:case 0x8c4c:case 0x8c50:return true;
    case 0xf0:case 0x3f0:case 0x1000:case 0x1010:case 0x101c:case 0x1a0:case 0x10b0:case 0x13b0:
    case 0x8d00:case 0x9100:return !write;default:return false;}
}
// This owns the chip programming sequence, not host DMA memory or callbacks.
// Successful download handoff keeps WCPU/clock running; shutdown is explicit.
template<class Io> class Preparation {
    Io &io_;uint64_t start_{},last_{};bool clockStarted_{},cleanup_{};
    bool fail(Error e,uint32_t a=0,uint32_t wanted=0,uint32_t got=0){
        if(result.error==Error::none){result.error=e;result.address=a;result.wanted=wanted;result.actual=got;}
        result.stage=Stage::fault;result.requiresRecovery|=result.modified;return false;
    }
    bool check(){
        if(result.error!=Error::none)return false;
        if(!io_.inGate())return fail(Error::precondition);
        const auto cmd=io_.command();if(cmd==0xffff||(cmd&6)!=2)return fail(Error::precondition,4,2,cmd);
        if(!cleanup_&&io_.cancelled())return fail(Error::cancelled);
        const auto now=io_.nowUs();if(!clockStarted_){start_=last_=now;clockStarted_=true;}
        if(now<last_)return fail(Error::clock);last_=now;
        return (now-start_<1000000&&result.operations<30000)||fail(Error::timeout);
    }
    bool read(uint32_t a,uint32_t &v,unsigned width=4){
        v=0;if(!check())return false;++result.operations;bool ok=false;
        if(width==2){uint16_t h=0;ok=io_.read16(a,h);v=h;}else ok=io_.read32(a,v);
        if(!ok||(width==4&&(v==0xffffffff||v==0xdeadbeef)))return fail(Error::io,a,0,v);return check();
    }
    bool equals(uint32_t a,uint32_t mask,uint32_t value){uint32_t got=0;
        return read(a,got)&&((got&mask)==value||fail(Error::readback,a,value,got&mask));}
    bool write(uint32_t a,uint32_t v,unsigned width=4,uint32_t verify=0xffffffff){
        if(!check())return false;++result.operations;++result.writes;result.modified=true;
        if(!(width==2?io_.write16(a,uint16_t(v)):io_.write32(a,v)))return fail(Error::io,a,v);
        if(!check())return false;uint32_t got=0;
        if(verify&&(!read(a,got,width)||((got^v)&verify)))return result.error!=Error::none?false:fail(Error::readback,a,v&verify,got&verify);
        return true;
    }
    bool update(uint32_t a,uint32_t mask,uint32_t value,unsigned width=4,uint32_t verify=0xffffffff){
        uint32_t old=0;return read(a,old,width)&&write(a,(old&~mask)|(value&mask),width,mask&verify);
    }
    bool poll(uint32_t a,uint32_t mask,uint32_t value,unsigned timeout){
        if(!check())return false;const auto begin=last_;
        for(unsigned i=0;i<=timeout/50;++i){uint32_t got=0;if(!read(a,got))return false;++result.polls;
            if(last_-begin>=timeout)return fail(Error::timeout,a,value,got&mask);
            if((got&mask)==value)return true;
            if(i==timeout/50)break;if(!io_.delayUs(50))return fail(Error::io,a);
        }return fail(Error::timeout,a,value);
    }
    bool stoppedDma(){return equals(0x1000,0x2800,0)&&equals(0x101c,0x007f0f03,0)&&
        equals(0x1a0,0xffffffff,0)&&equals(0x10b0,0xffffffff,0)&&equals(0x13b0,0xffffffff,0);}
    bool disableCpu(){
        // Source's 8852B watchdog reset uses APB wrapper, not a CPU-local write.
        return update(0x88,2,0)&&update(0x1e0,7,0,4,~0xe0u)&&update(8,0x4000,0)&&
            update(0x88,4,0)&&update(0x88,4,4)&&update(0x88,1,0)&&update(0x88,1,1)&&
            equals(0x88,2,0)&&equals(8,0x4000,0);
    }
    void budget(){clockStarted_=false;result.operations=0;}
public:
    Result result{};
    explicit Preparation(Io &io):io_(io){}
    Preparation(const Preparation&)=delete;Preparation &operator=(const Preparation&)=delete;
    bool prepareDmac(){
        if(result.stage!=Stage::idle)return false;budget();
        if(!check()||!equals(0x3f0,0x300,0x100)||!stoppedDma())return false;
        // Local HCI engines only: host RXHCI/TXHCI and PCI BM stay disabled.
        if(!update(0x8380,3,3)||!write(0x8400,0x60440000)||!write(0x8404,0x40000)||
           !update(0x8400,0x04800000,0)||!update(0x8404,0x04800000,0x04800000)||
           !update(0x8c08,0x1fff3f03,0)||!update(0x9008,0x1fff3f03,0x00400801)||
           !write(0x8c40,0)||!write(0x8c44,48)||!write(0x8c4c,0)||!write(0x8c50,0))return false;
        // DLFW wde_size9/ple_size8/wde_qt4/ple_qt13; WCPU minimum inherits
        // SCC wde_qt7=48 exactly as rtw89_mac_dle_init(QTA_DLFW, QTA_SCC).
        for(unsigned i=0;i<11;++i){const uint32_t q=i==2?16:i==3?48:0;
            if(!write(0x9040+4*i,q|(q<<16)))return false;}
        if(!update(0x8400,0x04800000,0x04800000)||!poll(0x8d00,3,3,2000)||!poll(0x9100,3,3,2000)||
           !update(0x8a00,9,0)||!write(0x8a04,40u<<16)||!update(0x8a00,0xc00,0)||!update(0x8a00,9,8))return false;
        result.stage=Stage::dmac;return true;
    }
    // Call after PCI link/pre-init configuration. PCI download ring preparation
    // still owns its own memory, BDRAM reset and host DMA transition.
    bool enableCpuForDownload(){
        if(result.stage!=Stage::dmac)return false;budget();
        if(!check()||!stoppedDma()||!disableCpu())return false;
        const uint32_t clears[]={0x1f4,0x1f8,0x160,0x164,0x168,0x16c};
        for(auto a:clears)if(!write(a,0))return false;
        if(!update(8,0x4000,0x4000)||!update(0x1e0,0xe7,1)||!update(0xc00,0x30000,0x20000)||
           !update(0x1e6,7,0,2)||!equals(0x1e0,0xe7,1)||!update(0x88,2,2)||
           !poll(0x1e0,2,2,400000))return false;
        uint32_t fw=0;if(!read(0x1e0,fw))return false;
        const auto state=(fw>>5)&7;if(state>=2)return fail(Error::download,0x1e0,0,fw);
        result.stage=Stage::cpu;return true;
    }
    bool acceptDownload(const transport::TransferResult &download){
        if(result.stage!=Stage::cpu)return false;budget();
        if(download.status!=transport::TransferStatus::complete||!download.quiesced||!download.buffersReleased||download.retainBuffers)
            return fail(Error::download);
        if(!check()||!stoppedDma()||!equals(0x1e0,0xe0,0xe0)||!equals(0x88,2,2)||!equals(8,0x4000,0x4000))return false;
        result.downloadReleased=true;result.cpuRunning=true;result.stage=Stage::ready;return true;
    }
    // Caller first drains/stops every DMA/IRQ owner. This never releases memory.
    bool stopCpu(){
        if(!result.modified||result.stage==Stage::stopped)return false;
        const Result first=result;result.error=Error::none;cleanup_=true;budget();io_.beginCleanup();
        const bool ok=check()&&stoppedDma()&&disableCpu();io_.endCleanup();cleanup_=false;
        if(ok){result.stage=Stage::stopped;result.cpuRunning=false;result.requiresRecovery=false;}
        else result.requiresRecovery=true;
        if(first.error!=Error::none){result.error=first.error;result.address=first.address;result.wanted=first.wanted;result.actual=first.actual;}
        return ok;
    }
};
} }
