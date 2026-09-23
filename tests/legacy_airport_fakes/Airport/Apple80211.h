// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

using IOReturn = uint32_t;
constexpr IOReturn kIOReturnSuccess = 0;
constexpr IOReturn kIOReturnBadArgument = 0xe00002c2u;
constexpr IOReturn kIOReturnUnsupported = 0xe00002c7u;
constexpr uint32_t APPLE80211_VERSION = 1;
constexpr uint32_t APPLE80211_C_FLAG_20MHZ = 2;
constexpr uint32_t APPLE80211_C_FLAG_2GHZ = 8;
constexpr uint32_t APPLE80211_C_FLAG_5GHZ = 16;
constexpr unsigned APPLE80211_MAX_RATES = 15;

struct apple80211_channel { uint32_t version, channel, flags; };
struct __attribute__((packed)) apple80211_scan_result {
    uint32_t version;
    apple80211_channel asr_channel;
    int16_t asr_unk, asr_noise, asr_snr, asr_rssi, asr_beacon_int, asr_cap;
    uint8_t asr_bssid[6], asr_nrates, asr_nr_unk;
    uint32_t asr_rates[APPLE80211_MAX_RATES];
    uint8_t asr_ssid_len, asr_ssid[32];
    int16_t unk;
    uint8_t unk2;
    uint32_t asr_age;
    uint16_t unk3;
    int16_t asr_ie_len;
    uint8_t asr_ie_data[1024];
};
