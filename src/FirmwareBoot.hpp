// SPDX-License-Identifier: BSD-3-Clause
// RTL8852B DLFW DLE/HFC and WCPU ordering from rtw89 mac.c/rtw8852b.c.
// Copyright(c) 2019-2022 Realtek Corporation
#pragma once
#include <stdint.h>
namespace rtl8852be { namespace boot {
enum class Status {notRun,unsafeCommand,invalidRead,writeFailed,timeout,staleReady,romError,ready,cleanupFailed};
inline const char *statusName(Status s){switch(s){
case Status::notRun:return "NOT_RUN";case Status::unsafeCommand:return "BOOT_UNSAFE_COMMAND";
case Status::invalidRead:return "BOOT_INVALID_READ";case Status::writeFailed:return "BOOT_WRITE_FAILED";
case Status::timeout:return "BOOT_TIMEOUT";case Status::staleReady:return "BOOT_STALE_READY";case Status::romError:return "ROM_REPORTED_ERROR";
case Status::ready:return "ROM_H2C_READY";case Status::cleanupFailed:return "BOOT_CLEANUP_FAILED";}return "UNKNOWN";}
inline bool allowed32(uint32_t a){
    if(a>=0x9040&&a<=0x9068&&(a&3)==0)return true;
    switch(a){case 8:case 0x88:case 0x160:case 0x164:case 0x168:case 0x16c:case 0x1e0:
    case 0x1f4:case 0x1f8:case 0xc00:case 0x1000:case 0x1010:case 0x8400:case 0x8404:
    case 0x8a00:case 0x8a04:case 0x8c08:case 0x9008:case 0x8c40:case 0x8c44:case 0x8c4c:case 0x8c50:return true;default:return false;}
}
inline bool invalid(uint32_t v){return v==0xffffffff||v==0xdeadbeef;}
// 8852B uses B_AX_TX_STOP1_MASK_V1: ACH4..7 do not exist on this chip.
// RX is stopped by RXHCI_EN above; rtw8852be does not use STOP_RXQ/RPQ.
constexpr uint32_t txChannels=0x00070f00,stopChannels=txChannels;
constexpr uint32_t busyChannels=txChannels|0x00780003;
struct Result {Status status{Status::notRun},operationStatus{Status::notRun};unsigned phase{},writes{},polls{};uint32_t wde{},ple{},control{},dmac{},clock{},wdeConfig{},pleConfig{},hfcControl{},hfcPages{};
    uint32_t hciBefore{},stopBefore{},hciPaused{},stopPaused{},hciAfter{},stopAfter{},cleanupFailures{};
    uint32_t cleanupClock{},cleanupDmac{};bool cleanupClockChecked{};
    uint32_t initialDmac{},initialHci{},accessDmac{},accessClock{};
    bool accessAttempted{},accessReady{},hciCaptured{};
    uint32_t failureAddress{},failureMask{},failureExpected{},failureActual{};
    bool failureRecorded{},attempted{},cleanupOK{},cpuStopped{};};
template<class D,class Action> Result probeWithAction(D &d,Action action){
    Result r;if((d.command()&6)!=2){r.status=Status::unsafeCommand;return r;}
    uint32_t hci=0,stop=0;const auto sec=d.read32(0xc00);
    const auto reason=d.read16(0x1e6);
    r.initialDmac=d.read32(0x8400);r.initialHci=d.read32(0x1000);
    if(invalid(r.initialDmac)||invalid(sec)||reason==0xffff){r.status=Status::invalidRead;return r;}
    auto matches=[&](uint32_t address,uint32_t value,uint32_t mask,uint32_t expected){
        if(!invalid(value)&&(value&mask)==expected)return true;
        if(!r.failureRecorded){r.failureRecorded=true;r.failureAddress=address;r.failureMask=mask;r.failureExpected=expected;r.failureActual=value;}
        return false;
    };
    auto wr=[&](uint32_t a,uint32_t v){++r.writes;if(!d.bootWrite32(a,v)){r.status=Status::writeFailed;return false;}return true;};
    auto rm=[&](uint32_t a,uint32_t mask,uint32_t value){const auto old=d.read32(a);if(invalid(old)){r.status=Status::invalidRead;return false;}return wr(a,(old&~mask)|value);};
    auto poll=[&](uint32_t a,uint32_t mask,uint32_t wanted,unsigned timeout,uint32_t &last){
        const auto start=d.nowUs();
        for(unsigned i=0;i<timeout/50+1;++i){
            const auto now=d.nowUs();if(now<start||now-start>=timeout){r.status=Status::timeout;return false;}
            last=d.read32(a);++r.polls;if(invalid(last)){r.status=Status::invalidRead;return false;}
            if((last&mask)==wanted)return true;d.pauseUs(50);
        }r.status=Status::timeout;return false;
    };
    r.attempted=true;
    do{
        // Supply-only power-on deliberately omitted the reference func_en tail.
        // Open the minimal HCI register window before manipulating PCI HCI,
        // as mac_dmac_pre_init precedes PCI mac_pre_init in rtw89. BM stays off.
        r.accessAttempted=true;
        if(!wr(0x8400,0x60440000))break;
        r.accessDmac=d.read32(0x8400);
        if(!matches(0x8400,r.accessDmac,0xffffffff,0x60440000)){r.status=Status::writeFailed;break;}
        if(!wr(0x8404,0x00040000))break;
        r.accessClock=d.read32(0x8404);
        if(!matches(0x8404,r.accessClock,0xffffffff,0x00040000)){r.status=Status::writeFailed;break;}
        r.accessReady=true;
        hci=d.read32(0x1000);stop=d.read32(0x1010);
        if(invalid(hci)||invalid(stop)){r.status=Status::invalidRead;break;}
        r.hciBefore=hci;r.stopBefore=stop;r.hciCaptured=true;
        r.phase=1;
        if(!wr(0x1000,hci&~0x2800u)||!wr(0x1010,stop|stopChannels))break;
        uint32_t idle=0;if(!poll(0x101c,busyChannels,0,2000,idle))break;
        r.hciPaused=d.read32(0x1000);r.stopPaused=d.read32(0x1010);
        if(!matches(0x1000,r.hciPaused,0x2800,0)||!matches(0x1010,r.stopPaused,stopChannels,stopChannels)){r.status=Status::writeFailed;break;}
        r.phase=2;
        // DLFW layout: 64 KiB WDE (0 linked pages), 128 KiB PLE (64 linked).
        if(!rm(0x8400,0x04800000,0)||!rm(0x8404,0x04800000,0x04800000))break;
        if(!rm(0x8c08,0x1fff3f03,0)||!rm(0x9008,0x1fff3f03,0x00400801))break;
        // WCPU min quota is inherited from the SCC configuration (48).
        if(!wr(0x8c40,0)||!wr(0x8c44,48)||!wr(0x8c4c,0)||!wr(0x8c50,0))break;
        bool quota=true;for(unsigned i=0;i<11;++i){const uint32_t q=i==2?16:(i==3?48:0);if(!wr(0x9040+i*4,q|(q<<16))){quota=false;break;}}
        if(!quota||!rm(0x8400,0x04800000,0x04800000))break;
        r.phase=3;if(!poll(0x8d00,3,3,2000,r.wde)||!poll(0x9100,3,3,2000,r.ple))break;
        r.phase=4;if(!rm(0x8a00,9,0)||!wr(0x8a04,40u<<16)||!rm(0x8a00,0xc00,0)||!rm(0x8a00,9,8))break;
        r.wdeConfig=d.read32(0x8c08);r.pleConfig=d.read32(0x9008);r.hfcControl=d.read32(0x8a00);r.hfcPages=d.read32(0x8a04);
        if(!matches(0x8c08,r.wdeConfig,0x1fff3f03,0)||!matches(0x9008,r.pleConfig,0x1fff3f03,0x00400801)||
           !matches(0x8a00,r.hfcControl,0xc09,8)||!matches(0x8a04,r.hfcPages,0xffffffff,40u<<16)){r.status=Status::invalidRead;break;}
        r.phase=5;
        if(!rm(0x88,2,0)||!rm(0x1e0,7,0)||!rm(8,0x4000,0)||!rm(0x88,4,0)||!rm(0x88,4,4)||!rm(0x88,1,0)||!rm(0x88,1,1))break;
        if(!wr(0x1f4,0)||!wr(0x1f8,0)||!wr(0x160,0)||!wr(0x164,0)||!wr(0x168,0)||!wr(0x16c,0))break;
        if(!rm(8,0x4000,0x4000)||!rm(0x1e0,0xe7,1)||!rm(0xc00,0x30000,0x20000))break;
        ++r.writes;if(!d.bootWrite16(0x1e6,static_cast<uint16_t>(reason&~7u))){r.status=Status::writeFailed;break;}
        const auto reset=d.read32(0x1e0);if(!matches(0x1e0,reset,0xe7,1)){r.status=Status::staleReady;break;}
        if(!rm(0x88,2,2))break;
        r.phase=6;if(!poll(0x1e0,2,2,400000,r.control))break;
        if((r.control&0xe0)==0xe0){r.status=Status::staleReady;break;}
        const auto state=(r.control>>5)&7;if(state>=2&&state<=4){r.status=Status::romError;break;}
        r.dmac=d.read32(0x8400);r.clock=d.read32(0x8404);
        if(!matches(0x8400,r.dmac,0xffffffff,0x64c40000)||!matches(0x8404,r.clock,0xffffffff,0x04840000)){r.status=Status::invalidRead;break;}
        r.status=Status::ready;
    }while(false);
    if(r.status==Status::ready)action(d);
    r.operationStatus=r.status;
    // Every cleanup operation executes, even after an earlier one failed.
    bool ok=true;
    auto clean=[&](uint32_t a,uint32_t mask,uint32_t v){const bool result=rm(a,mask,v);ok=result&&ok;};
    // Only clean blocks whose initialization was attempted. The access window
    // now precedes phase 1, but later HFC/CPU blocks remain stage-scoped.
    if(r.phase>=5){
        clean(0x88,2,0);clean(0x1e0,7,0);clean(8,0x4000,0);
        clean(0x88,4,0);clean(0x88,4,4);clean(0x88,1,0);clean(0x88,1,1);
    }
    if(r.phase>=4)clean(0x8a00,9,0);
    // Verify clocked HFC before disabling its parent DMAC block.
    if(r.phase>=4&&!matches(0x8a00,d.read32(0x8a00),9,0))r.cleanupFailures|=1;
    // Restore HCI while MAC access is still live; a gated shadow readback is
    // not evidence that a restore write reached the actual register.
    if(r.hciCaptured){
        const bool st=wr(0x1010,stop),h=wr(0x1000,hci);ok=st&&h&&ok;
        r.hciAfter=d.read32(0x1000);r.stopAfter=d.read32(0x1010);
        if(!matches(0x1000,r.hciAfter,0xffffffff,hci))r.cleanupFailures|=16;
        if(!matches(0x1010,r.stopAfter,0xffffffff,stop))r.cleanupFailures|=32;
    }
    if(r.accessAttempted){
        // MAC_FUNC_EN gates the downstream register window. 0.0.9 hardware
        // returned 0xffffffff at CLK_EN after FUNC_EN was cleared. Disable and
        // verify clocks while that window is still enabled, then close it.
        const auto function=d.read32(0x8400);
        if(!invalid(function)&&(function&0x40000000)){
            const bool c=wr(0x8404,0);ok=c&&ok;
            r.cleanupClock=d.read32(0x8404);r.cleanupClockChecked=true;
            if(!matches(0x8404,r.cleanupClock,0xffffffff,0))r.cleanupFailures|=4;
        }else{
            // The initial FUNC_EN write may itself have failed. Do not reopen
            // MAC just to inspect clocks; keep the cleanup result unresolved.
            r.cleanupFailures|=4;
        }
        const bool f=wr(0x8400,0);ok=f&&ok;
        r.cleanupDmac=d.read32(0x8400);
        if(!matches(0x8400,r.cleanupDmac,0xffffffff,0))r.cleanupFailures|=2;
    }
    if(r.phase>=5){const bool s=wr(0xc00,sec);++r.writes;const bool b=d.bootWrite16(0x1e6,reason);ok=s&&b&&ok;}
    const auto platform=d.read32(0x88),clock=d.read32(8),control=d.read32(0x1e0);
    r.cpuStopped=!invalid(platform)&&!(platform&2)&&!invalid(clock)&&!(clock&0x4000)&&!invalid(control)&&!(control&7);
    if(r.phase>=5&&(d.read32(0xc00)!=sec||d.read16(0x1e6)!=reason))r.cleanupFailures|=8;
    if(!ok)r.cleanupFailures|=64;
    if(!r.cpuStopped)r.cleanupFailures|=128;
    if((d.command()&6)!=2)r.cleanupFailures|=256;
    r.cleanupOK=r.cleanupFailures==0;
    r.status=r.cleanupOK?r.operationStatus:Status::cleanupFailed;return r;
}
struct NoAction {template<class D> void operator()(D &)const{}};
template<class D> Result probe(D &d){return probeWithAction(d,NoAction{});}
struct RepeatedResult {Result first{},second{};bool secondAttempted{};};
template<class D> RepeatedResult probeRepeated(D &d){
    RepeatedResult r;r.first=probe(d);
    // Confirmation after successful shutdown, never an automatic retry of a
    // failed/ambiguous attempt. Both passes leave DMA and RF disabled.
    if(r.first.status==Status::ready&&r.first.cleanupOK){r.secondAttempted=true;r.second=probe(d);}
    return r;
}
} }
