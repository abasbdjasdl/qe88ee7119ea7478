// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/FirmwareMailbox.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
namespace f=rtl8852be::firmware;
struct Device {
    struct Write{unsigned address,value,width;};std::vector<Write> writes;
    std::map<unsigned,unsigned> reg;unsigned ops{},failAt{},cancelAt{},delayCalls{},failDelay{};
    bool gate=true,cancel{},frozen{},backward{},suppressReply{},ignoreScheduler{},ignoreAck{};
    bool injectDuringWait{},callbackDone{};uint64_t time=100;
    uint32_t replyHeader=0x204;void *callbackOwner{};void (*callback)(void *){};
    Device(){reg[f::firmwareControl]=0xe0;reg[f::schedulerTx]=0xa55a;reg[f::hostCounters]=0xae;}
    bool step(){++ops;if(ops==cancelAt)cancel=true;
        if(callback&&!callbackDone){callbackDone=true;callback(callbackOwner);}return ops!=failAt;}
    bool inGate(){return gate;}bool cancelled(){return cancel;}
    uint64_t nowUs(){return backward&&delayCalls?--time:time;}
    bool delayUs(unsigned us){++delayCalls;if(!frozen)time+=us;
        if(injectDuringWait){reg[f::c2hControl]=1;reg[f::h2cControl]=0;}return delayCalls!=failDelay;}
    bool read8(unsigned a,uint8_t &v){v=uint8_t(reg[a]);return step();}
    bool read16(unsigned a,uint16_t &v){v=uint16_t(reg[a]);return step();}
    bool read32(unsigned a,uint32_t &v){v=reg[a];return step();}
    bool write8(unsigned a,uint8_t v){
        if(!step())return false;writes.push_back({a,v,1});if(a==f::c2hControl&&ignoreAck)return true;reg[a]=v;
        if(a==f::h2cControl&&v==1&&!suppressReply){
            if((reg[f::h2cData[0]]&127)==5&&!ignoreScheduler)reg[f::schedulerTx]=reg[f::h2cData[0]]>>16;
            reg[f::h2cControl]=0;reg[f::c2hData[0]]=replyHeader;reg[f::c2hControl]=1;
        }
        return true;
    }
    bool write32(unsigned a,uint32_t v){if(!step())return false;writes.push_back({a,v,4});reg[a]=v;return true;}
};
int main(){
    Device d;f::Mailbox<Device> m(d);assert(m.pauseScheduler());const auto pauseOps=d.ops;
    assert(m.paused()&&!m.faulted()&&d.reg[f::schedulerTx]==0&&d.reg[f::hostCounters]==0xbf);
    assert(d.reg[f::h2cData[0]]==0x205&&d.reg[f::h2cData[1]]==0xffff&&d.reg[f::h2cData[2]]==0&&d.reg[f::h2cData[3]]==0);
    assert(m.result.replyCaptured&&m.result.replyAcknowledged&&m.result.triggerAttempted);
    assert(!m.pauseScheduler()&&d.ops==pauseOps);
    assert(m.resumeScheduler()&&!m.paused()&&d.reg[f::schedulerTx]==0xa55a&&d.reg[f::hostCounters]==0xc0);
    assert(d.reg[f::h2cData[0]]==0xa55a0205);const auto bothOps=d.ops;
    for(unsigned i=1;i<=bothOps;++i){Device x;x.failAt=i;f::Mailbox<Device> c(x);
        assert(!(c.pauseScheduler()&&c.resumeScheduler()));assert(c.faulted()&&c.result.error==f::MailError::io);
        assert(x.ops==i);const auto n=x.ops;assert(!c.pauseScheduler()&&!c.resumeScheduler()&&!c.receivePending()&&x.ops==n);
    }
    // All command payload sizes: exact word count, zero register/padding bytes.
    for(unsigned size=0;size<=14;++size){Device x;f::Mailbox<Device> c(x);f::Request q{};q.function=3;q.contentBytes=uint8_t(size);
        for(auto &w:q.words)w=0xffffffff;assert(c.exchange(q,4));const unsigned length=(size+5)/4;
        assert((x.reg[f::h2cData[0]]&0xffff)==(3|(length<<8)));
        for(unsigned byte=size+2;byte<16;++byte)assert(((x.reg[f::h2cData[byte/4]]>>((byte%4)*8))&255)==0);
    }
    // Counter nibbles wrap independently, preserving the other host counter.
    for(unsigned i=0;i<256;++i){Device x;x.reg[f::hostCounters]=i;f::Mailbox<Device> c(x);assert(c.pauseScheduler());
        assert(x.reg[f::hostCounters]==((((i>>4)+1)&15)<<4|((i+1)&15)));}
    {Device x;x.reg[f::c2hControl]=1;x.reg[f::c2hData[0]]=0x104;f::Mailbox<Device> c(x);
        assert(!c.pauseScheduler()&&c.result.error==f::MailError::staleReply&&!c.faulted()&&x.writes.empty());
        assert(c.receivePending()&&c.result.reply.function==4&&!c.result.triggerAttempted);assert(c.pauseScheduler());}
    {Device x;x.reg[f::h2cControl]=1;x.injectDuringWait=true;f::Mailbox<Device> c(x);
        assert(!c.pauseScheduler()&&c.result.error==f::MailError::staleReply&&!c.faulted()&&x.writes.empty());}
    for(auto header:{0x4u,0x504u,0xf04u,0xffffffffu}){Device x;x.replyHeader=header;f::Mailbox<Device> c(x);
        assert(!c.pauseScheduler()&&c.result.error==f::MailError::malformedReply&&c.result.replyCaptured&&c.result.replyAcknowledged);}
    {Device x;x.replyHeader=0x203;f::Mailbox<Device> c(x);assert(!c.pauseScheduler()&&c.result.error==f::MailError::unexpectedReply&&c.result.stateUncertain);}
    {Device x;x.ignoreScheduler=true;f::Mailbox<Device> c(x);assert(!c.pauseScheduler()&&c.result.error==f::MailError::readback);}
    {Device x;f::Mailbox<Device> c(x);assert(c.pauseScheduler());x.reg[f::schedulerTx]=1;assert(!c.resumeScheduler()&&c.faulted()&&c.paused());}
    for(bool frozen:{false,true}){Device x;x.reg[f::h2cControl]=1;x.frozen=frozen;f::Mailbox<Device> c(x);
        assert(!c.pauseScheduler()&&c.result.error==f::MailError::timeout&&!c.result.triggerAttempted&&c.result.polls<=6);
        Device y;y.suppressReply=true;y.frozen=frozen;f::Mailbox<Device> z(y);
        assert(!z.pauseScheduler()&&z.result.error==f::MailError::timeout&&z.result.triggerAttempted&&z.result.polls<=1000002);
    }
    {Device x;x.reg[f::h2cControl]=1;x.backward=true;f::Mailbox<Device> c(x);assert(!c.pauseScheduler()&&c.result.error==f::MailError::clock);}
    {Device x;x.reg[f::h2cControl]=1;x.failDelay=1;f::Mailbox<Device> c(x);assert(!c.pauseScheduler()&&c.result.error==f::MailError::io);}
    for(auto reg:{f::h2cControl,f::c2hControl}){Device x;x.reg[reg]=255;f::Mailbox<Device> c(x);assert(!c.pauseScheduler()&&c.result.error==f::MailError::io);}
    {Device x;x.gate=false;f::Mailbox<Device> c(x);assert(!c.pauseScheduler()&&!x.ops&&c.result.error==f::MailError::ownership);}
    {Device x;x.cancel=true;f::Mailbox<Device> c(x);assert(!c.pauseScheduler()&&!x.ops&&c.result.error==f::MailError::cancelled);}
    for(unsigned i:{1u,pauseOps/2,pauseOps}){Device x;x.cancelAt=i;f::Mailbox<Device> c(x);assert(!c.pauseScheduler()&&c.result.error==f::MailError::cancelled&&x.ops==i);}
    {Device x;f::Mailbox<Device> c(x);x.callbackOwner=&c;x.callback=[](void *p){assert(!static_cast<f::Mailbox<Device> *>(p)->receivePending());};
        assert(!c.pauseScheduler()&&c.result.error==f::MailError::ownership);}
    printf("PASS: AX firmware mailbox/SCC pause-resume, %u I/O failures, all payload lengths/counter wraps, stale/malformed replies, timeouts and gate/reentry\n",bothOps);
}
