// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "PciConfig.hpp"
namespace rtl8852be {
// Register facts: rtw89 reg.h/core.c, GPL-2.0 OR BSD-3-Clause.
constexpr uint32_t sysCfg1 = 0x00f0, sysStatus1 = 0x00f4;
constexpr uint16_t memoryEnable = 2, busMasterEnable = 4;
enum class MmioStatus {
    wrongTarget, unsafeConfig, notD0, busMasterActive, badBar,
    resourceMismatch, mapFailed, enableFailed, sampled, restoreFailed
};
inline const char *statusName(MmioStatus s) {
    switch (s) {
    case MmioStatus::wrongTarget: return "SKIPPED_WRONG_TARGET";
    case MmioStatus::unsafeConfig: return "SKIPPED_INVALID_PM_CAPABILITY";
    case MmioStatus::notD0: return "SKIPPED_NOT_D0";
    case MmioStatus::busMasterActive: return "SKIPPED_BUS_MASTER_ACTIVE";
    case MmioStatus::badBar: return "SKIPPED_INVALID_BAR2";
    case MmioStatus::resourceMismatch: return "SKIPPED_RESOURCE_MISMATCH";
    case MmioStatus::mapFailed: return "MAP_FAILED";
    case MmioStatus::enableFailed: return "MEMORY_ENABLE_FAILED";
    case MmioStatus::sampled: return "SAMPLED";
    case MmioStatus::restoreFailed: return "COMMAND_RESTORE_FAILED";
    }
    return "UNKNOWN";
}
struct MmioResult {
    MmioStatus status{MmioStatus::wrongTarget};
    uint16_t commandBefore{}, commandDuring{}, commandAfter{};
    uint64_t base{}, length{};
    uint32_t cfgFirst{}, cfgSecond{}, sysStatus{};
    unsigned reads{};
    bool memoryBitChanged{}, commandRestored{};
    bool stableValue() const {
        return reads == 3 && cfgFirst == cfgSecond && cfgFirst != 0 &&
               cfgFirst != 0xffffffff && cfgFirst != 0xdeadbeef;
    }
};
// The extension runs inside the validated transaction. Cleanup remains owned
// here so every extension outcome restores the memory-enable bit.
template<class Device, class Extension>
MmioResult sampleMmio(Device &d, const Snapshot &s, Extension extension) {
    MmioResult r;
    if (!s.isTarget() || s.subsystemVendor != 0x1a3b || s.subsystemDevice != 0x5470) return r;
    r.status = MmioStatus::unsafeConfig;
    if (s.capStatus != CapStatus::valid || !s.pmControlReadable) return r;
    r.status = MmioStatus::notD0;
    if ((s.pmControlStatus & 3) != 0) return r;
    r.commandBefore = d.command();
    r.commandDuring = r.commandAfter = r.commandBefore;
    r.commandRestored = true;
    r.status = MmioStatus::busMasterActive;
    if (r.commandBefore & busMasterEnable) return r;
    r.status = MmioStatus::badBar;
    if ((s.bars[2] & 7) != 4) return r;
    r.base = (uint64_t(s.bars[3]) << 32) | (s.bars[2] & ~uint32_t(15));
    if (!r.base || r.base == 0xfffffffffffffff0ULL) return r;
    r.length = d.length();
    r.status = MmioStatus::resourceMismatch;
    if (d.physical() != r.base || r.length != 0x100000) return r;
    r.status = MmioStatus::mapFailed;
    if (!d.map()) return r;
    if (!(r.commandBefore & memoryEnable)) {
        r.memoryBitChanged = true;
        d.writeCommand(r.commandBefore | memoryEnable);
    }
    r.commandDuring = d.command();
    r.status = MmioStatus::enableFailed;
    if (r.commandDuring == uint16_t(r.commandBefore | memoryEnable)) {
        r.cfgFirst = d.read32(sysCfg1); ++r.reads;
        r.sysStatus = d.read32(sysStatus1); ++r.reads;
        r.cfgSecond = d.read32(sysCfg1); ++r.reads;
        r.status = MmioStatus::sampled;
        extension(d, r);
    }
    d.unmap();
    if (r.memoryBitChanged) {
        const uint16_t now = d.command();
        // Restore only our bit. Never write PCI status W1C bits or overwrite
        // unrelated command changes made by another component.
        if (now != 0xffff)
            d.writeCommand((now & ~memoryEnable) | (r.commandBefore & memoryEnable));
    }
    r.commandAfter = d.command();
    r.commandRestored = r.commandAfter == r.commandBefore;
    if (!r.commandRestored) r.status = MmioStatus::restoreFailed;
    return r;
}
struct NoMmioExtension {
    template<class Device> void operator()(Device &, const MmioResult &) const {}
};
template<class Device>
MmioResult sampleMmio(Device &d, const Snapshot &s) {
    return sampleMmio(d,s,NoMmioExtension{});
}
}
