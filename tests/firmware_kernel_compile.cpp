// SPDX-License-Identifier: BSD-3-Clause
// Compile-only check with the same freestanding flags/SDK as the kext.
// Not linked into the installed diagnostic driver.
#include "../src/FirmwarePlan.hpp"
#include "../src/FirmwareTransfer.hpp"
extern "C" int r16_validate_firmware_layout(const uint8_t *data, size_t size,
                                            uint8_t cut, rtl8852be::firmware::Plan *out) {
    if (!out) return -1;
    return static_cast<int>(rtl8852be::firmware::parse(data,size,cut,*out));
}
extern "C" int r16_encode_firmware_packet(const uint8_t *data,size_t size,uint8_t cut,
                                           unsigned index,uint8_t *out,size_t capacity) {
    rtl8852be::transport::Packets packets;
    const auto status=packets.initialize(data,size,cut);
    if(status!=rtl8852be::transport::PacketStatus::ok)return -1;
    rtl8852be::transport::PacketInfo info;
    return static_cast<int>(packets.encode(index,out,capacity,info));
}
