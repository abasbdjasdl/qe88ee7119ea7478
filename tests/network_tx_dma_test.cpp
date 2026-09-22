// SPDX-License-Identifier: GPL-2.0-or-later
// Actual native buffer, PCI ledger and queue adapter sources; IOKit/mbuf model.
#include "network_dma_fakes/DmaFake.hpp"
#include "../src/network/MacDmaBuffer.cpp"
#include "../src/network/Net80211PciQueue.cpp"
#include "../src/network/MacTxDmaQueue.cpp"
#include <cstdio>
DmaFake fake;
struct ieee80211com{};struct ieee80211_node{};
static unsigned frames{},nodes{},released{};
namespace rtl8852be { namespace network {
void releaseTx(ieee80211com *,TxLease &lease){if(lease.frame){delete lease.frame;--frames;}if(lease.node){delete lease.node;--nodes;}lease={};++released;}
int deliverRealtekRx(ieee80211com *,const uint8_t *,size_t,size_t,uint8_t,int,RxDeliveryTrace *){return 0;}
} }
using namespace rtl8852be::network;
void empty(){assert(!fake.memories&&!fake.commands&&!fake.mappers&&!fake.memoryPrepared&&!fake.dmaPrepared&&!fake.logs&&!frames&&!nodes);}
void reset(){empty();fake={};fake.advanceBus=true;released=0;}
TxLease lease(){auto *m=new FakeMbuf;m->bytes.resize(64);for(unsigned i=0;i<64;++i)m->bytes[i]=uint8_t(i);++frames;++nodes;return {m,new ieee80211_node,64};}
IOBufferMemoryDescriptor *memory(size_t i){return static_cast<IOBufferMemoryDescriptor*>(fake.allocations[i]);}
void data(){
    reset();IOPCIDevice device;IOWorkLoop loop;ieee80211com ic;MacTxDmaQueue q;
    assert(q.allocate(&device,&loop)&&fake.memories==129&&q.attach(&ic,0)==0);
    assert(q.markDeviceVisible()&&!q.releaseBeforeAttach());
    auto l=lease();TxInfo info{};info.pkt_size=64;info.ch_dma=0;info.qsel=0;info.mac_id=1;uint16_t producer=0;
    assert(q.stage(l,info,producer)==0&&!l.frame&&producer==1);
    for(unsigned i=0;i<64;++i)assert(memory(1)->bytes[i]==i);
    assert(little32(memory(0)->bytes.data()+4)==0x140000); // WD bus address, not frame
    assert(q.completions().consumeTo(1)==0&&frames==1&&released==0);
    ReleaseReport report{};report.page=0;report.qsel=0;report.macid=1;
    assert(q.completions().releaseReport(report)==0&&frames==0&&released==1);
    assert(q.releaseAfterDmaStopped());empty();
}
void stageFailures(){
    IOPCIDevice device;IOWorkLoop loop;ieee80211com ic;
    for(unsigned step=0;step<3;++step){
        reset();MacTxDmaQueue q;assert(q.allocate(&device,&loop)&&q.attach(&ic,0)==0&&q.markDeviceVisible());
        auto l=lease();TxInfo info{};info.pkt_size=64;info.mac_id=1;uint16_t producer=88;
        fake.failSyncAt=fake.syncCalls+step;
        assert(q.stage(l,info,producer)==EIO&&!l.frame&&producer==0&&frames==1&&q.completions().outstanding()==1);
        auto second=lease();assert(q.stage(second,info,producer)==ENETDOWN&&second.frame);releaseTx(&ic,second);
        fake.failSyncAt=size_t(-1);assert(q.releaseAfterDmaStopped()&&released==2);empty();
    }
}
void firmware(){
    reset();IOPCIDevice device;IOWorkLoop loop;MacFirmwareDmaQueue q;
    assert(q.allocate(&device,&loop)&&fake.memories==65&&q.attach()==0&&q.markDeviceVisible());
    uint8_t command[16];size_t bytes=0;uint16_t producer;
    for(unsigned i=0;i<12;++i){assert(encodeRole({1,0,0,0},uint8_t(i),command,sizeof command,bytes));
        assert(q.stage(command,bytes,producer)==0&&producer==i+1&&q.consumeTo(producer)==0);
        assert(!std::memcmp(memory(i+1)->bytes.data()+24,command,bytes));
    }
    fake.failSyncAt=fake.syncCalls;assert(q.stage(command,bytes,producer)==EIO&&producer==0);
    assert(q.stage(command,bytes,producer)==ENETDOWN);fake.failSyncAt=size_t(-1);
    assert(q.releaseAfterDmaStopped());empty();
}
void allocations(){
    IOPCIDevice d;IOWorkLoop l;
    for(size_t i=0;i<129;++i){reset();fake.failAllocationAt=i;MacTxDmaQueue q;assert(!q.allocate(&d,&l));empty();}
    for(size_t i=0;i<65;++i){reset();fake.failAllocationAt=i;MacFirmwareDmaQueue q;assert(!q.allocate(&d,&l));empty();}
}
int main(){data();stageFailures();firmware();allocations();std::puts("Native TX: actual mbuf/WD/BD path, completion retention, three sync fault points, firmware multi-tag staging and all 194 bank allocation failures passed");}
