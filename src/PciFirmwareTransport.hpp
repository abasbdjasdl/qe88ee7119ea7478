// SPDX-License-Identifier: BSD-3-Clause
// Bounded 8852B CH12-only download transport. rtw89 pci.c/pci.h BSD option.
// Copyright(c) 2020-2022 Realtek Corporation
#pragma once
#include "FirmwareTransfer.hpp"
#include "DmaProbe.hpp"
namespace rtl8852be { namespace transport {
inline bool uploadAddress32(uint32_t a){
    switch(a){case 0x160:case 0x164:case 0x1a0:case 0x1000:case 0x1004:
    case 0x1010:case 0x1014:case 0x10b0:case 0x1160:case 0x1164:case 0x1228:
    case 0x13b0:case 0x13f0:case 0x8380:case 0x8810:case 0x9a00:return true;default:return false;}
}
inline bool uploadAddress16(uint32_t a){return a==0x1038||a==0x1080;}
struct PciDownloadResult {
    unsigned stage{},writes{},polls{},published{},failureAddress{},gate{};
    uint32_t failureExpected{},failureActual{},busy{},index{},init{},stop{},hci{};
    uint32_t pollFailureAddress{},pollFailureMask{},pollFailureExpected{},pollFailureActual{},resetHci{},resetControl{};
    unsigned pollFailureReason{}; // 1 deadline, 2 backwards clock, 3 invalid read, 4 iteration cap
    bool attempted{},busMasterEnabled{},doorbellAttempted{},idle{},busMasterOff{},restored{};
};
// D owns MMIO and PCI config; packet bank ownership remains with the caller.
// No RX ring or radio engine is enabled. All waits have clock + iteration caps.
template<class D> class PciDownload {
    D &d;dma::Mapping ring;unsigned packets;bool bankReady,snapshot{},started{};
    bool (*releaseHook)(void *);void *owner;
    uint32_t saved[14]{};uint16_t savedCount{};
    static constexpr uint32_t addresses[14]={0x1000,0x1004,0x1010,0x1160,0x1164,0x1228,0x13f0,0x8380,0x8810,0x9a00,0x1a0,0x10b0,0x13b0,0x101c};
    static bool invalid(uint32_t v){return v==0xffffffff||v==0xdeadbeef;}
    bool equal(uint32_t a,uint32_t mask,uint32_t expected){
        const auto v=d.read32(a);
        if(!invalid(v)&&(v&mask)==expected)return true;
        if(!result.failureAddress){result.failureAddress=a;result.failureExpected=expected;result.failureActual=v;}
        return false;
    }
    bool w(uint32_t a,uint32_t v){++result.writes;const bool ok=uploadAddress32(a)&&d.uploadWrite32(a,v);if(!ok&&!result.failureAddress){result.failureAddress=a;result.failureExpected=v;result.failureActual=d.read32(a);}return ok;}
    bool w16(uint32_t a,uint16_t v){++result.writes;const bool ok=uploadAddress16(a)&&d.uploadWrite16(a,v);if(!ok&&!result.failureAddress){result.failureAddress=a;result.failureExpected=v;result.failureActual=d.read16(a);}return ok;}
    bool poll(uint32_t a,uint32_t mask,uint32_t expected,unsigned timeout,uint32_t &v){
        const auto begin=d.nowUs();
        auto failed=[&](unsigned reason){
            if(!result.pollFailureReason){result.pollFailureReason=reason;result.pollFailureAddress=a;result.pollFailureMask=mask;result.pollFailureExpected=expected;result.pollFailureActual=v;}
            return false;
        };
        for(unsigned i=0;i<timeout/50+1;++i){
            const auto now=d.nowUs();if(now<begin)return failed(2);if(now-begin>=timeout)return failed(1);
            v=d.read32(a);++result.polls;if(invalid(v))return failed(3);
            const auto after=d.nowUs();if(after<now)return failed(2);if(after-begin>=timeout)return failed(1);
            if((v&mask)==expected)return true;d.pauseUs(50);
        }return failed(4);
    }
public:
    PciDownloadResult result{};
    PciDownload(D &device,const dma::Mapping &mapping,unsigned count,bool ready,bool (*release)(void *),void *context):d(device),ring(mapping),packets(count),bankReady(ready),releaseHook(release),owner(context){}
    uint64_t nowUs(){return d.nowUs();}
    bool cancelled(){return false;}
    void delayUs(unsigned us){d.pauseUs(us);}
    uint8_t readControl(){const auto v=d.read32(0x1e0);return invalid(v)?0xff:static_cast<uint8_t>(v);}
    uint32_t readIndex(){return d.read32(0x1080);}
    bool prepare(const Packets &p){return bankReady&&dma::validMapping(ring)&&packets==p.count()&&packets>1&&packets<256&&(d.command()&6)==2;}
    bool startDownload(){
        result.attempted=true;result.stage=1;
        if((d.command()&6)!=2){result.gate=1;return false;}
        if(!d.interruptsSafe()){result.gate=2;return false;}
        for(unsigned i=0;i<14;++i){saved[i]=d.read32(addresses[i]);if(invalid(saved[i])){result.gate=3;result.failureAddress=addresses[i];result.failureActual=saved[i];return false;}}
        savedCount=d.read16(0x1038);
        // Do not take over a pre-existing live queue or a partially restored one.
        if(savedCount==0xffff||saved[3]||saved[4]||(readIndex()&0x0fff0fff)||saved[13]&0x7f0f03){result.gate=4;return false;}
        snapshot=true;
        const auto init=(saved[0]&~(0x2800u|0x40000u|0x1c000u|0x700u|8u))|0x1000u|0x700u|0xc000u;
        const auto stop=saved[2]|0x1f0f00u;
        if(!w(0x1000,init)||!w(0x1010,stop)||!poll(0x101c,0x7f0f03,0,10000,result.busy))return false;
        if(!equal(0x1000,0x2800,0)||!equal(0x1010,0x1f0f00,0x1f0f00))return false;
        result.stage=2;
        // Polling-only transaction: mask chip IRQs before any bus mastering.
        if(!w(0x1a0,0)||!w(0x10b0,0)||!w(0x13b0,0))return false;
        if(!equal(0x1a0,0xffffffff,0)||!equal(0x10b0,0xffffffff,0)||!equal(0x13b0,0xffffffff,0))return false;
        if(!w(0x1004,(saved[1]&~0x0f0f0000u)|0x01010000)||!w(0x13f0,(saved[6]&~0x70000u)|0x70000))return false;
        if(!w(0x8810,saved[8]|1)||!w(0x9a00,saved[9]&~2u))return false;
        if(!equal(0x1000,0x5ff08,init&0x5ff08)||!equal(0x1004,0x0f0f0000,0x01010000)||
           !equal(0x13f0,0x70000,0x70000)||!equal(0x8810,1,1)||!equal(0x9a00,2,0))return false;
        if(!w(0x1164,0)||!w(0x1160,static_cast<uint32_t>(ring.address))||!w16(0x1038,(savedCount&0xf000)|256)||
           !w(0x1228,(saved[5]&~0x00ffffffu)|0x01041c))return false;
        if(!equal(0x1160,0xffffffff,static_cast<uint32_t>(ring.address))||!equal(0x1164,0xffffffff,0)||
           (d.read16(0x1038)&0xfff)!=256||!equal(0x1228,0xffffff,0x01041c))return false;
        result.stage=3;
        // rtw89 mac_partial_init enables both internal HCI engines before
        // pci_mac_pre_init resets BDRAM. These are local engine gates, not
        // permission to access host memory: BM and TXHCI/RXHCI remain off,
        // all implemented queues and PCI IO remain stopped until reset ends.
        if(!w(0x8380,(saved[7]&~3u)|3)||!equal(0x8380,3,3))return false;
        result.resetHci=d.read32(0x8380);
        if(!w(0x1014,0x400)||!equal(0x1080,0x0fff0fff,0))return false;
        if(!w(0x1000,init|8)||!poll(0x1000,8,0,10000,result.resetControl))return false;
        // Only TX is needed after reset; close the internal RX gate before BM.
        if(!w(0x8380,(saved[7]&~3u)|1)||!equal(0x8380,3,1))return false;
        // Only CH12 can run. WPDMA + every other implemented TX channel stop;
        // RXHCI and HCI_RXDMA stay disabled. Producer is still zero.
        const auto runningStop=stop&~(0x100000u|0x40000u);
        if(!w(0x1010,runningStop)||!equal(0x1010,0x1f0f00,runningStop&0x1f0f00))return false;
        if(!equal(0x1000,0x2800,0)||!equal(0x1080,0x0fff0fff,0))return false;
        result.stage=4;started=true;
        if(!d.uploadBusMaster(true))return false;
        result.busMasterEnabled=true;
        if(!w(0x1000,init|0x800)||!equal(0x1000,0x2800,0x800))return false;
        result.init=d.read32(0x1000);result.stop=d.read32(0x1010);result.hci=d.read32(0x8380);
        return true;
    }
    bool publish(unsigned producer){
        if(!started||(d.command()&6)!=6||producer!=result.published+1||producer>packets)return false;
        result.doorbellAttempted=true;
        d.uploadBarrier();
        if(!w16(0x1080,static_cast<uint16_t>(producer)))return false;
        result.index=readIndex();
        if(invalid(result.index)||(result.index&0xfff)!=producer||((result.index>>16)&0xfff)>producer)return false;
        result.published=producer;return true;
    }
    bool clearHaltControls(){return w(0x160,0)&&w(0x164,0);}
    bool quiesceAndProveIdle(){
        bool ok=true;
        if(snapshot){
            const auto init=d.read32(0x1000),stop=d.read32(0x1010),hci=d.read32(0x8380);
            const bool a=!invalid(init)&&w(0x1000,init&~0x2800u);
            const bool b=!invalid(stop)&&w(0x1010,stop|0x1f0f00);
            const bool c=!invalid(hci)&&w(0x8380,hci&~3u);ok=a&&b&&c;
            result.idle=poll(0x101c,0x7f0f03,0,10000,result.busy)&&equal(0x1000,0x2800,0)&&equal(0x8380,3,0);
        }else result.idle=!started;
        // Always attempt to cut off DMA even after a failed stop or timeout.
        result.busMasterOff=d.uploadBusMaster(false)&&(d.command()&6)==2;
        if(!result.idle||!result.busMasterOff)return false;
        if(snapshot){
            // WCPU is now running and may have changed the interrupt masks
            // since download entry. Re-establish the caller's masked handoff
            // after DMA idle/BM-off; never assume the entry writes persisted.
            for(unsigned i=10;i<=12;++i){const bool masked=w(addresses[i],0);ok=masked&&ok;}
            for(unsigned i=10;i<=12;++i)ok=equal(addresses[i],0xffffffff,0)&&ok;
            // Indices have no prior outstanding ownership (preflight required
            // zero). Restore addresses only after idle and bus-master-off proof.
            const bool clear=w(0x1014,0x400)&&equal(0x1080,0x0fff0fff,0);ok=clear&&ok;
            for(unsigned i=1;i<=9;++i){const bool restored=w(addresses[i],saved[i]);ok=restored&&ok;}
            const bool n=w16(0x1038,savedCount);ok=n&&ok;
            const bool init=w(0x1000,saved[0]);ok=init&&ok;
            for(unsigned i=0;i<=9;++i)ok=equal(addresses[i],0xffffffff,saved[i])&&ok;
            ok=(d.read16(0x1038)==savedCount)&&ok;
            // Caller decides whether to initialize runtime or stop WCPU.
        }
        result.restored=ok;return ok;
    }
    bool releaseAll(){return (!started||(result.idle&&result.busMasterOff))&&releaseHook&&releaseHook(owner);}
};
template<class D> constexpr uint32_t PciDownload<D>::addresses[14];
} }
