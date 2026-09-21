// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/NetworkDescriptors.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
namespace n=rtl8852be::network;
static void put(std::vector<uint8_t>&v,size_t p,uint32_t x){for(unsigned j=0;j<4;++j)v[p+j]=x>>(8*j);}
int main(){
    n::TxInfo tx{};tx.pkt_size=1500;tx.en_wd_info=true;tx.ch_dma=2;tx.qsel=4;tx.mac_id=7;
    tx.hdr_llc_len=12;tx.seq=0xabc;tx.use_rate=true;tx.data_rate=4;tx.port=1;
    uint8_t out[50];memset(out,0xaa,sizeof(out));size_t len=0;
    assert(n::encodeTx(tx,out+1,48,len)==n::DescriptorStatus::ok&&len==48);
    assert(out[0]==0xaa&&out[49]==0xaa);
    // Golden little-endian words from the 8852B TXWD bit layout.
    assert(n::little32(out+1)==0x00426000&&n::little32(out+9)==0x070805dc);
    assert(n::little32(out+13)==0xabc&&n::little32(out+25)==0x40040010);
    assert(n::little32(out+41)==0x88000000&&n::little32(out+5)==0);
    for(size_t cap=0;cap<48;++cap){memset(out,0xaa,sizeof(out));assert(n::encodeTx(tx,out,cap,len)==n::DescriptorStatus::truncated&&len==0);for(auto b:out)assert(b==0xaa);}
    tx.seq=4096;assert(n::encodeTx(tx,out,50,len)==n::DescriptorStatus::unrepresentable);tx.seq=0;
    tx.mac_id=128;assert(n::encodeTx(tx,out,50,len)==n::DescriptorStatus::unrepresentable);tx.mac_id=0;
    tx.pkt_size=16384;assert(n::encodeTx(tx,out,50,len)==n::DescriptorStatus::unrepresentable);tx.pkt_size=0;
    assert(n::encodeTx(tx,out,50,len)==n::DescriptorStatus::invalidLength);
    tx.pkt_size=10;tx.en_wd_info=false;assert(n::encodeTx(tx,out,50,len)==n::DescriptorStatus::ok&&len==24);
    assert(n::encodeTx(tx,nullptr,50,len)==n::DescriptorStatus::nullPointer);
    // Unaligned descriptor, long form, 6-byte shift, 56-byte driver info.
    const size_t base=5,offset=base+32+6+56,payload=63;
    std::vector<uint8_t> rx(offset+payload);put(rx,base,0xf000c03f);put(rx,base+4,0x80040000);
    put(rx,base+8,0x12345678);put(rx,base+16,2);put(rx,base+20,0x13070905);
    n::RxPacket packet;
    assert(n::decodeRx(rx.data(),rx.size(),base,packet)==n::DescriptorStatus::ok);
    assert(packet.payload==rx.data()+offset&&packet.length==payload&&packet.info.bw==2);
    assert(packet.info.data_rate==4&&packet.info.free_run_cnt==0x12345678&&packet.info.mac_id==7&&packet.info.addr_cam_valid);
    for(size_t cap=0;cap<rx.size();++cap){assert(n::decodeRx(rx.data(),cap,base,packet)!=n::DescriptorStatus::ok);assert(!packet.payload&&!packet.length&&!packet.info.ready);}
    put(rx,base+12,0x200);assert(n::decodeRx(rx.data(),rx.size(),base,packet)==n::DescriptorStatus::corruptFrame&&!packet.payload);
    put(rx,base+12,0x400);assert(n::decodeRx(rx.data(),rx.size(),base,packet)==n::DescriptorStatus::corruptFrame);
    assert(n::decodeRx(rx.data(),rx.size(),SIZE_MAX,packet)==n::DescriptorStatus::truncated);
    assert(n::decodeRx(nullptr,rx.size(),0,packet)==n::DescriptorStatus::nullPointer);
    std::vector<uint8_t> shortRx(20);put(shortRx,0,0x0a000004);
    assert(n::decodeRx(shortRx.data(),20,0,packet)==n::DescriptorStatus::ok&&packet.info.pkt_type==10&&packet.offset==16);
    // Deterministic malformed DMA-buffer fuzz: accepted slices stay in bounds.
    uint32_t seed=0x8852b;
    for(unsigned i=0;i<100000;++i){
        for(auto &b:rx){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;b=uint8_t(seed);}
        size_t cap=i%rx.size(),start=(i/13)%rx.size();
        const auto s=n::decodeRx(rx.data(),cap,start,packet);
        if(s==n::DescriptorStatus::ok)assert(packet.offset<=cap&&packet.length<=cap-packet.offset&&packet.payload==rx.data()+packet.offset);
        else assert(!packet.payload&&!packet.length);
    }
    puts("PASS: reference-derived 8852B TX golden words, RX bounds/types/errors, every truncation, 100000 malformed DMA buffers");
}
