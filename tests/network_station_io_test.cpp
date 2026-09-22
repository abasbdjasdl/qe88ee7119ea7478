#include "../src/network/StationIoCore.hpp"
#include "../src/network/PciDataPath.hpp"
#include "../src/network/MacNetworkPhyWait.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace rtl8852be;
using namespace rtl8852be::network;
int main(){
    uint8_t header[24]{};header[0]=0xb0;header[4]=0x02;header[22]=0x30;header[23]=0x12;
    TxInfo info;unsigned ring;
    assert(stationio::legacyTx(header,24,50,7,{0,0,6,6},0x00f,info,ring));
    assert(ring==4&&info.qsel==0x12&&info.ch_dma==8&&info.mac_id==7&&info.seq==0x123);
    assert(info.hw_ssn_sel==1&&info.hw_seq_mode==1&&info.data_rate==0&&info.use_rate&&info.dis_data_fb&&!info.sec_en&&!info.agg_en);
    uint8_t wd[48]{};size_t length=0;assert(encodeTx(info,wd,48,length)==DescriptorStatus::ok&&length==48);
    assert((little32(wd)&0xf0000)==0x80000);assert((little32(wd+24)&0x40000400)==0x40000400);
    header[0]=0x08;header[1]=0x41; // software CCMP, ToDS, no hardware crypto
    assert(stationio::legacyTx(header,24,140,7,{1,0,36,36},0x150,info,ring));
    assert(ring==0&&info.qsel==0&&info.ch_dma==0&&info.hdr_llc_len==12&&info.data_rate==4&&info.hw_seq_mode==0&&!info.sec_en);
    header[4]=1;assert(stationio::legacyTx(header,24,140,7,{0,0,1,1},0,info,ring)&&info.is_bmc);
    for(unsigned n=0;n<24;++n)assert(!stationio::legacyTx(header,n,140,7,{0,0,1,1},0,info,ring));
    assert(!stationio::legacyTx(header,24,16384,7,{0,0,1,1},0,info,ring));
    assert(!stationio::legacyTx(header,24,140,128,{0,0,1,1},0,info,ring));
    assert(!stationio::legacyTx(header,24,140,7,{0,0,14,14},0x10,info,ring));
    assert(!stationio::legacyTx(header,24,140,7,{1,0,36,36},0x01,info,ring));
    // No QoS negotiated.
    header[0]=0x88;assert(!stationio::legacyTx(header,24,140,7,{0,0,1,1},0,info,ring));
    header[0]=0x08;header[1]=0x43;assert(!stationio::legacyTx(header,24,140,7,{0,0,1,1},0,info,ring));
    header[1]=0x45;assert(!stationio::legacyTx(header,24,140,7,{0,0,1,1},0,info,ring));
    header[1]=0x41;header[22]=1;assert(!stationio::legacyTx(header,24,140,7,{0,0,1,1},0,info,ring));
    for(unsigned n=0;n<12;++n){constexpr uint8_t rates[]={2,4,11,22,12,18,24,36,48,72,96,108};assert(stationio::hardwareRate(rates[n])==int(n));}
    assert(stationio::hardwareRate(3)==-1);
    MacProtocolPeer peer{};peer.aifsn=3;peer.ecwMin=4;peer.ecwMax=10;peer.shortSlot=true;
    uint8_t edca[12];assert(stationio::edcaPayload({0,0,1,1},peer,0,edca));assert(little32(edca)==0&&little32(edca+4)==0xa425&&little32(edca+8)==0);
    peer.shortSlot=false;assert(stationio::edcaPayload({0,0,1,1},peer,3,edca));assert(edca[0]==0x60&&little32(edca+4)==0xa446);
    peer.shortSlot=true;assert(stationio::edcaPayload({1,0,36,36},peer,0,edca)&&little32(edca+4)==0xa42b);
    peer.aifsn=0;assert(!stationio::edcaPayload({0,0,1,1},peer,0,edca));peer.aifsn=15;peer.shortSlot=false;assert(!stationio::edcaPayload({0,0,1,1},peer,0,edca));
    uint8_t ppdu[40]{};store32(ppdu,12|0x80|(5<<8));store32(ppdu+4,140|(120<<8));store32(ppdu+8,1|(0x20<<16));
    RxPacket packet{};packet.info.pkt_type=1;packet.info.ppdu_cnt=5;packet.payload=ppdu;packet.length=40;
    stationio::PhySample sample;assert(stationio::phySample(packet,sample));assert(sample.dbm==-40&&sample.normalized==60&&sample.ppduCount==5&&sample.channelKnown&&sample.channel==36&&sample.band==1);
    for(unsigned n=0;n<40;++n){packet.length=n;assert(!stationio::phySample(packet,sample));}packet.length=40;
    store32(ppdu+8,1|(6<<16));assert(stationio::phySample(packet,sample)&&sample.channel==6&&sample.band==0);
    store32(ppdu+8,1|(0x70<<16));assert(!stationio::phySample(packet,sample));
    store32(ppdu+8,23);assert(!stationio::phySample(packet,sample));
    store32(ppdu+8,8);assert(!stationio::phySample(packet,sample)); // variable IE zero length
    store32(ppdu+8,8|(4<<5));assert(stationio::phySample(packet,sample)&&!sample.channelKnown);
    store32(ppdu,12|(5<<8));assert(!stationio::phySample(packet,sample));store32(ppdu,12|0x80|(5<<8));
    unsigned combinations=0;
    uint8_t mixed[8+16+96+24+40]{};
    for(unsigned users=0;users<=4;++users)for(unsigned count=0;count<=1;++count)for(unsigned plcp=0;plcp<=3;++plcp){
        const auto offset=8+((users+1)&~1u)*4+96*count+plcp*8;
        std::memset(mixed,0,sizeof(mixed));store32(mixed,users|(count<<29));store32(mixed+4,plcp<<16);
        std::memcpy(mixed+offset,ppdu,40);packet.info.mac_info_valid=true;packet.payload=mixed;packet.length=offset+40;
        assert(stationio::phySample(packet,sample));
        packet.length=offset+7;assert(!stationio::phySample(packet,sample));++combinations;
    }
    store32(mixed,5);packet.length=sizeof(mixed);assert(!stationio::phySample(packet,sample));
    static MacNetworkPhyWait waiting;uint8_t data[4]={1,2,3,4};
    for(uint8_t id=0;id<8;++id)assert(waiting.store(id,id,data,4,100));
    for(uint8_t id=0;id<8;++id){auto *p=waiting.take(id,id,101);assert(p&&p->length==4&&!std::memcmp(p->bytes,data,4));assert(!waiting.take(id,id,102));}
    assert(!waiting.store(8,0,data,4,0));assert(!waiting.store(0,0,nullptr,4,0));assert(!waiting.store(0,0,data,MacNetworkPhyWait::capacity+1,0));
    assert(waiting.store(0,4,data,4,100)&&!waiting.take(0,5,101)&&!waiting.take(0,4,102));
    assert(waiting.store(0,4,data,4,100)&&!waiting.take(0,4,99));
    assert(waiting.store(0,4,data,4,100)&&waiting.take(0,4,250100));
    assert(waiting.store(0,4,data,4,100)&&!waiting.take(0,4,250101));
    assert(waiting.store(0,4,data,4,100));waiting.clear();assert(!waiting.take(0,4,101));
    std::printf("station IO TX/EDCA/PHY + RX-first-PPDU tests passed (%u MAC PPDU layouts)\n",combinations);
}

