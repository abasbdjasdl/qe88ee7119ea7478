// SPDX-License-Identifier: BSD-3-Clause
#include "../src/PciConfig.hpp"
#include <assert.h>
#include <stdio.h>
using namespace rtl8852be;
int main() {
    uint8_t c[configSize]{};
    c[0] = 0xec; c[1] = 0x10; c[2] = 0x52; c[3] = 0xb8;
    c[0x2c]=0x3b; c[0x2d]=0x1a; c[0x2e]=0x70; c[0x2f]=0x54;
    c[0x10]=0x04; c[0x13]=0x80;
    Snapshot s{};
    assert(!decode(nullptr, 256, s));
    assert(!decode(c, 0x3f, s));
    assert(decode(c, sizeof(c), s) && s.isTarget());
    assert(s.subsystemVendor == 0x1a3b && s.subsystemDevice == 0x5470);
    assert(s.bars[0] == 0x80000004 && s.capStatus == CapStatus::none);
    c[6]=0x10; c[0x34]=0x40; c[0x40]=1; c[0x41]=0x50;
    c[0x50]=5; c[0x51]=0x60; c[0x60]=0x10;
    assert(decode(c, sizeof(c), s) && s.capStatus == CapStatus::valid);
    assert(s.capabilityCount == 3 && s.hasCapability(1) && s.hasCapability(5) && s.hasCapability(0x10));
    assert(s.pmOffset == 0x40 && s.pmControlReadable && s.pmControlStatus == 0);
    c[0x44]=3;
    assert(decode(c, sizeof(c), s) && s.pmControlStatus == 3);
    c[0x44]=0;
    assert(!s.hasCapability(0x11) && !s.hasCapability(64));
    c[0x61]=0x40;
    assert(decode(c, sizeof(c), s) && s.capStatus == CapStatus::cycle);
    c[0x61]=0x43;
    assert(decode(c, sizeof(c), s) && s.capStatus == CapStatus::malformed);
    c[0x61]=0;
    assert(decode(c, 0x60, s) && s.capStatus == CapStatus::truncated);
    c[0x34]=0xfc; c[0xfc]=0xff;
    assert(decode(c, sizeof(c), s) && s.capStatus == CapStatus::valid && s.capabilityCount == 1);
    c[0x34]=0x20;
    assert(decode(c, sizeof(c), s) && s.capStatus == CapStatus::malformed);
    c[0x0e]=1;
    assert(!decode(c, sizeof(c), s));
    c[0x0e]=0x80; c[6]=0;
    assert(decode(c, sizeof(c), s));
    c[3]=0xb7;
    assert(decode(c, sizeof(c), s) && !s.isTarget());
    // Every possible single-node next pointer terminates, including cycles.
    c[6]=0x10; c[0x34]=0x40;
    for (unsigned next=0; next<256; ++next) {
        c[0x41]=static_cast<uint8_t>(next);
        assert(decode(c, sizeof(c), s));
        assert(s.capabilityCount <= 48);
    }
    puts("PASS: PCI IDs, endpoint/BAR decoding, bounded capability walks and malformed inputs");
}
