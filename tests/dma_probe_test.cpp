// SPDX-License-Identifier: BSD-3-Clause
#include "../src/DmaProbe.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace rtl8852be::dma;
struct Backend {
    uint16_t cmd=0;
    uint8_t data[2][pageBytes]{};
    bool allocated[2]{},ready[2]{};
    int failAllocate=-1,failPrepare=-1,failSync=-1,failClose=-1;
    bool corrupt=false;
    Mapping maps[2]{{0x100000,4096,4096,1},{0x200000,4096,4096,1}};
    std::vector<unsigned> closed;
    uint16_t command(){return cmd;}
    bool allocate(unsigned i){if(static_cast<int>(i)==failAllocate)return false;allocated[i]=true;return true;}
    bool prepare(unsigned i){assert(allocated[i]);ready[i]=true;return static_cast<int>(i)!=failPrepare;}
    Mapping mapping(unsigned i){assert(ready[i]);return maps[i];}
    uint8_t *bytes(unsigned i){assert(allocated[i]);return data[i];}
    bool synchronize(unsigned i){assert(ready[i]);if(corrupt)data[i][2045]^=1;return static_cast<int>(i)!=failSync;}
    bool close(unsigned i){closed.push_back(i);ready[i]=allocated[i]=false;return static_cast<int>(i)!=failClose;}
    uint32_t lastError(){return 0;}
};
void clean(const Backend &b){assert(!b.allocated[0]&&!b.allocated[1]&&!b.ready[0]&&!b.ready[1]);assert(b.closed==std::vector<unsigned>({1,0}));}
int main(){
    uint8_t bd[8];assert(encodeBd(bd,8,0x12345000,2044));
    const uint8_t expected[]={0xfc,0x07,0,0x40,0,0x50,0x34,0x12};
    for(unsigned i=0;i<8;++i)assert(bd[i]==expected[i]);
    for(uint64_t address:{0ULL,0x100000000ULL,0xfffffff0ULL}){
        assert(!encodeBd(bd,8,address,2044));for(auto byte:bd)assert(byte==0);
    }
    assert(!encodeBd(bd,7,0x100000,32));assert(!encodeBd(bd,8,0x100000,0));
    Backend b;auto r=probe(b);assert(r.status==Status::validated&&r.cleanupOk&&r.cpuVerified&&r.synchronized==2);clean(b);
    for(int stage=0;stage<4;++stage)for(int slot=0;slot<2;++slot){
        b=Backend{};
        if(stage==0)b.failAllocate=slot;
        if(stage==1)b.failPrepare=slot;
        if(stage==2)b.failSync=slot;
        if(stage==3)b.failClose=slot;
        r=probe(b);assert(r.status!=Status::validated);clean(b);
        if(stage==3)assert(r.operationStatus==Status::validated&&!r.cleanupOk);
    }
    for(unsigned variant=0;variant<6;++variant){
        b=Backend{};
        switch(variant){
        case 0:b.maps[1].address=0x100000000ULL;break;
        case 1:b.maps[1].address+=1;break;
        case 2:b.maps[1].length=2048;break;
        case 3:b.maps[1].segments=2;break;
        case 4:b.maps[1].offset=2048;break;
        case 5:b.maps[1].address=0;break;
        }
        r=probe(b);assert(r.status==Status::mapInvalid);clean(b);
    }
    b=Backend{};b.maps[1].address=b.maps[0].address;r=probe(b);assert(r.status==Status::overlap);clean(b);
    b=Backend{};b.corrupt=true;r=probe(b);assert(r.status==Status::verifyFailed);clean(b);
    b=Backend{};b.cmd=4;r=probe(b);assert(r.status==Status::skipped&&b.closed.empty()&&!r.allocations);
    assert(validMapping({0xfffff000,4096,4096,1}));
    puts("PASS: DMA address boundaries, BD bytes, partial allocation/prepare, synchronization and cleanup failures");
}
