// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Ventura IO80211Family's Apple80211 ABI, also used by the restored legacy
// family on newer installed macOS releases. This is deliberately outside
// src/network: the working Ethernet kext does not link or register it.
#include <Airport/Apple80211.h>
#include <libkern/libkern.h>
#include <stddef.h>
#include "../network/NativeScanCache.hpp"

#if __IO80211_TARGET != 130000
#error "This adapter is only audited against the Ventura Apple80211 ABI"
#endif
static_assert(sizeof(apple80211_scan_result) == 1164,
              "Ventura apple80211_scan_result layout changed");
static_assert(sizeof(apple80211_channel) == 12 &&
              offsetof(apple80211_channel, version) == 0 &&
              offsetof(apple80211_channel, channel) == 4 &&
              offsetof(apple80211_channel, flags) == 8,
              "Ventura apple80211_channel layout changed");
static_assert(offsetof(apple80211_scan_result, asr_channel) == 0x04 &&
              offsetof(apple80211_scan_result, asr_rssi) == 0x16 &&
              offsetof(apple80211_scan_result, asr_beacon_int) == 0x18 &&
              offsetof(apple80211_scan_result, asr_cap) == 0x1a &&
              offsetof(apple80211_scan_result, asr_bssid) == 0x1c &&
              offsetof(apple80211_scan_result, asr_nrates) == 0x22 &&
              offsetof(apple80211_scan_result, asr_rates) == 0x24 &&
              offsetof(apple80211_scan_result, asr_ssid_len) == 0x60 &&
              offsetof(apple80211_scan_result, asr_ssid) == 0x61 &&
              offsetof(apple80211_scan_result, asr_age) == 0x84 &&
              offsetof(apple80211_scan_result, asr_ie_len) == 0x8a &&
              offsetof(apple80211_scan_result, asr_ie_data) == 0x8c,
              "Ventura scan fields moved");

namespace rtl8852be { namespace network { namespace legacyairport {

// Encode one *completed* hardware observation. The caller owns both objects
// and must hold a stable scan generation while copying Entry from the backend.
// Apple80211 has room for only 1024 IE bytes; use the real RSN/WPA TLVs rather
// than fabricating capabilities or truncating an IE in the middle. No noise
// sample exists in the current Realtek backend, so noise/SNR remain zero.
inline IOReturn scanResult(const nativescan::Entry &entry,
                          uint64_t reportedAtUs,
                          apple80211_scan_result &out) {
    memset(&out, 0, sizeof(out));
    if (!nativescan::valid(entry.channel) || entry.ssidLength > 32 ||
        entry.ieLength > nativescan::maxIeBytes ||
        entry.signal.unit != nativescan::SignalUnit::dbm ||
        !nativescan::valid(entry.signal) ||
        entry.observedAtUs > reportedAtUs || entry.beaconInterval > 32767)
        return kIOReturnUnsupported;

    unsigned char address = 0;
    for (unsigned i = 0; i < 6; ++i) address |= entry.bssid[i];
    if (!address || (entry.bssid[0] & 1)) return kIOReturnBadArgument;

    // Validate the entire IE stream before publishing any portion of it.
    size_t rateCount = 0, securityBytes = 0;
    for (size_t pos = 0; pos < entry.ieLength;) {
        if (entry.ieLength - pos < 2) return kIOReturnBadArgument;
        const uint8_t id = entry.ies[pos];
        const size_t bytes = size_t(entry.ies[pos + 1]) + 2;
        if (bytes > entry.ieLength - pos) return kIOReturnBadArgument;
        if (id == 1 || id == 50) {
            if (rateCount + bytes - 2 > APPLE80211_MAX_RATES)
                return kIOReturnUnsupported;
            rateCount += bytes - 2;
        }
        const bool wpaVendor = id == 221 && bytes >= 6 &&
            entry.ies[pos + 2] == 0x00 && entry.ies[pos + 3] == 0x50 &&
            entry.ies[pos + 4] == 0xf2 && entry.ies[pos + 5] == 0x01;
        if (id == 48 || wpaVendor) {
            // Require enough bytes to hold the fixed fields and one pairwise
            // and AKM suite. This is not a full suite-grammar/auth check.
            if ((id == 48 && bytes < 20) || (wpaVendor && bytes < 24))
                return kIOReturnBadArgument;
            if (securityBytes + bytes > sizeof(out.asr_ie_data))
                return kIOReturnUnsupported;
            securityBytes += bytes;
        }
        pos += bytes;
    }

    out.version = APPLE80211_VERSION;
    out.asr_channel.version = APPLE80211_VERSION;
    out.asr_channel.channel = entry.channel.number;
    out.asr_channel.flags = APPLE80211_C_FLAG_20MHZ |
        (entry.channel.band == nativescan::Band::ghz5 ?
            APPLE80211_C_FLAG_5GHZ : APPLE80211_C_FLAG_2GHZ);
    // This is a measured PHY sample, though not attributed to this exact MPDU.
    out.asr_rssi = entry.signal.value;
    out.asr_beacon_int = int16_t(entry.beaconInterval);
    out.asr_cap = int16_t(entry.capability);
    memcpy(out.asr_bssid, entry.bssid, sizeof(entry.bssid));
    out.asr_ssid_len = entry.ssidLength;
    memcpy(out.asr_ssid, entry.ssid, entry.ssidLength);
    const uint64_t ageMs = (reportedAtUs - entry.observedAtUs) / 1000;
    out.asr_age = ageMs > UINT32_MAX ? UINT32_MAX : uint32_t(ageMs);

    size_t rateIndex = 0, securityIndex = 0;
    for (size_t pos = 0; pos < entry.ieLength;) {
        const uint8_t id = entry.ies[pos];
        const size_t bytes = size_t(entry.ies[pos + 1]) + 2;
        if (id == 1 || id == 50)
            for (size_t i = 2; i < bytes; ++i)
                out.asr_rates[rateIndex++] = entry.ies[pos + i];
        const bool wpaVendor = id == 221 && bytes >= 6 &&
            entry.ies[pos + 2] == 0x00 && entry.ies[pos + 3] == 0x50 &&
            entry.ies[pos + 4] == 0xf2 && entry.ies[pos + 5] == 0x01;
        if (id == 48 || wpaVendor) {
            memcpy(out.asr_ie_data + securityIndex, entry.ies + pos, bytes);
            securityIndex += bytes;
        }
        pos += bytes;
    }
    out.asr_nrates = uint8_t(rateIndex);
    out.asr_ie_len = int16_t(securityIndex);
    return kIOReturnSuccess;
}

} } }
