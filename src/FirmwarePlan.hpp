// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace rtl8852be { namespace firmware {
// Format/selection facts from Realtek rtw89 fw.h/fw.c, BSD-3-Clause option.
// This component has no hardware or filesystem access and never uploads data.
constexpr unsigned maxImages = 64, maxSections = 10, packetBytes = 2020;
enum class Status {
    ok, truncated, badContainer, tooManyImages, imageBounds, imageOverlap,
    ambiguousImage, noMatchingImage, unsupportedHeader, wrongFamily,
    sectionCount, dynamicHeader, unsupportedSection, sectionBounds,
    addressOverflow, trailingData
};
inline const char *statusName(Status s) {
    switch (s) {
    case Status::ok: return "LAYOUT_VALIDATED";
    case Status::truncated: return "TRUNCATED";
    case Status::badContainer: return "BAD_CONTAINER";
    case Status::tooManyImages: return "TOO_MANY_IMAGES";
    case Status::imageBounds: return "IMAGE_BOUNDS";
    case Status::imageOverlap: return "IMAGE_OVERLAP";
    case Status::ambiguousImage: return "AMBIGUOUS_IMAGE";
    case Status::noMatchingImage: return "NO_MATCHING_IMAGE";
    case Status::unsupportedHeader: return "UNSUPPORTED_HEADER";
    case Status::wrongFamily: return "WRONG_CHIP_FAMILY";
    case Status::sectionCount: return "SECTION_COUNT";
    case Status::dynamicHeader: return "DYNAMIC_HEADER";
    case Status::unsupportedSection: return "UNSUPPORTED_SECTION";
    case Status::sectionBounds: return "SECTION_BOUNDS";
    case Status::addressOverflow: return "ADDRESS_OVERFLOW";
    case Status::trailingData: return "TRAILING_DATA";
    }
    return "UNKNOWN";
}
inline uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline bool fits(size_t offset, size_t bytes, size_t total) {
    return offset <= total && bytes <= total - offset;
}
struct Section {
    size_t offset{}; // Absolute offset in unmodified container.
    uint32_t payloadBytes{}, transferBytes{}, address{}, metadata{};
    uint8_t type{};
    bool checksumTrailer{}, redownload{};
};
struct Plan {
    size_t imageOffset{}, imageBytes{}, headerBytes{}, baseHeaderBytes{};
    uint32_t version{}, commit{}, packetCount{};
    uint8_t cut{}, type{}, sectionCount{};
    Section sections[maxSections]{};
    bool valid{};
};
// The caller must use this status, not partially parsed output. On error out
// remains an empty invalid plan. Only normal CE/normal images for an exact cut
// are selected; no cut fallback, manufacturing, WoWLAN or log image selection.
inline Status parse(const uint8_t *data, size_t bytes, uint8_t cut, Plan &out) {
    out = Plan{};
    if (!data || bytes < 16) return Status::truncated;
    if (data[0] != 0xff || !data[1]) return Status::badContainer;
    const unsigned count = data[1];
    if (count > maxImages) return Status::tooManyImages;
    const size_t tableEnd = 16 + size_t(count) * 16;
    if (!fits(0, tableEnd, bytes)) return Status::truncated;
    int normal = -1, ce = -1;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *entry = data + 16 + i * 16;
        const size_t start = le32(entry + 4), length = le32(entry + 8);
        if (!length || start < tableEnd || !fits(start, length, bytes)) return Status::imageBounds;
        for (unsigned j = 0; j < i; ++j) {
            const uint8_t *prior = data + 16 + j * 16;
            const size_t p = le32(prior + 4), n = le32(prior + 8);
            // Subtraction form avoids end-offset overflow on 32-bit hosts.
            if (start >= p ? start - p < n : p - start < length) return Status::imageOverlap;
        }
        if (entry[0] != cut || entry[2] != 0) continue;
        if (entry[1] == 5) {
            if (ce >= 0) return Status::ambiguousImage;
            ce = static_cast<int>(i);
        } else if (entry[1] == 1) {
            if (normal >= 0) return Status::ambiguousImage;
            normal = static_cast<int>(i);
        }
    }
    const int selected = ce >= 0 ? ce : normal;
    if (selected < 0) return Status::noMatchingImage;
    const uint8_t *entry = data + 16 + static_cast<unsigned>(selected) * 16;
    Plan plan{};
    plan.cut = cut; plan.type = entry[1];
    plan.imageOffset = le32(entry + 4); plan.imageBytes = le32(entry + 8);
    if (plan.imageBytes < 32) return Status::truncated;
    const uint8_t *fw = data + plan.imageOffset;
    if (fw[15] != 0) return Status::unsupportedHeader;
    if ((le32(fw) >> 16) != 0x8852) return Status::wrongFamily;
    plan.version = le32(fw + 4); plan.commit = le32(fw + 8);
    plan.sectionCount = fw[25];
    if (!plan.sectionCount || plan.sectionCount > maxSections) return Status::sectionCount;
    plan.baseHeaderBytes = 32 + size_t(plan.sectionCount) * 16;
    plan.headerBytes = plan.baseHeaderBytes;
    if (!fits(0, plan.baseHeaderBytes, plan.imageBytes)) return Status::truncated;
    if (le32(fw + 28) & (1u << 16)) {
        plan.headerBytes = fw[14];
        if (plan.headerBytes < plan.baseHeaderBytes + 8 ||
            !fits(0, plan.headerBytes, plan.imageBytes) ||
            le32(fw + plan.baseHeaderBytes) != plan.headerBytes - plan.baseHeaderBytes)
            return Status::dynamicHeader;
        // Dynamic feature records remain opaque; this is a transfer layout,
        // not a claim that the firmware command ABI/features are implemented.
    }
    size_t cursor = plan.headerBytes;
    for (unsigned i = 0; i < plan.sectionCount; ++i) {
        const uint8_t *h = fw + 32 + i * 16;
        auto &section = plan.sections[i];
        section.metadata = le32(h + 4);
        section.type = static_cast<uint8_t>((section.metadata >> 24) & 15);
        if (section.type != 1 && section.type != 2 && section.type != 9)
            return Status::unsupportedSection;
        // The current candidate has no appended multi-signature blocks.
        // Fail closed for other layouts until their transfer rules are ported.
        if (section.type == 9 && le32(h + 8) != 0) return Status::unsupportedSection;
        section.payloadBytes = section.metadata & 0xffffff;
        if (!section.payloadBytes) return Status::sectionBounds;
        section.checksumTrailer = (section.metadata & (1u << 28)) != 0;
        section.redownload = (section.metadata & (1u << 29)) != 0;
        section.transferBytes = section.payloadBytes + (section.checksumTrailer ? 8 : 0);
        section.address = le32(h) & 0x1fffffff;
        if (section.transferBytes > 0x20000000u - section.address) return Status::addressOverflow;
        if (!fits(cursor, section.transferBytes, plan.imageBytes)) return Status::sectionBounds;
        section.offset = plan.imageOffset + cursor;
        cursor += section.transferBytes;
        plan.packetCount += (section.transferBytes + packetBytes - 1) / packetBytes;
    }
    if (cursor != plan.imageBytes) return Status::trailingData;
    plan.valid = true; out = plan;
    return Status::ok;
}
struct Chunk { size_t offset{}; uint32_t bytes{}, sectionOffset{}; unsigned section{}; };
// Payload segmentation only. Transport descriptors/ownership/interrupts, upload
// handshake and hardware checksum verification are deliberately not implemented.
inline bool chunkAt(const Plan &plan, uint32_t index, Chunk &out) {
    out = Chunk{};
    if (!plan.valid || plan.sectionCount > maxSections || index >= plan.packetCount) return false;
    for (unsigned i = 0; i < plan.sectionCount; ++i) {
        const auto &s = plan.sections[i];
        const uint32_t count = s.transferBytes / packetBytes + (s.transferBytes % packetBytes != 0);
        if (index >= count) { index -= count; continue; }
        const uint32_t offset = index * packetBytes;
        const uint32_t left = s.transferBytes - offset;
        out = {s.offset + offset, left < packetBytes ? left : packetBytes, offset, i};
        return true;
    }
    return false;
}
} }
