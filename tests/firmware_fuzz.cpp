// SPDX-License-Identifier: BSD-3-Clause
#include "../src/FirmwarePlan.hpp"
#include <assert.h>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t bytes) {
    using namespace rtl8852be::firmware;
    for(uint8_t cut=0;cut<3;++cut) {
        Plan p;
        if(parse(data,bytes,cut,p)!=Status::ok) {assert(!p.valid);continue;}
        size_t covered=0;
        for(uint32_t i=0;i<p.packetCount;++i) {
            Chunk c;assert(chunkAt(p,i,c));
            assert(c.bytes && c.bytes<=packetBytes && fits(c.offset,c.bytes,bytes));
            assert(c.offset==p.imageOffset+p.headerBytes+covered);
            covered+=c.bytes;
        }
        assert(covered==p.imageBytes-p.headerBytes);
    }
    return 0;
}
