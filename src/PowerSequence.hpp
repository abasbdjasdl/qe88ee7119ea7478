// SPDX-License-Identifier: BSD-3-Clause
// Supply ordering adapted from rtw8852b.c (BSD option),
// Copyright(c) 2019-2022 Realtek Corporation. See docs/power-sequence.md.
#pragma once
#include "XtalProbe.hpp"
namespace rtl8852be { namespace power {
constexpr uint32_t stateRegister=0x3f0;
enum class Kind { rmw32, rmw8, write32, poll32, delay, xtal };
struct Step { Kind kind; uint32_t address, mask, value, intervalUs, timeoutUs; };
constexpr Step r32(uint32_t a,uint32_t mask,uint32_t value) {return {Kind::rmw32,a,mask,value,0,0};}
constexpr Step r8(uint32_t a,uint32_t mask,uint32_t value) {return {Kind::rmw8,a,mask,value,0,0};}
constexpr Step wait32(uint32_t a,uint32_t mask,uint32_t value) {return {Kind::poll32,a,mask,value,1000,20000};}
constexpr Step xtal(uint8_t a,uint8_t mask,uint8_t value) {return {Kind::xtal,a,mask,value,50,50000};}
// Intentionally stops before the reference func_en label: DMAC/CMAC engines,
// radio transmission, pinmux and calibration-dependent adjustments are excluded.
constexpr Step on[] = {
    r32(0x4,0x1800,0),r32(0x4,0x40000,0x40000),r32(0x90,2,2),
    r32(0x4,0x8000,0),r32(0x4,0x400,0),wait32(0x4,0x20000,0x20000),
    r32(0x20,0x800000,0x800000),wait32(0x20,0x800000,0x800000),
    r32(0x400,3,1),r32(0x400,0x30,0x30),r32(0x4,0x10000,0x10000),
    r32(0x4,0x100,0x100),wait32(0x4,0x100,0),
    r8(0x88,1,1),r8(0x88,1,0),r8(0x88,1,1),r8(0x88,1,0),r8(0x88,1,1),
    r32(0x70,0x1000,0),r32(0x18,0x40,0x40),xtal(0x90,0x40,0x40),
    r32(0x18,0x20,0x20),xtal(0x90,0x20,0x20),xtal(0x90,4,4),
    xtal(0x90,8,8),xtal(0x90,0x10,0),xtal(0x90,1,1),xtal(0x90,2,2),
    xtal(0x90,0x80,0),xtal(0xa1,2,0),xtal(0x24,0x70,0),xtal(0x26,0xf,0),
    r32(0xcc,4,4),r32(0x0,0x100,0x100),r32(0x0,0x8000,0),
    {Kind::delay,0,0,0,1000,0},r32(0x0,0x4000,0),r32(0xcc,4,0)
};
// Reference power-off ordering, no RFE-05 voltage adjustment because EFUSE is
// not read/valid yet. Not an exact register-snapshot rollback.
constexpr Step off[] = {
    xtal(0x90,0x10,0x10),xtal(0x90,8,0),xtal(0x90,4,0),xtal(0x80,1,0),
    xtal(0x81,1,0),xtal(0x90,0x80,0x80),xtal(0x90,2,0),xtal(0x90,1,0),
    r32(0x4,0x10000,0x10000),r32(0x2f0,0x20000,0),r8(0x2,3,0),
    r32(0x18,0x20,0),xtal(0x90,0x20,0),r32(0x18,0x40,0),xtal(0x90,0x40,0),
    r32(0x4,0x200,0x200),wait32(0x4,0x200,0),
    {Kind::write32,0x90,0xffffffff,0x1a0b2,0,0},
    r32(0x10,0x400,0x400),r32(0x200,0x60000,0x60000),r32(0x4,0x400,0x400)
};
enum class Error { none, invalidRead, rejectedWrite, busy, timeout, deadline };
struct SequenceResult { Error error{Error::none}; unsigned step{}, writes{}, polls{}; uint32_t last{}; };
inline bool allowed32(uint32_t address) {
    switch(address) {
    case 0x0:case 0x4:case 0x10:case 0x18:case 0x20:case 0x70:
    case 0x90:case 0xcc:case 0x200:case 0x270:case 0x2f0:case 0x400:return true;
    default:return false;
    }
}
inline bool allowed8(uint32_t address) {return address==0x2 || address==0x88;}
template<class Device>
Error poll(Device &d,uint32_t address,uint32_t mask,uint32_t value,
           unsigned interval,unsigned timeout,uint64_t sequenceStart,SequenceResult &r) {
    const uint64_t start=d.nowUs();
    const unsigned limit=timeout/interval+1;
    for(unsigned i=0;i<limit;++i) {
        if(d.nowUs()-sequenceStart>=1000000) return Error::deadline;
        if(d.nowUs()-start>=timeout) return Error::timeout;
        r.last=d.read32(address);++r.polls;
        if(invalidRegister(r.last)) return Error::invalidRead;
        if((r.last&mask)==value)return Error::none;
        if(i+1<limit)d.pauseUs(interval);
    }
    return Error::timeout;
}
template<class Device, size_t N>
SequenceResult run(Device &d,const Step (&steps)[N]) {
    SequenceResult r;const uint64_t start=d.nowUs();
    for(unsigned i=0;i<N;++i) {
        r.step=i;
        if(d.nowUs()-start>=1000000) {r.error=Error::deadline;return r;}
        const auto &s=steps[i];
        if(s.kind==Kind::delay) {d.pauseUs(s.intervalUs);continue;}
        if(s.kind==Kind::poll32) {
            r.error=poll(d,s.address,s.mask,s.value,s.intervalUs,s.timeoutUs,start,r);
        } else if(s.kind==Kind::xtal) {
            r.last=d.read32(xtalControl);
            if(invalidRegister(r.last))r.error=Error::invalidRead;
            else if(r.last&0x80000000u)r.error=Error::busy;
            else {
                const uint32_t command=0x80000000u|(s.mask<<16)|(s.value<<8)|s.address;
                if(!d.powerWrite32(xtalControl,command))r.error=Error::rejectedWrite;
                else {
                    ++r.writes;
                    r.error=poll(d,xtalControl,0x80000000u,0,50,50000,start,r);
                }
            }
        } else {
            uint32_t value=s.value;
            if(s.kind==Kind::rmw8 || s.kind==Kind::rmw32) {
                r.last=s.kind==Kind::rmw8?d.read8(s.address):d.read32(s.address);
                if(invalidRegister(r.last) || (s.kind==Kind::rmw8 && r.last==0xff)) {
                    r.error=Error::invalidRead;return r;
                }
                value=(r.last&~s.mask)|s.value;
            }
            const bool written=s.kind==Kind::rmw8?
                (allowed8(s.address)&&d.powerWrite8(s.address,static_cast<uint8_t>(value))):
                (allowed32(s.address)&&d.powerWrite32(s.address,value));
            if(!written)r.error=Error::rejectedWrite;
            else ++r.writes;
        }
        if(r.error!=Error::none)return r;
    }
    r.step=N;return r;
}
enum class Status { notRun, preflightFailed, invalidState, initiallyOn, dirtyWriteMask,
    completed, onFailedOff, cleanupFailed, activeNotObserved };
inline const char *statusName(Status s) {
    switch(s) {
    case Status::notRun:return "NOT_RUN";
    case Status::preflightFailed:return "SKIPPED_PREFLIGHT";
    case Status::invalidState:return "INVALID_INITIAL_STATE";
    case Status::initiallyOn:return "SKIPPED_MAC_NOT_OFF";
    case Status::dirtyWriteMask:return "SKIPPED_PMC_WRITE_MASK";
    case Status::completed:return "SUPPLY_CYCLE_COMPLETE";
    case Status::onFailedOff:return "POWER_ON_FAILED_CLEANUP_OFF";
    case Status::cleanupFailed:return "CLEANUP_NOT_CONFIRMED";
    case Status::activeNotObserved:return "MAC_ACTIVE_NOT_OBSERVED";
    }
    return "UNKNOWN";
}
struct Result {
    Status status{Status::notRun};
    SequenceResult on{}, off{};
    uint32_t initialState{}, activeState{}, finalState{}, initialPower{}, finalPower{};
    bool attempted{}, cleanupAttempted{}, returnedOff{}, activeObserved{};
};
template<class Device,class Extension>
Result cycle(Device &d,const MmioResult &mmio,const XtalResult &xtalResult,Extension extension) {
    Result r;
    if(!mmio.stableValue() || ((mmio.cfgFirst>>12)&15)!=1 ||
       xtalResult.status!=XtalStatus::complete || !xtalResult.revisionValid ||
       xtalResult.rawRevision!=0x11 || (d.command()&6)!=2) {
        r.status=Status::preflightFailed;return r;
    }
    r.initialState=d.read32(stateRegister);r.initialPower=d.read32(sysPower);
    const auto pmc=d.read32(0xcc);
    if(invalidRegister(r.initialState)||invalidRegister(r.initialPower)||invalidRegister(pmc)) {
        r.status=Status::invalidState;return r;
    }
    if(((r.initialState>>8)&3)!=0) {r.status=Status::initiallyOn;return r;}
    if(pmc&4) {r.status=Status::dirtyWriteMask;return r;}
    r.attempted=true;r.on=run(d,on);
    if(r.on.error==Error::none) {
        r.activeState=d.read32(stateRegister);
        r.activeObserved=!invalidRegister(r.activeState)&&((r.activeState>>8)&3)==1;
    }
    if(r.activeObserved)extension(d);
    // Any possible partial power-on is followed by the reference shutdown path.
    r.cleanupAttempted=true;r.off=run(d,off);
    // The isolation sequence temporarily opens a write mask; always close our
    // bit after the shutdown attempt, including an interrupted on sequence.
    const auto mask=d.read32(0xcc);
    const bool maskClosed=!invalidRegister(mask)&&(!(mask&4)||d.powerWrite32(0xcc,mask&~4u));
    const auto maskAfter=d.read32(0xcc);
    r.finalState=d.read32(stateRegister);r.finalPower=d.read32(sysPower);
    r.returnedOff=r.off.error==Error::none && maskClosed && !invalidRegister(maskAfter) && !(maskAfter&4) &&
        !invalidRegister(r.finalState) && ((r.finalState>>8)&3)==0 &&
        !invalidRegister(r.finalPower) && (r.finalPower&0x400);
    if(!r.returnedOff)r.status=Status::cleanupFailed;
    else if(r.on.error!=Error::none)r.status=Status::onFailedOff;
    else if(!r.activeObserved)r.status=Status::activeNotObserved;
    else r.status=Status::completed;
    return r;
}
struct NoExtension {template<class Device> void operator()(Device &) const {}};
template<class Device>
Result cycle(Device &d,const MmioResult &mmio,const XtalResult &x) {
    return cycle(d,mmio,x,NoExtension{});
}
} }
