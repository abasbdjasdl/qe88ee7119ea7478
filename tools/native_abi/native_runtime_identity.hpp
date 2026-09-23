// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef R16_NATIVE_ABI_AUDIT_ONLY
#error "Runtime identity collector still needs target link/load verification"
#endif
#include <stdint.h>
#include <stddef.h>

namespace r16_native_identity {
struct Component {const char *identifier;uint8_t uuid[16];};
// Derived from LC_UUID in the exact audited KC, not release-name guesses.
static constexpr Component components[]={
 {"com.apple.kernel",{0xe6,0x32,0x68,0x09,0x88,0xf4,0x3e,0xcc,0x93,0xbb,0xd2,0xcb,0xdf,0x23,0x55,0x88}},
 {"com.apple.iokit.IO80211Family",{0xb1,0x93,0xf4,0xb7,0x5a,0x7f,0x33,0xb7,0xa6,0x67,0x87,0x11,0x25,0x88,0xaa,0xe7}},
 {"com.apple.iokit.IOSkywalkFamily",{0x9a,0xf0,0x84,0xd1,0x88,0x4f,0x38,0xc5,0xb0,0x7d,0x4b,0x36,0x22,0x19,0xfd,0xc6}},
 {"com.apple.driver.corecapture",{0x4d,0xbe,0xbf,0x77,0xf6,0x3c,0x3a,0x84,0xb8,0xd4,0x50,0xc8,0xe5,0x10,0xbc,0xa3}}
};
constexpr size_t componentCount=sizeof(components)/sizeof(components[0]);
enum class Status : uint8_t {unsupportedKernel,missing,notLoaded,noUUID,invalidUUID,mismatch,matched};
struct Result {
    Status status{Status::unsupportedKernel};
    size_t component{}; // componentCount on complete match; first failure otherwise.
    uint8_t observed[16]{};
};
// Run before hardware/native startup and outside driver gates (OSKext lookup
// takes the global kext lock). A match identifies builds, not executable bytes:
// deployment must separately verify the pinned KC hash. No authorization token,
// session, PCI claim, registry mutation or native registration is produced.
Result inspectLoadedComponents();
}
