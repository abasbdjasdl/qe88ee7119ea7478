// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/PciDataPath.hpp"
#include "../src/network/PciRxAssembly.hpp"
#include "../src/network/FirmwareProtocol.hpp"
#include "../src/network/FirmwareDmaQueue.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
namespace n=rtl8852be::network;
static void testWire(){
    n::TxInfo info{};info.pkt_size=1500;info.qsel=0x12;info.ch_dma=8;info.en_wd_info=true;info.mac_id=7;
    n::TxWire wire{};
    assert(n::encodePciTx(info,0x123,0x12340000,0x56780000,wire)==n::DescriptorStatus::ok);
    assert(wire.wdBytes==64&&n::little32(wire.wd+48)==0x8123&&n::little32(wire.wd+52)==0);
    assert(n::little32(wire.wd+56)==0x800105dc&&n::little32(wire.wd+60)==0x56780000);
    assert(n::little32(wire.bd)==0x40000040&&n::little32(wire.bd+4)==0x12340000);
    assert(n::encodePciTx(info,0,0xfffffff0,1,wire)==n::DescriptorStatus::invalidLength);
    assert(n::encodePciTx(info,0,1,0xfffffff0,wire)==n::DescriptorStatus::invalidLength);
    info.qsel=0x1a;assert(n::encodePciTx(info,0,1,1,wire)==n::DescriptorStatus::unrepresentable);
    uint8_t rxbd[8]{};assert(n::encodeRxBd(0x12345678,8192,rxbd));
    assert(n::little32(rxbd)==8192&&n::little32(rxbd+4)==0x12345678);
    assert(!n::encodeRxBd(0x100000000ULL,8192,rxbd));
    uint8_t release[4];n::store32(release,0x81235207);n::ReleaseReport report;
    assert(n::decodeRelease(release,4,report)&&report.page==0x123&&report.qsel==0x12&&report.status==2&&report.macid==7&&report.polluted);
    for(unsigned i=0;i<4;++i)assert(!n::decodeRelease(release,i,report));
}
static void testOwnership(){
    n::TxOwnership<4> q;using Q=n::QueueStatus;
    n::TxOwnership<4>::Ticket tickets[4]{};n::TxOwnership<4>::Completion completion;
    int cookies[4]{};
    for(unsigned i=0;i<3;++i){assert(q.peek(tickets[i])==Q::ok);assert(q.commit(tickets[i],0,7,&cookies[i])==Q::ok);}
    assert(q.peek(tickets[3])==Q::full);assert(q.consumedTo(4)==Q::invalid);
    // Report arrives before consumer index: no premature release.
    assert(q.report({tickets[0].page,0,7,0,false})==Q::ok);
    assert(q.take(tickets[0].page,completion)==Q::notReady);
    assert(q.report({tickets[0].page,0,7,0,false})==Q::duplicate);
    assert(q.report({tickets[1].page,0,8,0,false})==Q::invalid);
    assert(q.consumedTo(2)==Q::ok);
    assert(q.take(tickets[0].page,completion)==Q::ok&&completion.cookie==&cookies[0]);
    assert(q.take(tickets[0].page,completion)==Q::notReady);
    // Index arrives before report: DMA data ownership is still retained.
    assert(q.take(tickets[1].page,completion)==Q::notReady);
    assert(q.report({tickets[1].page,0,7,1,true})==Q::ok);
    assert(q.take(tickets[1].page,completion)==Q::ok&&completion.status==1&&completion.polluted);
    assert(q.peek(tickets[3])==Q::ok);assert(q.commit(tickets[3],0,7,&cookies[3])==Q::ok);
    assert(q.producer()==0&&q.pendingBd()==2);
    assert(q.consumedTo(1)==Q::invalid&&q.pendingBd()==2); // impossible advance
    assert(q.consumedTo(0)==Q::ok); // wrap 2 -> 0
    unsigned releases=0;q.reclaimAfterDmaStopped([&](void *p){assert(p==&cookies[2]||p==&cookies[3]);++releases;});
    assert(releases==2&&!q.outstanding()&&!q.pendingBd());
    // Repeated wrap and both completion orders; each cookie returned once.
    for(unsigned i=0;i<100000;++i){
        n::TxOwnership<4>::Ticket t;assert(q.peek(t)==Q::ok);assert(q.commit(t,0,7,&cookies[0])==Q::ok);
        if(i&1){assert(q.consumedTo(t.nextProducer)==Q::ok);assert(q.take(t.page,completion)==Q::notReady);}
        assert(q.report({t.page,0,7,uint8_t(i%4),false})==Q::ok);
        if(!(i&1))assert(q.consumedTo(t.nextProducer)==Q::ok);
        assert(q.take(t.page,completion)==Q::ok&&completion.cookie==&cookies[0]&&completion.status==i%4);
    }
}
static std::vector<uint8_t> segment(bool first,bool last,size_t payload,size_t total=0){
    std::vector<uint8_t> v(4+(first?16:0)+payload);
    n::store32(v.data(),uint32_t(v.size())|(first?0x8000:0)|(last?0x4000:0));
    if(first)n::store32(v.data()+4,uint32_t(total?total:payload));
    for(size_t i=first?20:4;i<v.size();++i)v[i]=uint8_t(i);
    return v;
}
static void testRx(){
    n::PciRxAssembly assembly;n::AssembledRx out;using A=n::AssemblyStatus;
    auto first=segment(true,false,50,100);auto last=segment(false,true,50);
    assert(assembly.feed(first.data(),first.size(),out)==A::incomplete&&!out.data);
    assert(assembly.feed(last.data(),last.size(),out)==A::complete&&out.packet.length==100);
    assert(!assembly.pending()&&out.bytes==120);
    assert(!std::memcmp(out.packet.payload,first.data()+20,50));
    assert(!std::memcmp(out.packet.payload+50,last.data()+4,50));
    assert(assembly.feed(last.data(),last.size(),out)==A::invalid&&!out.data);
    assert(assembly.feed(first.data(),first.size(),out)==A::incomplete);
    assert(assembly.feed(first.data(),first.size(),out)==A::invalid&&!assembly.pending());
    auto single=segment(true,true,100);
    for(size_t i=0;i<single.size();++i){assert(assembly.feed(single.data(),i,out)==A::invalid&&!out.data);}
    assert(assembly.feed(single.data(),single.size(),out)==A::complete);
    auto early=segment(false,true,49);assembly.feed(first.data(),first.size(),out);
    assert(assembly.feed(early.data(),early.size(),out)==A::invalid);
    auto over=segment(false,true,51);assembly.feed(first.data(),first.size(),out);
    assert(assembly.feed(over.data(),over.size(),out)==A::invalid);
    // Long descriptors, driver-info and padding remain before payload.
    std::vector<uint8_t> longRx(4+32+6+56+28);n::store32(longRx.data(),0xc000|uint32_t(longRx.size()));
    n::store32(longRx.data()+4,0xf000c01c);
    assert(assembly.feed(longRx.data(),longRx.size(),out)==A::complete&&out.packet.offset==98&&out.packet.length==28);
    uint32_t seed=0x8852be;
    for(unsigned i=0;i<100000;++i){for(auto &b:single){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;b=uint8_t(seed);}
        auto status=assembly.feed(single.data(),i%single.size(),out);
        if(status==A::complete)assert(out.packet.offset<=out.bytes&&out.packet.length<=out.bytes-out.packet.offset);
        else assert(!out.data&&!out.packet.payload);
    }
}
static void testFirmware(){
    uint8_t output[32]{};size_t size;
    assert(n::encodeRole({7,2,1,1},0,output,sizeof(output),size)&&size==12);
    assert(n::little32(output)==0x421&&n::little32(output+4)==0xc00c&&n::little32(output+8)==0x2607);
    assert(n::encodeRole({7,2,1,1},1,output,sizeof(output),size)&&n::little32(output+4)==0x800c);
    assert(n::encodeJoin({7,0,1,2,2,1,2,false,true},255,output,sizeof(output),size));
    assert(n::little32(output)==0xff000021&&n::little32(output+8)==0x86401407);
    for(size_t cap=0;cap<12;++cap){memset(output,0xab,sizeof(output));assert(!n::encodeRole({7,2,1,1},0,output,cap,size)&&!size);for(auto b:output)assert(b==0xab);}
    uint8_t c2h[12]{};n::store32(c2h,0x101);n::store32(c2h+4,12);n::store32(c2h+8,0x79000421);
    n::FirmwareEvent event;n::FirmwareAck ack;
    assert(n::decodeC2h(c2h,12,event)&&n::decodeAck(event,ack)&&ack.done&&ack.sequence==0x79&&ack.returnCode==0&&ack.command.function==4);
    n::store32(c2h+8,0x79020421);assert(n::decodeC2h(c2h,12,event)&&n::decodeAck(event,ack)&&ack.returnCode==2);
    n::store32(c2h,1);n::store32(c2h+8,0x00790421);
    assert(n::decodeC2h(c2h,12,event)&&n::decodeAck(event,ack)&&!ack.done&&ack.sequence==0x79);
    for(size_t cap=0;cap<12;++cap)assert(!n::decodeC2h(c2h,cap,event));
    n::store32(c2h+4,7);assert(!n::decodeC2h(c2h,12,event));
}
static void testFirmwareDma(){
    uint8_t command[12],dma[36],bd[8];size_t size;
    assert(n::encodeRole({7,2,1,1},0,command,sizeof(command),size));
    assert(n::encodeCommandDma(command,12,0x12340000,dma,sizeof(dma),bd,size)&&size==36);
    assert(n::little32(dma)==0x000c0000&&n::little32(dma+8)==12&&n::little32(dma+12)==0);
    assert(!std::memcmp(dma+24,command,12)&&n::little32(bd)==0x40000024&&n::little32(bd+4)==0x12340000);
    assert(!n::encodeCommandDma(dma,12,0x12340000,dma,sizeof(dma),bd,size));
    n::FirmwareDmaOwnership<16> q;int cookies[16];unsigned released=0;
    auto release=[&](void *p){assert(p>=static_cast<void *>(cookies)&&p<static_cast<void *>(cookies+16));++released;};
    for(unsigned i=0;i<8;++i){assert(q.commit(uint16_t(q.producer()),&cookies[i]));assert(q.consumeTo(q.producer(),release));assert(!released);}
    assert(q.retained()==8&&!q.pending());
    for(unsigned i=8;i<100008;++i){auto slot=q.producer();assert(q.commit(uint16_t(slot),&cookies[slot]));
        assert(q.consumeTo(q.producer(),release)&&q.retained()==8&&released==i-7);}
    q.reclaimAfterDmaStopped(release);assert(released==100008&&!q.retained()&&!q.pending());
    for(unsigned i=0;i<15;++i)assert(q.commit(uint16_t(i),&cookies[i]));
    assert(q.full()&&!q.commit(15,&cookies[15])&&!q.consumeTo(16,release));
    assert(q.consumeTo(15,release)&&q.retained()==8);
    q.reclaimAfterDmaStopped(release);
}
int main(){testWire();testOwnership();testRx();testFirmware();testFirmwareDma();puts("PASS: PCI TX wire format; 100000 ownership cycles; RX segment assembly/truncation/fuzz; AX H2C/C2H; 100008 CH12 multi-tag retention cycles");}
