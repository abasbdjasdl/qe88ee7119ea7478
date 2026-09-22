// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/FirmwareCommands.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
namespace n=rtl8852be::network;
struct Transport {
    bool gate=true,fail{},reenter{};uint64_t time=100,advance{};unsigned calls{};
    std::vector<uint8_t> copied;void *owner{};void (*during)(void *){};
    bool inGate(){return gate;}uint64_t nowUs(){return time;}
    bool publish(const uint8_t *bytes,size_t length){++calls;copied.assign(bytes,bytes+length);
        time+=advance;if(during)during(owner);return !fail;}
};
using Bus=n::FirmwareCommands<Transport>;
struct Owner {
    unsigned callbacks{};uint64_t epoch{},token{};n::FirmwareAck ack{};bool reject{},enqueue{},invalidate{};
    Bus *bus{};uint8_t nextSequence{};
    static bool event(void *context,const n::FirmwareEvent &event,uint64_t epoch,uint64_t token){
        auto &o=*static_cast<Owner *>(context);++o.callbacks;o.epoch=epoch;o.token=token;
        assert(n::decodeAck(event,o.ack));
        if(o.invalidate)o.bus->invalidate();
        if(o.enqueue){o.enqueue=false;uint8_t payload[4]={9,8,7,6};
            assert(o.bus->submit({1,8,0},false,true,payload,4,o.receiver(),token+1,2000,o.nextSequence));}
        return !o.reject;
    }
    n::CommandReceiver receiver(){return {this,event};}
};
struct Ack {
    uint8_t payload[4]{};n::FirmwareEvent event{};
    Ack(n::CommandId id,uint8_t seq,bool done=true,uint8_t code=0){
        n::store32(payload,uint32_t(id.category)|(uint32_t(id.commandClass)<<2)|(uint32_t(id.function)<<8)|
            (done?(uint32_t(code)<<16)|(uint32_t(seq)<<24):uint32_t(seq)<<16));
        event={{1,0,uint8_t(done)},payload,4};
    }
};
constexpr n::CommandId role{1,8,4},policy{2,16,3};
int main(){
    {Transport t;Bus bus(t,42);Owner station,bt;station.bus=bt.bus=&bus;station.enqueue=true;
        uint8_t r=99,b=99,payload[4]={1,2,3,4};
        assert(bus.submit(role,false,true,payload,4,station.receiver(),100,2000,r)&&r==0);
        payload[0]=99;assert(t.copied[8]==1); // publish owns a copy
        assert(bus.submit(policy,false,true,payload,4,bt.receiver(),200,300000,b)&&b==1);
        Ack receipt(role,r,false);assert(bus.accept(receipt.event,42)==n::CommandEvent::received&&!station.callbacks);
        assert(bus.accept(receipt.event,42)==n::CommandEvent::unrelated);
        Ack wrong(role,b);assert(bus.accept(wrong.event,42)==n::CommandEvent::unrelated&&!bt.callbacks);
        Ack bd(policy,b);assert(bus.accept(bd.event,41)==n::CommandEvent::unrelated);
        assert(bus.accept(bd.event,42)==n::CommandEvent::completed&&bt.callbacks==1&&bt.token==200&&!station.callbacks);
        Ack rd(role,r);assert(bus.accept(rd.event,42)==n::CommandEvent::completed&&station.callbacks==1);
        assert(station.epoch==42&&station.token==100&&station.nextSequence==2&&bus.allocated()==3);
        assert(bus.accept(rd.event,42)==n::CommandEvent::unrelated); // callback already enqueued join
        Ack join({1,8,0},2);assert(bus.accept(join.event,42)==n::CommandEvent::completed&&station.callbacks==2&&station.token==101);
        assert(bus.record(0).state==n::CommandState::complete&&bus.record(1).state==n::CommandState::complete);
    }
    {Transport t;Bus bus(t,1);Owner o;uint8_t seq;
        assert(bus.submit(role,true,false,nullptr,0,o.receiver(),1,100,seq));
        Ack done(role,seq);assert(bus.accept(done.event,1)==n::CommandEvent::unrelated);
        Ack rx(role,seq,false);assert(bus.accept(rx.event,1)==n::CommandEvent::completed&&o.callbacks==1&&!o.ack.done);}
    {Transport t;Bus bus(t,1);Owner o;uint8_t seq;
        assert(bus.submit(role,false,true,nullptr,0,o.receiver(),1,100,seq));Ack rejected(role,seq,true,9);
        assert(bus.accept(rejected.event,1)==n::CommandEvent::fault&&o.callbacks==1&&o.ack.returnCode==9);
        assert(bus.error()==n::CommandError::firmwareRejected&&!bus.service());}
    {Transport t;Bus bus(t,1);uint8_t seq;
        assert(bus.submit(role,false,true,nullptr,0,{},0,100,seq));Ack rejected(role,seq,true,1);
        assert(bus.accept(rejected.event,1)==n::CommandEvent::fault&&bus.error()==n::CommandError::firmwareRejected);}
    for(bool invalidate:{false,true}){Transport t;Bus bus(t,1);Owner o;o.bus=&bus;o.reject=!invalidate;o.invalidate=invalidate;uint8_t seq;
        assert(bus.submit(policy,false,true,nullptr,0,o.receiver(),1,100,seq));Ack done(policy,seq);
        assert(bus.accept(done.event,1)==n::CommandEvent::fault&&bus.faulted());
        assert(bus.error()==(invalidate?n::CommandError::invalidated:n::CommandError::receiver));}
    for(bool atPublish:{false,true}){Transport t;Bus bus(t,1);Owner o;uint8_t seq;
        if(atPublish)t.advance=100;
        const bool accepted=bus.submit(role,false,true,nullptr,0,o.receiver(),1,100,seq);
        if(atPublish)assert(!accepted&&bus.error()==n::CommandError::timeout);
        else{assert(accepted);t.time+=100;Ack done(role,seq);assert(bus.accept(done.event,1)==n::CommandEvent::fault);}
        assert(!o.callbacks&&bus.error()==n::CommandError::timeout);}
    {Transport t;Bus bus(t,1);uint8_t seq;assert(bus.reserve({},0,100,seq));t.time+=100;
        assert(!bus.service()&&bus.error()==n::CommandError::timeout&&!t.calls);}
    {Transport t;Bus bus(t,1);uint8_t seq;t.fail=true;
        assert(!bus.submit(role,false,true,nullptr,0,{},0,100,seq)&&t.calls==1);
        t.fail=false;assert(!bus.submit(role,false,true,nullptr,0,{},0,100,seq)&&t.calls==1&&bus.error()==n::CommandError::transport);}
    {Transport t;Bus bus(t,1);uint8_t seq;t.gate=false;
        assert(!bus.reserve({},0,100,seq)&&!t.calls&&!bus.allocated());t.gate=true;
        assert(bus.reserve({},0,100,seq));--t.time;
        assert(!bus.service()&&bus.error()==n::CommandError::clock);}
    {Transport t;Bus bus(t,1);uint8_t seq;
        for(unsigned i=0;i<256;++i){assert(bus.submit(policy,false,true,nullptr,0,{},0,100,seq)&&seq==i);
            Ack ack(policy,seq);assert(bus.accept(ack.event,1)==n::CommandEvent::completed);}
        assert(!bus.reserve({},0,100,seq)&&bus.error()==n::CommandError::exhausted&&t.calls==256);}
    {Transport t;Bus bus(t,1);uint8_t seq;assert(bus.reserve({},0,100,seq));
        uint8_t bytes[12]{};size_t length=0;uint8_t payload[4]={0};assert(n::encodeH2c(role,seq,false,true,payload,4,bytes,12,length));
        for(unsigned bit=16;bit<24;++bit){bytes[bit/8]|=uint8_t(1U<<(bit%8));assert(!bus.publish(bytes,12));bytes[bit/8]=0;}
        for(unsigned bit=16;bit<32;++bit){bytes[4+bit/8]|=uint8_t(1U<<(bit%8));assert(!bus.publish(bytes,12));bytes[4+bit/8]=0;}
        assert(!bus.publish(bytes,11)&&!bus.publish(nullptr,12)&&!t.calls);
        assert(bus.publish(bytes,12)&&t.calls==1);assert(!bus.publish(bytes,12)&&t.calls==1);}
    {Transport t;Bus bus(t,1);uint8_t seq=77;
        assert(!bus.submit({4,0,0},false,true,nullptr,0,{},0,1,seq)&&!bus.allocated());
        assert(!bus.reserve({&t,nullptr},0,1,seq)&&!bus.allocated());
        assert(!bus.reserve({},0,0,seq)&&!bus.reserve({},0,10000001,seq));
        std::vector<uint8_t> payload(16375,0xaa);
        assert(!bus.submit(role,false,true,payload.data(),16376,{},0,100,seq)&&!bus.allocated());
        assert(bus.submit(role,false,true,payload.data(),16375,{},0,100,seq)&&t.copied.size()==16383);
        for(size_t i=8;i<t.copied.size();++i)assert(t.copied[i]==0xaa);}
    {Transport t;Bus bus(t,0);uint8_t seq;assert(!bus.reserve({},0,1,seq)&&!t.calls);}
    {Transport t;t.time=UINT64_MAX-50;Bus bus(t,1);uint8_t seq;assert(!bus.reserve({},0,100,seq)&&!t.calls);}
    {Transport t;Bus bus(t,1);uint8_t seq; // non-ACK command: sequence 1 avoids upstream periodic RACK
        assert(bus.submit(role,false,true,nullptr,0,{},0,100,seq));Ack ack(role,seq);assert(bus.accept(ack.event,1)==n::CommandEvent::completed);
        assert(bus.submit({2,8,0},false,false,nullptr,0,{},0,100,seq)&&seq==1&&bus.record(seq).state==n::CommandState::complete);
        t.time+=1000;assert(bus.service());}
    {Transport t;Bus bus(t,1);t.owner=&bus;t.during=[](void *p){auto &b=*static_cast<Bus *>(p);Ack ack(role,0);
            assert(b.accept(ack.event,1)==n::CommandEvent::fault);};uint8_t seq;
        assert(!bus.submit(role,false,true,nullptr,0,{},0,100,seq)&&bus.error()==n::CommandError::receiver);}
    puts("PASS: shared firmware sequences, separate receipt/done ACK routing, callback-chained submissions, stale epochs/duplicates, deadlines, rejection, transport faults and bounds; no DMA ownership inferred from ACK");
}
