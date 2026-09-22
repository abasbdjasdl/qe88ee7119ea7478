// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/RadioBootSequence.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace rtl8852be::network::radioboot;
struct Backend {
    bool gate{true},health{true},paused{},lease{},ran{},startAck{},stopAck{},btAck{};
    unsigned calls{},failAt{},failures{},pages{},commands{},finishCalls{};Step current{};
    std::vector<Step> steps;void *owner{};void (*reenter)(void *){};
    bool op(){return ++calls!=failAt;}
    bool inGate(){return gate;}bool healthy(){return health;}
    bool pause(){if(!op())return false;paused=true;return true;}
    bool prepare(){assert(paused);if(reenter)reenter(owner);return op();}
    Progress upload(){if(!op())return Progress::failed;++commands;return ++pages==6?Progress::complete:Progress::pending;}
    bool startBt(){assert(paused);commands+=5;return op();}
    Progress pollBt(){if(!op())return Progress::failed;return btAck?Progress::complete:Progress::pending;}
    bool requestLease(Step s){assert(paused&&!lease);if(!op())return false;
        current=s;lease=true;ran=startAck=stopAck=false;++commands;return true;}
    Progress pollLease(){if(!op())return Progress::failed;return startAck?Progress::complete:Progress::pending;}
    bool run(Step s){assert(paused&&lease&&startAck&&!stopAck&&s==current&&!ran);
        if(!op())return false;ran=true;steps.push_back(s);return true;}
    bool finishLease(){assert(paused&&lease&&ran);++commands;return op();}
    Progress pollRestore(){if(!op())return Progress::failed;if(!stopAck)return Progress::pending;lease=false;return Progress::complete;}
    bool finishOperation(Action){assert(paused&&!lease);++finishCalls;return op();}
    void failed(){++failures;paused=true;health=false;}
};
static bool drive(Sequence<Backend> &q,Backend &b,uint64_t &now){
    for(unsigned n=0;n<1000&&q.busy();++n){
        const auto stage=q.result.stage;
        if(stage==Stage::btInitialization)b.btAck=true;
        if(stage==Stage::acquire)b.startAck=true;
        if(stage==Stage::restore)b.stopAck=true;
        if(!q.service(++now))return false;
    }
    return q.result.stage==Stage::ready;
}
static unsigned complete(Backend &b,Sequence<Backend> &q,uint64_t &now){
    assert(q.begin(++now));assert(drive(q,b,now));
    assert(q.result.initialized&&!q.result.tuned&&!q.result.fullCalibration);
    assert((b.steps==std::vector<Step>{Step::rck,Step::dack,Step::initialRxDc}));
    assert(!q.launch(Action::scanBegin,++now));
    assert(q.launch(Action::tune,++now));assert(drive(q,b,now));assert(q.result.tuned&&q.result.fullCalibration);
    assert(q.launch(Action::scanBegin,++now));assert(drive(q,b,now));
    assert(q.result.scanning&&q.result.tuned&&!q.result.fullCalibration);
    for(unsigned n=0;n<22;++n){
        assert(!q.launch(Action::tune,++now));
        assert(q.launch(Action::scanTune,++now));assert(drive(q,b,now));
        assert(q.result.scanning&&q.result.tuned&&!q.result.fullCalibration);
        assert(b.steps[b.steps.size()-2]==Step::channel&&b.steps.back()==Step::scanTune);
    }
    assert(q.launch(Action::scanEnd,++now));assert(drive(q,b,now));
    assert(!q.result.scanning&&q.result.fullCalibration&&q.result.tuned);
    assert(q.launch(Action::tune,++now));assert(drive(q,b,now));
    assert(b.commands==129); // max6 RF pages+5 init+6 initial+10 home+4 scan edges+88 hops+10 associate
    assert(q.result.completion==27);return b.calls;
}
int main(){
    {Backend b;Sequence<Backend> q(b);uint64_t now=0;complete(b,q,now);
     const auto calls=b.calls;assert(!q.begin(++now)&&b.calls==calls);
     assert(q.stop()&&!q.result.tuned&&q.result.stage==Stage::stopped&&!q.service(++now));}
    // A queued service cannot manufacture firmware acknowledgement.
    {Backend b;Sequence<Backend> q(b);uint64_t now=0;assert(q.begin(++now));
     for(unsigned i=0;i<6;++i)assert(q.service(++now));assert(q.result.stage==Stage::btInitialization);
     for(unsigned i=0;i<5;++i)assert(q.service(++now));assert(b.steps.empty());
     b.btAck=true;assert(q.service(++now));assert(q.result.stage==Stage::acquire);
     for(unsigned i=0;i<5;++i)assert(q.service(++now));assert(b.steps.empty());
     b.startAck=true;assert(q.service(++now));assert(q.result.stage==Stage::run);
     assert(q.service(++now));assert(q.result.stage==Stage::restore&&b.steps.size()==1);
     for(unsigned i=0;i<5;++i)assert(q.service(++now));assert(b.steps.size()==1&&!q.result.initialized);
     b.stopAck=true;assert(q.service(++now));assert(q.result.step==Step::dack);}
    // Fail every backend action of boot + one full channel RFK; never leak readiness.
    unsigned total=0;
    {Backend b;Sequence<Backend> q(b);uint64_t n=0;assert(q.begin(++n)&&drive(q,b,n));
     assert(q.launch(Action::tune,++n)&&drive(q,b,n));total=b.calls;}
    for(unsigned point=1;point<=total;++point){Backend b;b.failAt=point;Sequence<Backend> q(b);uint64_t n=0;
        const bool done=q.begin(++n)&&drive(q,b,n)&&q.launch(Action::tune,++n)&&drive(q,b,n);
        assert(!done&&q.result.stage==Stage::fault&&q.result.requiresRecovery&&!q.result.tuned&&b.failures);}
    {Backend b;Sequence<Backend> q(b);assert(q.begin(10));assert(!q.service(9)&&q.result.error==Error::clock);}
    {Backend b;Sequence<Backend> q(b);assert(q.begin(10));assert(!q.service(10000010)&&q.result.error==Error::timeout);}
    {Backend b;Sequence<Backend> q(b);uint64_t n=0;assert(q.begin(++n)&&drive(q,b,n));
     assert(q.launch(Action::tune,++n));assert(q.service(n+2000000));
     assert(q.service(n+9999999)&&q.result.stage==Stage::acquire&&!q.result.tuned);
     assert(!q.service(n+10000000)&&q.result.error==Error::timeout);}
    {Backend b;Sequence<Backend> q(b);uint64_t n=0;assert(q.begin(++n)&&drive(q,b,n));
     assert(q.launch(Action::tune,++n)&&drive(q,b,n));assert(q.launch(Action::scanBegin,++n));
     assert(q.service(n+1999999)&&!q.result.tuned);
     assert(!q.service(n+2000000)&&q.result.error==Error::timeout);}
    {Backend b;b.gate=false;Sequence<Backend> q(b);assert(!q.begin(0)&&q.result.error==Error::ownership&&b.calls==0);}
    {Backend b;Sequence<Backend> q(b);b.owner=&q;b.reenter=[](void *p){assert(!static_cast<Sequence<Backend> *>(p)->service(1));};
     assert(!q.begin(0)&&q.result.stage==Stage::fault&&q.result.error==Error::ownership);}
    std::printf("PASS: radio lifecycle ACK gating, initial/full/22 scan hops, 129 command budget, %u failed stages, deadlines and reentry\n",total);
}
