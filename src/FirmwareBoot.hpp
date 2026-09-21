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
struct Result {Status status{Status::notRun},operationStatus{Status::notRun};unsigned phase{},writes{},polls{};uint32_t wde{},ple{},control{},dmac{},clock{},wdeConfig{},pleConfig{},hfcControl{},hfcPages{};bool attempted{},cleanupOK{},cpuStopped{};};
template<class D> Result probe(D &d){
    Result r;if((d.command()&6)!=2){r.status=Status::unsafeCommand;return r;}
    const auto hci=d.read32(0x1000),stop=d.read32(0x1010),sec=d.read32(0xc00);
    const auto reason=d.read16(0x1e6);
    if(invalid(hci)||invalid(stop)||invalid(sec)||reason==0xffff){r.status=Status::invalidRead;return r;}
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
        r.phase=1;
        if(!wr(0x1000,hci&~0x2800u)||!wr(0x1010,stop|0x7ff03u))break;
        uint32_t idle=0;if(!poll(0x101c,0x7fff03,0,2000,idle))break;
        if((d.read32(0x1000)&0x2800)||((d.read32(0x1010)&0x7ff03)!=0x7ff03)){r.status=Status::writeFailed;break;}
        r.phase=2;
        // DLFW layout: 64 KiB WDE (0 linked pages), 128 KiB PLE (64 linked).
        if(!wr(0x8400,0x60440000)||!wr(0x8404,0x00040000)||!rm(0x8400,0x04800000,0)||!rm(0x8404,0x04800000,0x04800000))break;
        if(!rm(0x8c08,0x1fff3f03,0)||!rm(0x9008,0x1fff3f03,0x00400801))break;
        // WCPU min quota is inherited from the SCC configuration (48).
        if(!wr(0x8c40,0)||!wr(0x8c44,48)||!wr(0x8c4c,0)||!wr(0x8c50,0))break;
        bool quota=true;for(unsigned i=0;i<11;++i){const uint32_t q=i==2?16:(i==3?48:0);if(!wr(0x9040+i*4,q|(q<<16))){quota=false;break;}}
        if(!quota||!rm(0x8400,0x04800000,0x04800000))break;
        r.phase=3;if(!poll(0x8d00,3,3,2000,r.wde)||!poll(0x9100,3,3,2000,r.ple))break;
        r.phase=4;if(!rm(0x8a00,9,0)||!wr(0x8a04,40u<<16)||!rm(0x8a00,0xc00,0)||!rm(0x8a00,9,8))break;
        r.wdeConfig=d.read32(0x8c08);r.pleConfig=d.read32(0x9008);r.hfcControl=d.read32(0x8a00);r.hfcPages=d.read32(0x8a04);
        if(invalid(r.wdeConfig)||invalid(r.pleConfig)||invalid(r.hfcControl)||
           (r.wdeConfig&0x1fff3f03)!=0||(r.pleConfig&0x1fff3f03)!=0x00400801||
           (r.hfcControl&0xc09)!=8||r.hfcPages!=(40u<<16)){r.status=Status::invalidRead;break;}
        r.phase=5;
        if(!rm(0x88,2,0)||!rm(0x1e0,7,0)||!rm(8,0x4000,0)||!rm(0x88,4,0)||!rm(0x88,4,4)||!rm(0x88,1,0)||!rm(0x88,1,1))break;
        if(!wr(0x1f4,0)||!wr(0x1f8,0)||!wr(0x160,0)||!wr(0x164,0)||!wr(0x168,0)||!wr(0x16c,0))break;
        if(!rm(8,0x4000,0x4000)||!rm(0x1e0,0xe7,1)||!rm(0xc00,0x30000,0x20000))break;
        ++r.writes;if(!d.bootWrite16(0x1e6,static_cast<uint16_t>(reason&~7u))){r.status=Status::writeFailed;break;}
        const auto reset=d.read32(0x1e0);if(invalid(reset)||(reset&0xe7)!=1){r.status=Status::staleReady;break;}
        if(!rm(0x88,2,2))break;
        r.phase=6;if(!poll(0x1e0,2,2,400000,r.control))break;
        if((r.control&0xe0)==0xe0){r.status=Status::staleReady;break;}
        const auto state=(r.control>>5)&7;if(state>=2&&state<=4){r.status=Status::romError;break;}
        r.dmac=d.read32(0x8400);r.clock=d.read32(0x8404);
        if(r.dmac!=0x64c40000||r.clock!=0x04840000){r.status=Status::invalidRead;break;}
        r.status=Status::ready;
    }while(false);
    r.operationStatus=r.status;
    // Every cleanup operation executes, even after an earlier one failed.
    bool ok=true;
    auto clean=[&](uint32_t a,uint32_t mask,uint32_t v){const bool result=rm(a,mask,v);ok=result&&ok;};
    clean(0x88,2,0);clean(0x1e0,7,0);clean(8,0x4000,0);
    clean(0x88,4,0);clean(0x88,4,4);clean(0x88,1,0);clean(0x88,1,1);
    clean(0x8a00,9,0);
    const bool f=wr(0x8400,0),c=wr(0x8404,0);ok=f&&c&&ok;
    const bool s=wr(0xc00,sec),b=d.bootWrite16(0x1e6,reason),st=wr(0x1010,stop),h=wr(0x1000,hci);ok=s&&b&&st&&h&&ok;
    const auto platform=d.read32(0x88),clock=d.read32(8),control=d.read32(0x1e0);
    r.cpuStopped=!invalid(platform)&&!(platform&2)&&!invalid(clock)&&!(clock&0x4000)&&!invalid(control)&&!(control&7);
    const auto hfc=d.read32(0x8a00);
    r.cleanupOK=ok&&r.cpuStopped&&d.read32(0x8400)==0&&d.read32(0x8404)==0&&!invalid(hfc)&&!(hfc&9)&&
        d.read32(0xc00)==sec&&d.read16(0x1e6)==reason&&d.read32(0x1000)==hci&&d.read32(0x1010)==stop&&(d.command()&6)==2;
    r.status=r.cleanupOK?r.operationStatus:Status::cleanupFailed;return r;
}
} }
