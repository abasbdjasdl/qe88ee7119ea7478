// SPDX-License-Identifier: BSD-3-Clause
#include <compat.h>
#include "../src/network/NetworkDescriptors.hpp"
#include "../src/network/Net80211PacketBridge.hpp"
#include <cassert>
#include <cstdio>
bool bridgeFailAlloc=false,bridgeFailCopy=false;
unsigned bridgeFreed=0,bridgeInputs=0;
std::vector<uint8_t> bridgeInputBytes;
using namespace rtl8852be::network;
int main(){
    ieee80211com ic;ic.ic_channels[1].ic_freq=2412;
    // A synthetic local EAPOL-Key envelope; never uses production captures.
    const size_t frameSize=24+8+99;
    std::vector<uint8_t> dma(4+16+frameSize+4);
    const auto put=[&](unsigned offset,uint32_t v){for(unsigned i=0;i<4;++i)dma[offset+i]=uint8_t(v>>(8*i));};
    put(4,uint32_t(frameSize+4));dma[20]=8;dma[21]=2;
    const uint8_t llc[]={0xaa,0xaa,3,0,0,0,0x88,0x8e};memcpy(dma.data()+20+24,llc,8);
    dma[20+32]=2;dma[20+33]=3;dma[20+35]=95;
    RxDeliveryTrace trace;
    assert(deliverRealtekRx(&ic,dma.data(),dma.size(),4,1,50,&trace)==0);
    assert(trace.stage==13&&trace.packetLength==frameSize&&trace.firstLength==frameSize);
    assert(bridgeInputs==1&&bridgeFreed==1&&bridgeInputBytes.size()==frameSize);
    assert(memcmp(bridgeInputBytes.data(),dma.data()+20,frameSize)==0);
    put(16,4);assert(deliverRealtekRx(&ic,dma.data(),dma.size(),4,1,50,&trace)==EOPNOTSUPP&&trace.stage==3);
    put(16,6);assert(deliverRealtekRx(&ic,dma.data(),dma.size(),4,1,50,&trace)==0&&trace.stage==13);
    put(16,0x406);assert(deliverRealtekRx(&ic,dma.data(),dma.size(),4,1,50,&trace)==EINVAL&&trace.stage==2);
    put(16,0);
    assert(deliverRealtekRx(&ic,dma.data(),dma.size(),4,0,50,&trace)==EINVAL&&trace.stage==7);
    bridgeFailAlloc=true;assert(deliverRealtekRx(&ic,dma.data(),dma.size(),4,1,50,&trace)==ENOBUFS&&trace.stage==8);bridgeFailAlloc=false;
    bridgeFailCopy=true;assert(deliverRealtekRx(&ic,dma.data(),dma.size(),4,1,50,&trace)==ENOBUFS&&trace.stage==9);bridgeFailCopy=false;
    assert(bridgeInputs==2&&bridgeFreed==3);
    puts("Packet bridge: synthetic EAPOL bytes/length/FCS, software fallback, hardware/error rejection and allocation/copy ownership passed (fake mbuf; not live net80211).");
}
