// SPDX-License-Identifier: BSD-3-Clause
// Register definitions/layout derived from rtw89 pci.h/pci.c, dual BSD/GPL.
// Copyright(c) 2020-2022 Realtek Corporation
#pragma once
#include "DmaProbe.hpp"
namespace rtl8852be { namespace ring {
constexpr uint32_t init=0x1000,stop=0x1010,busy=0x101c,num=0x1038;
constexpr uint32_t index=0x1080,low=0x1160,high=0x1164,ram=0x1228;
constexpr uint32_t hciEnable=0x2800,stopCh12=0x40000,busyMask=0x7fff03;
constexpr uint32_t indexMask=0x0fff0fff,ramMask=0x00ffffff,ramLayout=0x01041c;
constexpr uint16_t countMask=0x0fff,count=256;
inline bool allowed32(uint32_t a){return a==init||a==stop||a==low||a==high||a==ram;}
inline bool allowed16(uint32_t a){return a==num;}
inline bool invalid(uint32_t v){return v==0xffffffff||v==0xdeadbeef;}
enum class Status {notRun,badMapping,badCommand,invalidRead,busy,dirtyIndex,
    writeFailed,readbackFailed,validated,restoreFailed};
inline const char *statusName(Status s){
    switch(s){
    case Status::notRun:return "NOT_RUN";
    case Status::badMapping:return "RING_INVALID_MAPPING";
    case Status::badCommand:return "RING_UNSAFE_PCI_COMMAND";
    case Status::invalidRead:return "RING_INVALID_READ";
    case Status::busy:return "RING_SKIPPED_BUSY";
    case Status::dirtyIndex:return "RING_SKIPPED_NONZERO_INDEX";
    case Status::writeFailed:return "RING_WRITE_FAILED";
    case Status::readbackFailed:return "RING_READBACK_FAILED";
    case Status::validated:return "RING_CONFIG_VALIDATED";
    case Status::restoreFailed:return "RING_RESTORE_FAILED";
    }
    return "UNKNOWN";
}
struct Snapshot {uint32_t init{},stop{},busy{},index{},low{},high{},ram{};uint16_t num{};};
struct Result {
    Status status{Status::notRun},operationStatus{Status::notRun};
    Snapshot before{},configured{},after{};
    unsigned writeAttempts{},restoreAttempts{};
    bool attempted{},readbackOK{},restored{},busMasterAfter{};
};
template<class D> Snapshot read(D &d){
    return {d.read32(init),d.read32(stop),d.read32(busy),d.read32(index),
        d.read32(low),d.read32(high),d.read32(ram),d.read16(num)};
}
inline bool valid(const Snapshot &s){
    return !invalid(s.init)&&!invalid(s.stop)&&!invalid(s.busy)&&!invalid(s.index)&&
        !invalid(s.low)&&!invalid(s.high)&&!invalid(s.ram)&&s.num!=0xffff;
}
inline bool equalConfig(const Snapshot &a,const Snapshot &b){
    return a.init==b.init&&a.stop==b.stop&&a.index==b.index&&a.low==b.low&&
        a.high==b.high&&a.ram==b.ram&&a.num==b.num;
}
template<class D> Result probe(D &d,const dma::Mapping &mapping){
    Result r;
    if(!dma::validMapping(mapping)){r.status=Status::badMapping;return r;}
    if((d.command()&6)!=2){r.status=Status::badCommand;return r;}
    r.before=read(d);
    if(!valid(r.before)){r.status=Status::invalidRead;return r;}
    if(r.before.busy&busyMask){r.status=Status::busy;return r;}
    if(r.before.index&indexMask){r.status=Status::dirtyIndex;return r;}
    auto wanted=r.before;
    wanted.init&=~hciEnable;wanted.stop|=stopCh12;
    wanted.low=static_cast<uint32_t>(mapping.address);wanted.high=0;
    wanted.num=static_cast<uint16_t>((wanted.num&~countMask)|count);
    wanted.ram=(wanted.ram&~ramMask)|ramLayout;
    r.attempted=true;
    auto write32=[&](uint32_t a,uint32_t v){++r.writeAttempts;return d.ringWrite32(a,v);};
    auto write16=[&](uint32_t a,uint16_t v){++r.writeAttempts;return d.ringWrite16(a,v);};
    do {
        if(!write32(init,wanted.init)||!write32(stop,wanted.stop)) {r.status=Status::writeFailed;break;}
        const auto quiescent=read(d);
        if(!valid(quiescent)||quiescent.init!=wanted.init||quiescent.stop!=wanted.stop||
           (quiescent.busy&busyMask)||quiescent.index!=r.before.index||(d.command()&6)!=2){
            r.status=Status::readbackFailed;break;
        }
        // The queue is stopped and PCI bus mastering remains OFF. Never write
        // an index/doorbell, clear-pointer trigger, BDRAM reset, or interrupt.
        if(!write32(high,0)||!write32(low,wanted.low)||!write16(num,wanted.num)||!write32(ram,wanted.ram)){
            r.status=Status::writeFailed;break;
        }
        r.configured=read(d);
        r.readbackOK=valid(r.configured)&&equalConfig(r.configured,wanted)&&
            !(r.configured.busy&busyMask)&&(d.command()&6)==2;
        r.status=r.readbackOK?Status::validated:Status::readbackFailed;
    }while(false);
    r.operationStatus=r.status;
    // Restore queue addresses/count/layout while HCI is disabled, then stop
    // and HCI controls. Attempt every restoration even after one write fails.
    bool restored=true;
    auto restore32=[&](uint32_t a,uint32_t v){++r.restoreAttempts;const bool ok=d.ringWrite32(a,v);restored=ok&&restored;};
    restore32(ram,r.before.ram);
    ++r.restoreAttempts;restored=d.ringWrite16(num,r.before.num)&&restored;
    restore32(low,r.before.low);restore32(high,r.before.high);
    restore32(stop,r.before.stop);restore32(init,r.before.init);
    r.after=read(d);r.busMasterAfter=(d.command()&4)!=0;
    r.restored=restored&&valid(r.after)&&equalConfig(r.after,r.before)&&!r.busMasterAfter;
    if(!r.restored)r.status=Status::restoreFailed;
    return r;
}
} }
