// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/StationTables.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <utility>
namespace t=rtl8852be::station::tables;
namespace s=rtl8852be::station;
namespace n=rtl8852be::network;
static t::Cam cam(){t::Cam c{};c.macid=7;c.port=2;c.addressIndex=17;c.bssidIndex=3;
    c.local={{2,0x11,0x22,0x33,0x44,0x55}};c.bssid={{2,0xaa,0xbb,0xcc,0xdd,0xee}};return c;}
static void words(const t::Payload &p,const uint32_t *expected,unsigned count){
    assert(p.length==count*4);for(unsigned i=0;i<count;++i){if(n::little32(p.bytes+4*i)!=expected[i]){
        printf("word %u: got %08x expected %08x\n",i,n::little32(p.bytes+4*i),expected[i]);assert(false);}}
    for(unsigned i=count*4;i<68;++i)assert(p.bytes[i]==0);
}
static void golden(){
    t::Payload p;assert(t::defaultCmac({7,3},p));
    const uint32_t defaults[17]={0x87,0,0,0,0,0,0x00430000,0,0,0,0,0,0,0x00000e00,0xffff0000,0x0f0c0000,0};
    words(p,defaults,17);assert(n::sameCommand(p.id,t::cmacCommand));
    for(unsigned antenna=0;antenna<4;++antenna){assert(t::defaultCmac({7,uint8_t(antenna)},p));
        assert(n::little32(p.bytes+24)==((antenna?antenna:2)<<16|(antenna==3?1U<<22:0)));}
    t::AssociationCmac a{};a.macid=7;a.port=3;a.band=1;a.stationPresent=true;a.he.present=true;
    a.he.rxNss=2;a.he.txNss=2;a.he.phy[9]=0x80;assert(t::associationCmac(a,p));
    const uint32_t association[17]={0x87,0x06000000,0x40000000,0,0,3,0x2000,0x80a20000,0x20000000,
        0x06000000,0xf0000800,0x40,0,7,0x2000,0xc0f20000,0x30000000};
    words(p,association,17);
    a.he={};a.stationPresent=false;a.band=0;assert(t::associationCmac(a,p));
    assert(n::little32(p.bytes+8)==0&&n::little32(p.bytes+24)==0&&n::little32(p.bytes+56)==0);
    assert(n::little32(p.bytes+28)==0x20000&&n::little32(p.bytes+60)==0xc0f20000);
    auto c=cam();c.connected=true;c.aid=0x345;c.bssColor=17;c.trigger=true;c.lsigTxop=true;c.target=5;c.frameTarget=6;
    assert(t::addressCam(c,p));
    const uint32_t connected[15]={0,0x00400011,0xec130005,3,0x33221102,0xaa025544,0xeeddccbb,0,
        0x3500d207,0x00020345,0,0,0x00080003,0xaa0211fd,0xeeddccbb};
    words(p,connected,15);assert(n::sameCommand(p.id,t::camCommand));
    c.valid=false;assert(t::addressCam(c,p));assert(n::little32(p.bytes+8)==0xec130004&&n::little32(p.bytes+52)==0xaa0211fc);
    c=cam();c.nontransmitted=true;assert(t::addressCam(c,p));assert((n::little32(p.bytes+52)&0xff)==0x7d);
    assert(n::little32(p.bytes+36)==0x20000); // software crypto, zero AID/key map
    for(unsigned i=40;i<48;++i)assert(!p.bytes[i]);
}
static void setBits(uint8_t *p,unsigned bit,unsigned length,unsigned value){
    for(unsigned i=0;i<length;++i)if(value&(1U<<i))p[(bit+i)/8]|=uint8_t(1U<<((bit+i)%8));
}
static void padding(){
    for(unsigned nominal=0;nominal<4;++nominal){t::HeCapabilities he{};he.present=true;he.rxNss=2;he.txNss=2;he.phy[9]=nominal<<6;
        uint8_t out[4];assert(t::packetPadding(he,out));for(auto p:out)assert(p==nominal);}
    unsigned cases=0;
    for(unsigned bitmap=1;bitmap<16;++bitmap)for(unsigned streams=1;streams<=8;++streams){
        t::HeCapabilities he{};he.present=true;he.phy[6]=0x80;he.ppe[0]=(bitmap<<3)|(streams-1);
        uint8_t expected[8][4]{};unsigned bit=7;
        for(unsigned stream=0;stream<streams;++stream)for(unsigned ru=0;ru<4;++ru){
            if(!(bitmap&(1U<<ru))){expected[stream][ru]=1;continue;}
            const unsigned select=(stream+ru)%3;
            const unsigned ppe16=select==0?7:3,ppe8=select==1?3:7;
            setBits(he.ppe,bit,3,ppe16);setBits(he.ppe,bit+3,3,ppe8);bit+=6;
            expected[stream][ru]=select==0?0:(select==1?1:2);
        }
        he.ppeLength=(bit+7)/8;
        for(unsigned rx=1;rx<=8;++rx)for(unsigned tx=1;tx<=2;++tx){
            he.rxNss=rx;he.txNss=tx;const unsigned selected=(rx<tx?rx:tx)-1;uint8_t pads[4];
            if(selected>=streams){assert(!t::packetPadding(he,pads));continue;}
            assert(t::packetPadding(he,pads));for(unsigned ru=0;ru<4;++ru)assert(pads[ru]==expected[selected][ru]);++cases;
            const auto size=he.ppeLength;for(unsigned truncated=0;truncated<size;++truncated){he.ppeLength=truncated;assert(!t::packetPadding(he,pads));}he.ppeLength=size;
        }
    }
    t::HeCapabilities he{};uint8_t pads[4];assert(t::packetPadding(he,pads));for(auto p:pads)assert(p==0);
    he.present=true;assert(!t::packetPadding(he,pads));he.rxNss=1;he.txNss=3;assert(!t::packetPadding(he,pads));
    he.txNss=2;he.ppeLength=26;assert(!t::packetPadding(he,pads));he.ppeLength=1;he.phy[6]=0x80;assert(!t::packetPadding(he,pads));
    printf("station PPE capability cases: %u\n",cases);
}
static void camHashesAndBounds(){
    for(unsigned mask=0;mask<64;++mask)for(unsigned selector=0;selector<3;++selector){
        auto c=cam();c.addressMask=mask;c.maskSelect=selector;t::Payload p;assert(t::addressCam(c,p));
        unsigned first=0;if(mask)while(!(mask&(1U<<first)))++first;
        uint8_t own=0,ap=0;for(unsigned i=0;i<6;++i){if(selector!=1||i>=first)own^=c.local.bytes[i];if(selector!=2||i>=first)ap^=c.bssid.bytes[i];}
        assert(p.bytes[10]==own&&p.bytes[11]==ap);
    }
    t::Payload p;assert(!t::defaultCmac({128,3},p)&&!p.length);assert(!t::defaultCmac({1,4},p));
    t::AssociationCmac a{};a.macid=128;assert(!t::associationCmac(a,p));a={};a.port=5;assert(!t::associationCmac(a,p));
    a={};a.band=2;assert(!t::associationCmac(a,p));a={};a.he.present=true;assert(!t::associationCmac(a,p));
    for(unsigned test=0;test<15;++test){auto c=cam();switch(test){
        case 0:c.macid=128;break;case 1:c.port=5;break;case 2:c.addressIndex=128;break;case 3:c.bssidIndex=10;break;
        case 4:c.local.bytes[0]=1;break;case 5:c.bssColor=64;break;case 6:c.beaconHit=4;break;case 7:c.hitRule=4;break;
        case 8:c.addressMask=64;break;case 9:c.maskSelect=3;break;case 10:c.target=8;break;case 11:c.frameTarget=8;break;
        case 12:c.connected=true;break;case 13:c.connected=true;c.aid=2008;break;case 14:c.aid=1;break;}
        assert(!t::addressCam(c,p)&&p.length==0);
    }
    t::Plan plan;auto c=cam();assert(t::idlePlan(c,{7,3},plan)&&plan.count==2);
    assert(!t::idlePlan(c,{8,3},plan)&&plan.count==0);c.connected=true;c.aid=1;assert(!t::idlePlan(c,{7,3},plan));
    assert(t::camPlan(c,plan)&&plan.count==1);a={};assert(t::cmacPlan(a,plan)&&plan.count==1);
    // Address-byte copies must match every imported split-word source setter.
    c=cam();assert(t::addressCam(c,p));uint8_t reference[68]{};
    t::wire::FWCMD_SET_ADDR_SMA0(reference,c.local.bytes[0]);t::wire::FWCMD_SET_ADDR_SMA1(reference,c.local.bytes[1]);
    t::wire::FWCMD_SET_ADDR_SMA2(reference,c.local.bytes[2]);t::wire::FWCMD_SET_ADDR_SMA3(reference,c.local.bytes[3]);
    t::wire::FWCMD_SET_ADDR_SMA4(reference,c.local.bytes[4]);t::wire::FWCMD_SET_ADDR_SMA5(reference,c.local.bytes[5]);
    t::wire::FWCMD_SET_ADDR_TMA0(reference,c.bssid.bytes[0]);t::wire::FWCMD_SET_ADDR_TMA1(reference,c.bssid.bytes[1]);
    t::wire::FWCMD_SET_ADDR_TMA2(reference,c.bssid.bytes[2]);t::wire::FWCMD_SET_ADDR_TMA3(reference,c.bssid.bytes[3]);
    t::wire::FWCMD_SET_ADDR_TMA4(reference,c.bssid.bytes[4]);t::wire::FWCMD_SET_ADDR_TMA5(reference,c.bssid.bytes[5]);
    for(unsigned i=16;i<28;++i)assert(reference[i]==p.bytes[i]);
}
struct Transport {
    bool gate=true;uint64_t now=1;unsigned failAt=0,calls=0;std::vector<std::vector<uint8_t>> packets;
    bool inGate(){return gate;}uint64_t nowUs(){return now;}
    bool publish(const uint8_t *p,size_t len){++calls;if(calls==failAt)return false;packets.emplace_back(p,p+len);return true;}
};
using Bus=n::FirmwareCommands<Transport>;using Programmer=t::Programmer<Transport>;
struct Owner {unsigned callbacks=0;bool success=false,reject=false;s::Token token{};
    static bool finished(void *p,s::Token token,bool ok){auto &o=*static_cast<Owner*>(p);++o.callbacks;o.success=ok;o.token=token;return !o.reject;}
    t::Completion completion(){return {this,finished};}
};
static n::CommandEvent ack(Bus &bus,const std::vector<uint8_t> &packet,bool done=true,uint8_t result=0,uint64_t epoch=9){
    uint8_t payload[4]{};n::store32(payload,(n::little32(packet.data())&0xffff)|
        (done?(uint32_t(result)<<16)|(uint32_t(packet[3])<<24):uint32_t(packet[3])<<16));
    return bus.accept({{1,0,uint8_t(done)},payload,4},epoch);
}
static void programming(){
    t::Plan plan;assert(t::idlePlan(cam(),{7,3},plan));
    {Transport tr;Bus bus(tr,9);Programmer p(bus);Owner owner;assert(p.begin(plan,{9,11},owner.completion()));
     assert(p.state()==t::ProgramState::pending&&tr.packets.size()==1&&tr.packets[0].size()==68);
     const auto first=tr.packets.back();assert(n::little32(first.data())==0x19&&n::little32(first.data()+4)==0xc044);
     assert(ack(bus,first,false)==n::CommandEvent::received&&!owner.callbacks&&!p.acknowledged());
     assert(ack(bus,first,true,0,8)==n::CommandEvent::unrelated);
     assert(ack(bus,first)==n::CommandEvent::completed&&tr.packets.size()==2&&p.acknowledged()==1&&!owner.callbacks);
     const auto second=tr.packets.back();assert(second.size()==76&&n::little32(second.data())==0x01000215);
     assert(ack(bus,first)==n::CommandEvent::unrelated&&!owner.callbacks);
     assert(ack(bus,second)==n::CommandEvent::completed&&owner.callbacks==1&&owner.success&&s::same(owner.token,{9,11}));
     assert(p.acknowledged()==2&&p.state()==t::ProgramState::complete&&p.service());
     assert(ack(bus,second)==n::CommandEvent::unrelated&&owner.callbacks==1);
     assert(p.begin(plan,{9,12},owner.completion())&&bus.allocated()==3);}
    {Transport tr;Bus bus(tr,9);Programmer p(bus);Owner o;auto copy=plan;
     assert(p.begin(copy,{9,1},o.completion()));const auto original=tr.packets[0];copy.commands[1].bytes[0]=99;
     uint8_t otherSequence=0,otherPayload[1]={7};
     assert(bus.submit({2,16,3},false,true,otherPayload,1,{},55,2000000,otherSequence)&&otherSequence==1);
     const auto bt=tr.packets.back();assert(ack(bus,bt)==n::CommandEvent::completed&&!p.acknowledged());
     assert(ack(bus,original)==n::CommandEvent::completed&&p.acknowledged()==1);
     assert(tr.packets.back()[3]==2&&tr.packets.back()[8]==0x87); // copied plan, global bus sequence
     assert(ack(bus,tr.packets.back())==n::CommandEvent::completed&&o.success);}
    for(unsigned failAt=1;failAt<=2;++failAt){Transport tr;tr.failAt=failAt;Bus bus(tr,9);Programmer p(bus);Owner o;
        if(failAt==1)assert(!p.begin(plan,{9,1},o.completion())&&!o.callbacks);
        else{assert(p.begin(plan,{9,1},o.completion()));assert(ack(bus,tr.packets[0])==n::CommandEvent::fault&&o.callbacks==1&&!o.success);}
        assert(p.state()==t::ProgramState::failed&&bus.faulted());assert(!p.begin(plan,{9,2},o.completion()));}
    for(unsigned reject=0;reject<2;++reject){Transport tr;Bus bus(tr,9);Programmer p(bus);Owner o;
        assert(p.begin(plan,{9,1},o.completion()));if(reject)assert(ack(bus,tr.packets.back())==n::CommandEvent::completed);
        assert(ack(bus,tr.packets.back(),true,5)==n::CommandEvent::fault&&o.callbacks==1&&!o.success&&p.error()==t::ProgramError::firmware);}
    {Transport tr;Bus bus(tr,9);Programmer p(bus);Owner o;assert(p.begin(plan,{9,1},o.completion()));tr.now+=2000000;
     assert(!p.service()&&o.callbacks==1&&!o.success&&bus.error()==n::CommandError::timeout);assert(!p.service()&&o.callbacks==1);}
    {Transport tr;Bus bus(tr,9);Programmer p(bus);Owner o;assert(p.begin(plan,{9,1},o.completion()));
     assert(ack(bus,tr.packets[0])==n::CommandEvent::completed&&p.acknowledged()==1);tr.now+=2000000;
     assert(!p.service()&&o.callbacks==1&&!o.success&&p.acknowledged()==1);}
    {Transport tr;Bus bus(tr,9);Programmer p(bus);Owner o;assert(p.begin(plan,{9,1},o.completion()));
     assert(!p.cancel()&&o.callbacks==1&&!o.success&&p.error()==t::ProgramError::cancelled&&bus.faulted());
     assert(ack(bus,tr.packets.back())==n::CommandEvent::fault&&o.callbacks==1);}
    {Transport tr;Bus bus(tr,9);Programmer p(bus);Owner o;o.reject=true;assert(p.begin(plan,{9,1},o.completion()));
     assert(ack(bus,tr.packets.back())==n::CommandEvent::completed);assert(ack(bus,tr.packets.back())==n::CommandEvent::fault);
     assert(o.callbacks==1&&o.success&&p.error()==t::ProgramError::completion&&p.state()==t::ProgramState::failed);}
    {Transport tr;Bus bus(tr,9);Programmer p(bus);Owner o;assert(!p.begin(plan,{8,1},o.completion()));
     assert(!p.begin(plan,{9,0},o.completion()));assert(!p.begin(plan,{9,1},{}));auto bad=plan;bad.commands[0].length=59;
     assert(!p.begin(bad,{9,1},o.completion()));bad=plan;bad.commands[0].id={1,8,0};assert(!p.begin(bad,{9,1},o.completion()));
     bad=plan;bad.count=3;assert(!p.begin(bad,{9,1},o.completion()));assert(tr.calls==0&&bus.allocated()==0);}
}
struct Registers {
    bool gate=true,owned=true,paused=true,failDrain=false;unsigned calls=0,failAt=0,unpauseAt=0;uint64_t now=1,step=0;
    std::vector<std::pair<uint32_t,uint32_t>> writes;
    bool inGate(){return gate;}bool stationTableWindowOwned(){return owned;}bool schedulerPaused(){return paused;}
    uint64_t nowUs(){const auto result=now;now+=step;return result;}
    bool write32(uint32_t a,uint32_t v){++calls;if(calls==failAt)return false;writes.emplace_back(a,v);if(calls==unpauseAt)paused=false;return true;}
    bool drainWrites(){return !failDrain;}
};
static void seeds(){
    Registers io;auto result=t::seedMacTables(io,7);assert(result.complete&&!result.requiresReset&&result.written==17&&io.paused);
    for(unsigned i=0;i<4;++i){assert(io.writes[2*i]==std::make_pair(0xc04U,0x18800070U+4*i));assert(io.writes[2*i+1]==std::make_pair(0x40000U,0U));}
    assert(io.writes[8]==std::make_pair(0xc04U,0x188400e0U));
    const uint32_t expected[]={4,0x400a0004,0,0,0,0xe43000b,0,0xb8109};
    for(unsigned i=0;i<8;++i)assert(io.writes[9+i]==std::make_pair(0x40000U+4*i,expected[i]));
    for(unsigned fail=1;fail<=17;++fail){Registers r;r.failAt=fail;result=t::seedMacTables(r,7);
        assert(!result.complete&&result.requiresReset&&result.error==t::SeedError::write&&result.attempted==fail&&result.written==fail-1);}
    for(unsigned drop=1;drop<=17;++drop){Registers r;r.unpauseAt=drop;result=t::seedMacTables(r,7);
        assert(!result.complete&&result.requiresReset&&result.error==t::SeedError::precondition&&result.written==drop);}
    {Registers r;r.failDrain=true;result=t::seedMacTables(r,7);assert(result.error==t::SeedError::drain&&result.requiresReset&&!result.complete);}
    for(unsigned bad=0;bad<4;++bad){Registers r;if(bad==0)r.gate=false;if(bad==1)r.owned=false;if(bad==2)r.paused=false;
        result=t::seedMacTables(r,bad==3?128:7);assert(result.error==t::SeedError::precondition&&!result.requiresReset&&!r.calls);}
    {Registers r;r.step=50000;result=t::seedMacTables(r,7);assert(result.error==t::SeedError::timeout&&result.requiresReset&&r.calls==1);}
    {Registers r;r.step=UINT64_MAX;result=t::seedMacTables(r,7);assert(result.error==t::SeedError::clock&&!result.requiresReset&&!r.calls);}
    {Registers r;r.now=UINT64_MAX-1;result=t::seedMacTables(r,7);assert(result.error==t::SeedError::clock&&!r.calls);}
}
int main(){golden();padding();camHashesAndBounds();programming();seeds();puts("station table tests passed");}
