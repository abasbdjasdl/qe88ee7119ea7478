// SPDX-License-Identifier: GPL-2.0-or-later
// Run on a macOS host with the pinned Ventura Airport headers and kernel SDK.
#include "../src/legacy_airport/LegacyScanResult.hpp"

using namespace rtl8852be::network;

int main() {
    nativescan::Entry entry{};
    const uint8_t ies[] = {
        0, 3, 'a', 0, 'b',
        1, 2, 0x82, 0x84,
        48, 20, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4,
        1, 0, 0, 0x0f, 0xac, 2, 0, 0,
    };
    const uint8_t bssid[] = {2, 1, 2, 3, 4, 5};
    memcpy(entry.bssid, bssid, sizeof(bssid));
    entry.ssidLength = 3;
    memcpy(entry.ssid, ies + 2, 3);
    memcpy(entry.ies, ies, sizeof(ies));
    entry.ieLength = sizeof(ies);
    entry.channel = {nativescan::Band::ghz2, 6};
    entry.signal = {nativescan::SignalUnit::dbm, -52};
    entry.capability = 0x431;
    entry.beaconInterval = 100;
    entry.observedAtUs = 1000000;
    apple80211_scan_result result{};

    if (legacyairport::scanResult(entry, 1027000, result) != kIOReturnSuccess)
        return 1;
    if (result.version != APPLE80211_VERSION || result.asr_ssid_len != 3 ||
        memcmp(result.asr_ssid, ies + 2, 3) ||
        memcmp(result.asr_bssid, bssid, sizeof(bssid)) ||
        result.asr_channel.channel != 6 || result.asr_channel.flags !=
            (APPLE80211_C_FLAG_20MHZ | APPLE80211_C_FLAG_2GHZ) ||
        result.asr_rssi != -52 || result.asr_noise != 0 ||
        result.asr_nrates != 2 || result.asr_rates[0] != 0x82 ||
        result.asr_rates[1] != 0x84 || result.asr_age != 27 ||
        result.asr_ie_len != 22 || memcmp(result.asr_ie_data, ies + 9, 22))
        return 2;

    // An unknown/percent signal must not be invented as a dBm reading.
    entry.signal = {nativescan::SignalUnit::percent, 53};
    if (legacyairport::scanResult(entry, 1027000, result) != kIOReturnUnsupported ||
        result.version != 0)
        return 3;
    entry.signal = {nativescan::SignalUnit::dbm, -52};
    entry.ies[entry.ieLength++] = 50; // An incomplete final TLV is rejected.
    if (legacyairport::scanResult(entry, 1027000, result) != kIOReturnBadArgument ||
        result.version != 0)
        return 4;
    --entry.ieLength;

    // A TLV can fit the buffer while being too short to contain an RSN body.
    entry.ies[entry.ieLength++] = 48;
    entry.ies[entry.ieLength++] = 0;
    if (legacyairport::scanResult(entry, 1027000, result) != kIOReturnBadArgument ||
        result.version != 0)
        return 6;
    entry.ieLength -= 2;

    // The WPA vendor OUI/type alone is not a complete WPA information element.
    const uint8_t shortWpa[] = {221, 4, 0, 0x50, 0xf2, 1};
    memcpy(entry.ies + entry.ieLength, shortWpa, sizeof(shortWpa));
    entry.ieLength += sizeof(shortWpa);
    if (legacyairport::scanResult(entry, 1027000, result) != kIOReturnBadArgument ||
        result.version != 0)
        return 7;
    entry.ieLength -= sizeof(shortWpa);

    // A 5 GHz result is emitted only when the backend observed that band.
    entry.channel = {nativescan::Band::ghz5, 36};
    if (legacyairport::scanResult(entry, 1027000, result) != kIOReturnSuccess ||
        result.asr_channel.flags !=
            (APPLE80211_C_FLAG_20MHZ | APPLE80211_C_FLAG_5GHZ))
        return 5;
    return 0;
}
