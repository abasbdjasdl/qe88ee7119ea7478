// SPDX-License-Identifier: BSD-3-Clause
// RTL8852BE AX DMA/interrupt registers from pinned rtw89 pci.c/pci.h (BSD option).
#pragma once
#include "PciRingSetup.hpp"
namespace rtl8852be { namespace network {
constexpr uint32_t irqMasks[3]={0x1a0,0x10b0,0x13b0};
constexpr uint32_t irqStatus[3]={0x1a4,0x10b4,0x13b4};
constexpr uint32_t irqEnabled[3]={0x00200000,0x011e0007,0x10000000};
constexpr uint32_t runtimeStopMask=0x001f0f00,runtimeBusyMask=0x007f0f03;
struct InterruptStatus {
    uint32_t halt{},dma{},indirect{};bool valid{};
    bool pending()const{return valid&&(halt||dma||indirect);}
    bool fatal()const{return (halt&0x00200000)||(dma&0x00060000);}
};
struct RuntimeResult {
    uint32_t failureAddress{},expected{},actual{},lastBusy{};
    unsigned polls{};bool irqMasked{},masterOff{},idle{},stopped{};
};
// Serialized on the controller workloop. No allocations or protocol callbacks.
// Owner must pin/mark ALL nine banks device-visible before start. These methods
// do not assert MAC/RFK completion: the caller must actually initialize those
// blocks. Register preconditions below additionally reject obviously stale state.
template<class D> class PciRuntime {
    D &d_;bool attempted_{},running_{},faulted_{};uint16_t host_[9]{};uint64_t addresses_[9]{};
    bool bad(uint32_t v)const{return v==0xffffffff||v==0xdeadbeef;}
    bool fail(uint32_t a,uint32_t expected,uint32_t actual){
        faulted_=true;if(!result.failureAddress){result.failureAddress=a;result.expected=expected;result.actual=actual;}return false;
    }
    bool equal(uint32_t a,uint32_t mask,uint32_t expected){
        const auto v=d_.read32(a);return (!bad(v)&&(v&mask)==expected)||fail(a,expected,v);
    }
    bool write(uint32_t a,uint32_t value){return d_.write32(a,value)||fail(a,value,0xffffffff);}
    bool modify(uint32_t a,uint32_t mask,uint32_t value){
        const auto old=d_.read32(a);if(bad(old))return fail(a,value,old);
        return write(a,(old&~mask)|value)&&equal(a,mask,value);
    }
    bool command(uint16_t value){
        const bool wrote=d_.writeCommand(value);const auto actual=d_.command();
        return (wrote&&actual==value)||fail(4,value,actual);
    }
public:
    RuntimeResult result{};
    explicit PciRuntime(D &d):d_(d){}
    PciRuntime(const PciRuntime &)=delete;PciRuntime &operator=(const PciRuntime &)=delete;
    bool running()const{return running_&&!faulted_;}
    bool faulted()const{return faulted_;}
    bool ownsRing(unsigned ring,uint64_t address)const{return running()&&ring<9&&addresses_[ring]==address;}
    bool maskInterrupts(){
        bool ok=true;for(auto a:irqMasks)ok=write(a,0)&&ok;
        for(auto a:irqMasks)ok=equal(a,0xffffffff,0)&&ok;
        result.irqMasked=ok;return ok;
    }
    bool enableInterrupts(){
        if(!running())return false;
        for(unsigned i=0;i<3;++i)if(!write(irqMasks[i],irqEnabled[i])||!equal(irqMasks[i],0xffffffff,irqEnabled[i])){
            maskInterrupts();return false;
        }
        result.irqMasked=false;return true;
    }
    InterruptStatus acknowledgeInterrupts(){
        InterruptStatus r;if(!running()||!result.irqMasked)return r;
        uint32_t values[3]{};
        for(unsigned i=0;i<3;++i){const auto v=d_.read32(irqStatus[i]);
            if(bad(v)){fail(irqStatus[i],0,v);return r;}values[i]=v&irqEnabled[i];
        }
        // W1C only observed, enabled causes. Do not read-modify-write status.
        for(unsigned i=0;i<3;++i)if(values[i]&&!write(irqStatus[i],values[i]))return r;
        r.halt=values[0];r.dma=values[1];r.indirect=values[2];r.valid=true;
        return r;
    }
    bool start(const RingMemory (&rings)[9]){
        if(attempted_||faulted_)return false;attempted_=true;
        if((d_.command()&6)!=2)return fail(4,2,d_.command());
        if(!maskInterrupts()||!equal(0x1000,0x2808,0)||
           !equal(0x1010,runtimeStopMask,runtimeStopMask)||!equal(0x101c,runtimeBusyMask,0)||
           !equal(0x8380,3,3)||!equal(0x8400,0x60000000,0x60000000)||
           !equal(0xc000,0x40000000,0x40000000)||!equal(0x1e0,0xe0,0xe0))return false;
        for(unsigned i=0;i<9;++i){const auto &m=rings[i];const auto &r=ringRegisters[i];
            if(m.count!=64||(m.address&7)||!dma32Range(m.address,512))return fail(r.low,0,uint32_t(m.address));
            for(unsigned j=0;j<i;++j)if(m.address<rings[j].address+512&&rings[j].address<m.address+512)return fail(r.low,0,uint32_t(m.address));
            if(!equal(r.low,0xffffffff,uint32_t(m.address))||!equal(r.high,0xffffffff,0)||
               (d_.read16(r.count)&0xfff)!=64||!equal(r.index,0x0fff0fff,0))return fail(r.count,64,d_.read16(r.count));
            addresses_[i]=m.address;
        }
        // No outstanding frames in a fresh epoch. Clear old enabled causes
        // before starting, never when rearming after runtime packet processing.
        for(unsigned i=0;i<3;++i){const auto value=d_.read32(irqStatus[i]);
            if(bad(value))return fail(irqStatus[i],0,value);
            if((value&irqEnabled[i])&&!write(irqStatus[i],value&irqEnabled[i]))return false;
        }
        d_.barrier();
        // INTx stays disabled; the native owner uses a verified MSI source.
        const auto cmd=d_.command();if(cmd==0xffff)return fail(4,2,cmd);
        if(!command(uint16_t(cmd|0x404)))return false;
        result.masterOff=false;result.stopped=false;
        if(!modify(0x1010,runtimeStopMask,0)||!modify(0x1000,0x2800,0x2800))return false;
        running_=true;return true; // Owner enables the event source, then IRQs.
    }
    bool readIndices(unsigned ring,uint16_t &host,uint16_t &hardware){
        if(!running()||ring>=9)return false;const auto a=ringRegisters[ring].index,v=d_.read32(a);
        host=uint16_t(v&0xfff);hardware=uint16_t((v>>16)&0xfff);
        if(bad(v)||host>=64||hardware>=64||host!=host_[ring])return fail(a,host_[ring],v);
        return true;
    }
    bool publish(unsigned ring,uint16_t next){
        if(!running()||ring>=9||next>=64)return false;
        uint16_t host=0,hardware=0;if(!readIndices(ring,host,hardware))return false;
        const unsigned advance=(next+64-host)%64;
        if(ring<7){
            // TX staging commits one descriptor. Keep one empty slot.
            if(advance!=1||next==hardware)return fail(ringRegisters[ring].index,(host+1)%64,next);
        }else if(advance>(hardware+64-host)%64)return fail(ringRegisters[ring].index,hardware,next);
        if(!advance)return true;
        d_.barrier();const auto a=ringRegisters[ring].index;
        // write16 touches only the host half; never overwrite the HW index.
        if(!d_.write16(a,next))return fail(a,next,0xffffffff);
        host_[ring]=next;
        const auto value=d_.read32(a);
        return (!bad(value)&&(value&0xfff)==next)||fail(a,next,value);
    }
    // Event sources/timers must already be disabled and callbacks serialized.
    // A false return forbids releasing any DMA bank, even if BM is now off.
    bool stop(){
        running_=false;result.stopped=false;
        bool ok=maskInterrupts();
        const bool hci=modify(0x1000,0x2800,0);
        const bool channels=modify(0x1010,runtimeStopMask,runtimeStopMask);
        ok=hci&&channels&&ok;
        const uint64_t begin=d_.nowUs();uint64_t previous=begin;result.idle=false;
        for(unsigned i=0;i<201;++i){const auto now=d_.nowUs();
            if(now<previous||now-begin>=10000)break;previous=now;
            result.lastBusy=d_.read32(0x101c);++result.polls;
            if(bad(result.lastBusy))break;
            if(!(result.lastBusy&runtimeBusyMask)){result.idle=true;break;}d_.pauseUs(50);
        }
        // Cut off bus mastering even when stop writes or idle polling fail.
        const auto cmd=d_.command();result.masterOff=cmd!=0xffff&&command(uint16_t(cmd&~4u))&&!(d_.command()&4);
        ok=equal(0x1000,0x2800,0)&&equal(0x1010,runtimeStopMask,runtimeStopMask)&&ok;
        result.stopped=ok&&result.idle&&result.masterOff&&result.irqMasked;
        if(!result.stopped)faulted_=true;return result.stopped;
    }
};
} }
