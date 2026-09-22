// SPDX-License-Identifier: BSD-3-Clause
// AX/8852B algorithms from rtw89 d1fced1b8a741dc9f92b47c69489c24385945f6e.
#pragma once
#include "NetworkDescriptors.hpp"
#include "ChannelGeometry.hpp"
namespace rtl8852be { namespace network {
struct MacProtocolPeer {
    uint16_t beaconInterval{},basicRates{},txop{};uint8_t dtimPeriod{},aifsn{},ecwMin{},ecwMax{};
    bool valid{},shortSlot{},shortPreamble{},useProtection{};
};
namespace stationio {
inline int hardwareRate(uint8_t halfMbps){
    constexpr uint8_t rates[12]={2,4,11,22,12,18,24,36,48,72,96,108};
    for(int i=0;i<12;++i)if(rates[i]==halfMbps)return i;return -1;
}
// Header is the ORIGINAL already-encrypted 802.11 frame, never Ethernet.
// Caller copies only 24 bytes from a checked mbuf; length is the full MPDU.
inline bool legacyTx(const uint8_t *header,size_t headerBytes,size_t frameBytes,uint8_t macid,
                     channel::Channel channel,uint16_t basicRates,TxInfo &out,unsigned &ring){
    out={};ring=9;
    if(!header||headerBytes<24||frameBytes<24||frameBytes>16383||macid>=128||channel.width||
       !channel::validChannel(channel)||(header[0]&3)||(basicRates&0xf000))return false;
    const uint8_t type=header[0]&0x0c,subtype=header[0]&0xf0;
    if(type!=0&&type!=8)return false;
    // This owner negotiates no QoS, 4-address, fragments or HT Control.
    if((type==8&&(subtype&0x80))||(header[1]&3)==3||(header[1]&0x84)||(header[22]&0x0f))return false;
    const bool management=type==0||(type==8&&subtype==0x40);
    unsigned rate=channel.band?4:0;
    uint16_t allowed=uint16_t(basicRates&(channel.band?0xff0:(channel.primary==14?0x00f:0xfff)));
    if(basicRates&&!allowed)return false;
    if(allowed){rate=0;while(!(allowed&(1u<<rate)))++rate;}
    out.pkt_size=uint16_t(frameBytes);out.mac_id=macid;out.seq=uint16_t((uint16_t(header[23])<<4)|(header[22]>>4));
    out.is_bmc=(header[4]&1)!=0;out.wd_page=true;out.en_wd_info=true;
    out.use_rate=true;out.dis_data_fb=true;out.data_rate=uint16_t(rate);out.data_retry_lowest_rate=uint16_t(rate);
    // Low fixed rate applies to all legacy traffic including software EAPOL/CCMP.
    if(management){out.qsel=0x12;out.ch_dma=8;out.hw_ssn_sel=1;out.hw_seq_mode=1;ring=4;}
    else{out.qsel=0;out.ch_dma=0;out.hdr_llc_len=12;ring=0;}
    return true;
}
// rtw89_fw_h2c_set_edca and rtw89_aifsn_to_aifs; caller submits DONE-ACK
// command {category=1,class=9,function=0x0f} on the shared firmware bus.
inline bool edcaPayload(channel::Channel channel,const MacProtocolPeer &peer,uint8_t ac,uint8_t (&out)[12]){
    for(auto &b:out)b=0;
    if(channel.width||!channel::validChannel(channel)||ac>3||!peer.aifsn||peer.aifsn>15||
       peer.ecwMin>15||peer.ecwMax>15||peer.ecwMin>peer.ecwMax||peer.txop>0x7ff)return false;
    const unsigned aifs=peer.aifsn*(peer.shortSlot?9:20)+(channel.band?16:10);
    if(aifs>255)return false;
    out[0]=uint8_t(ac<<5); // MAC0, WMM0, selector0
    const uint32_t word=(uint32_t(peer.txop)<<16)|(uint32_t(peer.ecwMax)<<12)|(uint32_t(peer.ecwMin)<<8)|aifs;
    for(unsigned i=0;i<4;++i)out[4+i]=uint8_t(word>>(8*i));return true;
}
struct PhySample {
    int dbm{},normalized{};uint8_t rawA{},rawB{},ppduCount{},channel{},band{};
    bool channelKnown{};
};
inline bool phySample(const RxPacket &packet,PhySample &out){
    out={};if(packet.info.pkt_type!=1||packet.info.bb_sel||!packet.payload)return false;
    const uint8_t *bytes=packet.payload;size_t length=packet.length;
    if(packet.info.mac_info_valid){
        if(length<8)return false;const uint32_t a=little32(bytes),b=little32(bytes+4);
        const unsigned users=a&15;if(users>4)return false;
        const size_t offset=8+((users+1)&~1u)*4+((a&(1u<<29))?96:0)+((b>>16)&255)*8;
        if(offset>length)return false;bytes+=offset;length-=offset;
    }
    if(length<8)return false;const uint32_t a=little32(bytes),b=little32(bytes+4);
    if(!(a&0x80)||((a>>8)&255)*8!=length||(a&31)<11)return false;
    constexpr uint8_t sizes[32]={16,32,24,24,8,8,8,8,255,8,255,176,255,255,255,255,255,255,16,24,255,255,255,0,24,24,24,24,32,32,32,32};
    PhySample result{};result.rawA=uint8_t(b);result.rawB=uint8_t(b>>8);
    result.dbm=int((result.rawA>result.rawB?result.rawA:result.rawB)>>1)-110;
    result.normalized=result.dbm+100;if(result.normalized<0)result.normalized=0;if(result.normalized>100)result.normalized=100;
    result.ppduCount=packet.info.ppdu_cnt;
    for(size_t offset=8;offset<length;){
        if(length-offset<4)return false;const uint32_t word=little32(bytes+offset);const unsigned kind=word&31;
        const size_t size=sizes[kind]==255?((word>>5)&127)*8:sizes[kind];
        if(!size||size>length-offset)return false;
        if(kind==1){const unsigned encoded=(word>>16)&255,index=encoded>>4,delta=encoded&15;
            if(encoded){
                if(!index){result.band=0;result.channel=uint8_t(delta);}
                else if(index>=2&&index<=5){constexpr uint8_t base[4]={36,100,132,149};result.band=1;result.channel=uint8_t(base[index-2]+2*delta);}
                else return false; // 8852B has no 6 GHz radio.
                if(!channel::validChannel({result.band,0,result.channel,result.channel}))return false;
                result.channelKnown=true;
            }
        }
        offset+=size;
    }
    out=result;return true;
}
} } }
