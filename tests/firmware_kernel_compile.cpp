// SPDX-License-Identifier: BSD-3-Clause
// Compile-only check with the same freestanding flags/SDK as the kext.
// Not linked into the installed diagnostic driver.
#include "../src/FirmwarePlan.hpp"
extern "C" int r16_validate_firmware_layout(const uint8_t *data, size_t size,
                                            uint8_t cut, rtl8852be::firmware::Plan *out) {
    if (!out) return -1;
    return static_cast<int>(rtl8852be::firmware::parse(data,size,cut,*out));
}
