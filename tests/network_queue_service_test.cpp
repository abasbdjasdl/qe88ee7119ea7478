// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/PciQueueService.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
namespace n=rtl8852be::network;
struct Runtime {
    uint16_t hosts[9]{},hardware[9]{};bool live=true;int failedRead=-1,failedPublish=-1;
    unsigned readCount{},publishCount{};std::vector<unsigned> published;
    bool running(){return live;}
    bool readIndices(unsigned i,uint16_t &h,uint16_t &d){++readCount;if(int(readCount)==failedRead)return false;h=hosts[i];d=hardware[i];return true;}
    bool publish(unsigned i,uint16_t h){++publishCount;if(int(publishCount)==failedPublish)return false;hosts[i]=h;published.push_back(i);return true;}
};
struct Tx {unsigned calls{};uint16_t consumer{};bool fail{};Tx &completions(){return *this;}
    int consumeTo(uint16_t c){++calls;consumer=c;return fail?1:0;}};
struct Poll {bool ok=true;uint16_t consumer=0;};
struct Rx {
    uint16_t consumer{};unsigned delivered{};bool fail{};
    Poll poll(uint16_t producer,size_t budget,void (*receive)(void *,const uint8_t *,size_t),void *context){
        if(fail)return {false,consumer};const uint8_t bytes[4]={1,2,3,4};
        while(consumer!=producer&&budget--){receive(context,bytes,sizeof(bytes));consumer=(consumer+1)%64;++delivered;}
        return {true,consumer};
    }
};
using Service=n::PciQueueService<Runtime,Tx,Tx,Rx>;
struct Sink {
    Runtime *runtime;Service *service{};unsigned rx{},rpq{};bool fail{},stop{},arrive{},reenter{};
    static bool rxq(void *ctx,const uint8_t *bytes,size_t length){auto &s=*static_cast<Sink *>(ctx);assert(bytes[0]==1&&length==4);++s.rx;return s.handle();}
    static bool rpqf(void *ctx,const uint8_t *bytes,size_t length){auto &s=*static_cast<Sink *>(ctx);assert(bytes[0]==1&&length==4);++s.rpq;return s.handle();}
    bool handle(){assert(service->serving());if(stop)runtime->live=false;
        if(arrive){runtime->hardware[7]=(runtime->hardware[7]+1)%64;arrive=false;}
        if(reenter){reenter=false;assert(service->drain()==n::QueueServiceResult::fault);}
        return !fail;
    }
};
int main(){
    for(unsigned mode=0;mode<8;++mode){
        Runtime runtime;Tx tx[6],firmware;Tx *queues[6];for(unsigned i=0;i<6;++i)queues[i]=&tx[i];Rx rx,rpq;
        Sink sink{&runtime};Service service(runtime,queues,firmware,rx,rpq,{&sink,Sink::rxq,Sink::rpqf});sink.service=&service;
        runtime.hardware[7]=40;runtime.hardware[8]=2;
        if(mode==1)sink.fail=true;if(mode==2)sink.stop=true;if(mode==3)sink.arrive=true;
        if(mode==4)sink.reenter=true;if(mode==5)firmware.fail=true;if(mode==6)rx.fail=true;if(mode==7)runtime.failedPublish=1;
        const auto result=service.drain();assert(!service.serving());
        if(mode==0||mode==3){assert(result==n::QueueServiceResult::more&&runtime.hosts[8]==2&&runtime.hosts[7]==32);
            assert(sink.rpq==2&&sink.rx==32&&runtime.published[0]==8);
            assert(service.drain()==n::QueueServiceResult::drained&&sink.rx==(mode==3?41u:40u));
            for(auto &q:tx)assert(q.calls==2);assert(firmware.calls==2);
        }else{assert(result==n::QueueServiceResult::fault&&service.faulted());const auto count=sink.rx+sink.rpq;
            assert(service.drain()==n::QueueServiceResult::fault&&sink.rx+sink.rpq==count);}
    }
    for(int fail=1;fail<=11;++fail){Runtime runtime;runtime.failedRead=fail;Tx tx[6],fw;Tx *q[6];for(unsigned i=0;i<6;++i)q[i]=&tx[i];Rx rx,rpq;
        Sink sink{&runtime};Service service(runtime,q,fw,rx,rpq,{&sink,Sink::rxq,Sink::rpqf});sink.service=&service;
        assert(service.drain()==n::QueueServiceResult::fault&&service.faulted());}
    puts("PASS: queue service model; all seven TX consumers, RPQ priority, RX budget continuation/arrival race, callback fault/stop/reentry, MMIO failures");
}
