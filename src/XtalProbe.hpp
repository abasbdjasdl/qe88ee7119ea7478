// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "MmioProbe.hpp"
namespace rtl8852be {
// rtw89 mac.c:rtw89_mac_read_xtal_si_ax; reg.h/mac.h at d1fced1.
// Exactly one indirect READ command for XTAL_SI_CV (0x41), never an analog write.
constexpr uint32_t xtalControl = 0x270, xtalCvReadCommand = 0x81000041;
constexpr uint32_t sysPower = 0x4, sysClock = 0x8, firmwareControl = 0x1e0;
enum class XtalStatus { notRun, unstableChip, wrongCut, invalidPower, powerNotReady,
    invalidControl, busy, commandRejected, timeout, invalidData, complete };
inline const char *xtalStatusName(XtalStatus s) {
    switch(s) {
    case XtalStatus::notRun: return "NOT_RUN";
    case XtalStatus::unstableChip: return "SKIPPED_UNSTABLE_CHIP";
    case XtalStatus::wrongCut: return "SKIPPED_UNTESTED_DIGITAL_CUT";
    case XtalStatus::invalidPower: return "INVALID_POWER_READ";
    case XtalStatus::powerNotReady: return "SKIPPED_SYSTEM_POWER_NOT_READY";
    case XtalStatus::invalidControl: return "INVALID_XTAL_CONTROL";
    case XtalStatus::busy: return "SKIPPED_XTAL_BUSY";
    case XtalStatus::commandRejected: return "READ_COMMAND_REJECTED";
    case XtalStatus::timeout: return "XTAL_READ_TIMEOUT";
    case XtalStatus::invalidData: return "INVALID_XTAL_DATA";
    case XtalStatus::complete: return "XTAL_READ_COMPLETE";
    }
    return "UNKNOWN";
}
struct XtalResult {
    XtalStatus status{XtalStatus::notRun};
    uint32_t isolation{}, powerBefore{}, powerAfter{}, clock{}, firmware{};
    uint32_t controlBefore{}, controlAfter{};
    uint64_t elapsedUs{};
    unsigned reads32{}, reads8{}, writes{}, polls{};
    uint8_t rawRevision{};
    bool powerSampled{}, powerAfterSampled{}, revisionValid{};
};
inline bool invalidRegister(uint32_t value) { return value==0xffffffff || value==0xdeadbeef; }
// Called only inside the validated BAR transaction. All exits return to its
// unmap/PCI-command restore path. Deadline + poll-count cap protect against a
// stuck command or clock. A bus access/kernel fault cannot be timed out here.
template<class Device>
XtalResult sampleXtal(Device &d, const MmioResult &baseline) {
    XtalResult r;
    if (!baseline.stableValue()) { r.status=XtalStatus::unstableChip; return r; }
    if (((baseline.cfgFirst>>12)&15)!=1) {r.status=XtalStatus::wrongCut;return r;}
    r.isolation=d.read32(0x0); r.powerBefore=d.read32(sysPower);
    r.clock=d.read32(sysClock); r.firmware=d.read32(firmwareControl);
    r.reads32=4; r.powerSampled=true;
    if (invalidRegister(r.powerBefore)) {r.status=XtalStatus::invalidPower;return r;}
    if (!(r.powerBefore & (1u<<17)) || (r.powerBefore & (1u<<22))) {
        r.status=XtalStatus::powerNotReady;return r;
    }
    r.controlBefore=d.read32(xtalControl);++r.reads32;
    if (invalidRegister(r.controlBefore)) {r.status=XtalStatus::invalidControl;return r;}
    if (r.controlBefore & 0x80000000u) {r.status=XtalStatus::busy;return r;}
    const uint64_t start=d.nowUs();
    if (!d.startXtalCvRead()) {r.status=XtalStatus::commandRejected;return r;}
    r.writes=1;
    r.status=XtalStatus::timeout;
    for(unsigned i=0;i<1001;++i) {
        // Do not poll again when a descheduled thread returns too late.
        if (d.nowUs()-start>=50000) break;
        r.controlAfter=d.read32(xtalControl);++r.reads32;++r.polls;
        if (invalidRegister(r.controlAfter)) {r.status=XtalStatus::invalidControl;break;}
        if (!(r.controlAfter & 0x80000000u)) {
            r.rawRevision=d.readXtalData();++r.reads8;
            r.revisionValid=r.rawRevision!=0xff;
            r.status=r.revisionValid?XtalStatus::complete:XtalStatus::invalidData;
            break;
        }
        if (i<1000 && d.nowUs()-start<50000) d.pause50Us();
    }
    r.elapsedUs=d.nowUs()-start;
    if (r.status==XtalStatus::complete || r.status==XtalStatus::invalidData) {
        r.powerAfter=d.read32(sysPower);++r.reads32;r.powerAfterSampled=true;
    }
    // Do not restore the command register: replaying an old control value can
    // launch another transaction. No retry/reset/power-on on timeout.
    return r;
}
}
