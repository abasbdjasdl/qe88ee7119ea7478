// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace rtl8852be {
constexpr uint16_t vendor = 0x10ec;
constexpr uint16_t device = 0xb852;
constexpr size_t configSize = 256;
inline uint16_t read16(const uint8_t *p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline uint32_t read32(const uint8_t *p) {
    return uint32_t(read16(p)) | (uint32_t(read16(p + 2)) << 16);
}
enum class CapStatus { none, valid, truncated, malformed, cycle };
struct Snapshot {
    uint16_t vendorID{}, deviceID{}, subsystemVendor{}, subsystemDevice{};
    uint16_t command{};
    uint8_t revision{}, headerType{};
    uint32_t bars[6]{};
    uint64_t capabilities{}; // Standard capability IDs below 64.
    unsigned capabilityCount{};
    CapStatus capStatus{CapStatus::none};
    bool isTarget() const { return vendorID == vendor && deviceID == device; }
    bool hasCapability(unsigned id) const {
        return id < 64 && (capabilities & (uint64_t(1) << id));
    }
};

// Decode only. Never size BARs by writing all-ones into live PCI registers.
inline bool decode(const uint8_t *bytes, size_t length, Snapshot &out) {
    out = Snapshot{};
    if (!bytes || length < 0x40) return false;
    out.vendorID = read16(bytes);
    out.deviceID = read16(bytes + 2);
    out.command = read16(bytes + 4);
    out.revision = bytes[8];
    out.headerType = bytes[0x0e] & 0x7f;
    if (out.headerType != 0) return false; // Only endpoint headers.
    out.subsystemVendor = read16(bytes + 0x2c);
    out.subsystemDevice = read16(bytes + 0x2e);
    for (unsigned i = 0; i < 6; ++i) out.bars[i] = read32(bytes + 0x10 + i * 4);
    if (!(read16(bytes + 6) & 0x10)) return true;
    uint8_t offset = bytes[0x34];
    uint64_t visited = 0;
    out.capStatus = CapStatus::valid;
    while (offset) {
        if (offset < 0x40 || offset > 0xfc || (offset & 3)) {
            out.capStatus = CapStatus::malformed; break;
        }
        if (size_t(offset) + 2 > length) {
            out.capStatus = CapStatus::truncated; break;
        }
        const uint64_t bit = uint64_t(1) << (offset / 4);
        if (visited & bit) { out.capStatus = CapStatus::cycle; break; }
        visited |= bit;
        const unsigned id = bytes[offset];
        if (id < 64) out.capabilities |= uint64_t(1) << id;
        ++out.capabilityCount;
        offset = bytes[offset + 1];
    }
    return true;
}
} // namespace rtl8852be
