// SPDX-License-Identifier: BSD-3-Clause
#include "../src/FirmwareBank.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <array>
namespace t=rtl8852be::transport;
struct Memory {
    std::vector<std::array<uint8_t,4096>> data;
    std::vector<rtl8852be::dma::Mapping> maps;
    std::vector<unsigned> syncs;
    int failAlloc=-1,failPrep=-1,failSync=-1,overlap=-1,unaligned=-1,corrupt=-1;
    uint16_t command(){return 0;}
    explicit Memory(unsigned n):data(n),maps(n){}
    bool allocate(unsigned i){return static_cast<int>(i)!=failAlloc;}
    bool prepare(unsigned i){maps[i]={0x100000+4096u*i,4096,4096,1};
        if(static_cast<int>(i)==overlap)maps[i].address=0x100000;
        if(static_cast<int>(i)==unaligned)++maps[i].address;return static_cast<int>(i)!=failPrep;}
    rtl8852be::dma::Mapping mapping(unsigned i){return maps[i];}
    uint8_t *bytes(unsigned i){return data[i].data();}
    bool synchronize(unsigned i){syncs.push_back(i);if(static_cast<int>(i)==corrupt)data[i][4000]=1;return static_cast<int>(i)!=failSync;}
};
int main(int argc,char **argv){
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);auto size=ftell(f);rewind(f);
    std::vector<uint8_t> image(static_cast<size_t>(size));assert(fread(image.data(),1,image.size(),f)==image.size());fclose(f);
    t::Packets packets;assert(packets.initialize(image.data(),image.size(),1)==t::PacketStatus::ok);
    const unsigned n=packets.count()+1;assert(n==165);
    Memory m(n);auto r=t::prepareBank(m,packets);
    assert(r.status==t::BankStatus::ready&&r.allocated==n&&r.prepared==n&&r.synced==n&&m.syncs.back()==0);
    assert(m.data[0][164*8]==0&&m.data[0][0]==112&&m.data[0][3]==0x40);
    for(unsigned i=0;i<n;++i)for(unsigned fail=0;fail<3;++fail){
        Memory test(n);if(fail==0)test.failAlloc=i;if(fail==1)test.failPrep=i;if(fail==2)test.failSync=i;
        r=t::prepareBank(test,packets);assert(r.status!=t::BankStatus::ready&&r.failedSlot==i);
    }
    for(unsigned i=1;i<n;++i){Memory test(n);test.overlap=i;r=t::prepareBank(test,packets);assert(r.status==t::BankStatus::mappingOverlap);}
    for(unsigned i:{0u,1u,164u}){Memory test(n);test.corrupt=i;r=t::prepareBank(test,packets);assert(r.status==t::BankStatus::verifyFailed);}
    Memory test(n);test.unaligned=164;r=t::prepareBank(test,packets);assert(r.status==t::BankStatus::mappingInvalid);
    puts("PASS: 165-page real firmware bank, all allocation/prepare/sync failures, overlapping mappings, full-page verification, ring-last sync");
}
