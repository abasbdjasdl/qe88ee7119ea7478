// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/BtRfkCoordination.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
namespace b=rtl8852be::bt;
namespace n=rtl8852be::network;
struct Device {
    struct Write {uint32_t address,value;unsigned width;};
    std::map<uint32_t,uint32_t> reg;
    std::vector<Write> writes;std::vector<std::vector<uint8_t>> commands;
    uint32_t grant=0xa6005900,btScoreboard=0x16000003,lastScoreboard{};
    unsigned ops{},failAt{},delays{},failDelay{},submits{},invalidateCalls{};
    unsigned btBusyReads{},btReads{},busyLteReads{},lteReads{};
    unsigned jumpAt{};uint64_t jumpUs{};
    uint64_t time=100;uint8_t nextSequence=19;
    bool gate=true,cancel{},frozen{},backward{},ignoreGrant{},ignoreControl{},ignorePriority{};
    bool failSubmit{},sameSequence{},btRace{},writeThenFail{},ackDuringSubmit{},invalidated{};
    void *callbackOwner{};void (*callback)(void *){};
    Device(){reg[b::firmwareControl]=0xe0;reg[b::schedulerRegister]=0;
        reg[b::cmacFunctionRegister]=0x40000000;
        reg[b::controlPathRegister]=0xa1;reg[b::priorityRegister]=0x015a;}
    bool step(){++ops;if(ops==jumpAt)time+=jumpUs;return ops!=failAt;}
    bool inGate(){return gate;}bool cancelled(){return cancel;}
    uint64_t nowUs(){if(backward&&delays)--time;return time;}
    bool delayUs(unsigned us){++delays;if(!frozen)time+=us;return delays!=failDelay;}
    bool read8(uint32_t a,uint8_t &v){
        if(!step())return false;
        v=a==b::lteControl+3?(++lteReads<=busyLteReads?0:0x20):uint8_t(reg[a]);return true;}
    bool read16(uint32_t a,uint16_t &v){v=uint16_t(reg[a]);return step();}
    bool read32(uint32_t a,uint32_t &v){
        if(!step())return false;
        if(a==b::scoreboardRegister){++btReads;v=btScoreboard;
            if(btReads<=btBusyReads||(btRace&&lastScoreboard))v|=b::btRfkRun;
        }else v=a==b::lteReadData?grant:reg[a];
        return true;}
    bool write8(uint32_t a,uint8_t v){
        bool ok=step();if(!ok&&!writeThenFail)return false;
        writes.push_back({a,v,1});if(!ignoreControl)reg[a]=v;return ok;}
    bool write16(uint32_t a,uint16_t v){
        bool ok=step();if(!ok&&!writeThenFail)return false;
        writes.push_back({a,v,2});if(!ignorePriority)reg[a]=v;return ok;}
    bool write32(uint32_t a,uint32_t v){
        bool ok=step();if(!ok&&!writeThenFail)return false;
        writes.push_back({a,v,4});
        if(a==b::scoreboardRegister)lastScoreboard=v;
        else {reg[a]=v;if(a==b::lteControl&&(v&0xc0000000)==0xc0000000&&!ignoreGrant)
            grant=reg[b::lteWriteData];}
        return ok;}
    bool submitH2c(n::CommandId id,bool doneAck,const uint8_t *p,size_t bytes,uint8_t &sequence){
        ++submits;assert(n::sameCommand(id,b::policyCommand)&&doneAck&&bytes==26);
        commands.emplace_back(p,p+bytes);sequence=nextSequence;
        if(!sameSequence)++nextSequence;
        if(ackDuringSubmit&&callback)callback(callbackOwner);
        return !failSubmit&&!invalidated;
    }
    void invalidateFirmwareEpoch(){invalidated=true;++invalidateCalls;}
};
using Coordinator=b::RfkCoordination<Device>;
b::PolicySnapshot baseline(){
    b::PolicySnapshot p{};p.tdma[0]=1;p.tdma[4]=5;p.tdma[7]=0x40;
    p.offDuration=77;p.offTable=0x5a5a5a5a;p.offType=1;p.valid=true;return p;
}
struct Fixture {
    Device io;b::Scoreboard shadow{0x412383,true};
    Coordinator owner{io,shadow,b::Protocol::select(0x001d1d00),baseline()};
    bool begin(){return owner.begin({b::RfkType::iqk,3,1,0});}
    b::EventResult ack(uint8_t seq,uint8_t ret=0,n::CommandId command=b::policyCommand,bool done=true){
        uint8_t bytes[4]{};n::store32(bytes,uint32_t(command.category)|(uint32_t(command.commandClass)<<2)|
            (uint32_t(command.function)<<8)|(uint32_t(ret)<<16)|(uint32_t(seq)<<24));
        const n::FirmwareEvent event{{1,0,uint8_t(done?1:0)},bytes,sizeof(bytes)};
        return owner.acceptEvent(event);
    }
    void ready(){assert(begin());assert(!owner.ready());assert(ack(19)==b::EventResult::consumed);
        assert(owner.ready()&&owner.result.startAcknowledged);}
    void finish(){assert(owner.finish());assert(!owner.ready());
        assert(ack(20)==b::EventResult::consumed);assert(owner.result.stage==b::Stage::complete);}
};

static void formats(){
    assert(!b::Protocol::select(0x001a0000).valid());
    assert(b::Protocol::select(0x001b0000).valid());
    assert(b::Protocol::select(0x001d1d00).valid());
    assert(!b::Protocol::select(0x001e0000).valid());
    uint8_t bytes[26]{};assert(b::encodePolicy({3,1},b::calibrationPolicy(),bytes));
    const uint8_t expected[]={0,12,3,0,0,0,0,0,0,0,0,0,0,0,1,10,1,0,100,0,0xaa,0xaa,0xaa,0xaa,0,0};
    for(unsigned i=0;i<26;++i)assert(bytes[i]==expected[i]);
    assert(!b::encodePolicy({1,1},b::calibrationPolicy(),bytes));
    uint8_t info[6]{};assert(b::encodeRfkInfo({b::RfkType::tssi,3,1,1},b::RfkState::oneshotStart,info));
    assert(info[0]==4&&info[1]==4&&n::little32(info+2)==0x194e);
    assert(!b::encodeRfkInfo({b::RfkType::iqk,3,2,0},b::RfkState::start,info));
    uint8_t command[64]{};size_t length=0;
    assert(n::encodeH2c(b::policyCommand,19,false,true,expected,26,command,sizeof(command),length));
    assert(length==34&&n::little32(command)==0x13000342&&n::little32(command+4)==0x8022);
}
static unsigned lifecycle(){
    Fixture f;const auto oldShadow=f.shadow.value,oldGrant=f.io.grant;
    f.ready();assert(f.io.grant==((oldGrant&~b::grantMask)|b::calibrationGrants));
    assert(f.io.reg[b::controlPathRegister]==0xa5&&f.io.reg[b::priorityRegister]==0x100);
    assert(f.shadow.value==((oldShadow|b::wlRfkBit)&~b::tdmaBit));
    assert(f.io.lastScoreboard==(((oldShadow|b::wlRfkBit)&~b::tdmaBit)|0x97000000));
    // Read-side BT bits differ from the W2B shadow. They must never be copied.
    assert((f.shadow.value&0xffffff)==((oldShadow|b::wlRfkBit)&~b::tdmaBit));
    assert(f.owner.oneshot(0x11,true));assert(f.owner.result.oneshot);
    assert(f.owner.oneshot(0x11,false));assert(!f.owner.result.oneshot);
    assert(f.owner.oneshot(0x12,true));assert(f.owner.oneshot(0x12,false));
    f.finish();assert(f.io.grant==oldGrant&&f.io.reg[b::controlPathRegister]==0xa1);
    assert(f.io.reg[b::priorityRegister]==0x15a&&f.shadow.value==oldShadow);
    assert(f.owner.result.hardwareRestored&&!f.owner.result.ownershipHeld&&!f.owner.result.requiresRecovery);
    assert(f.io.submits==2&&!f.io.invalidated&&f.io.delays==2);
    uint8_t restore[26]{};assert(b::encodePolicy({3,1},baseline(),restore));
    for(unsigned i=0;i<26;++i)assert(f.io.commands[1][i]==restore[i]);
    assert(f.ack(20)==b::EventResult::unrelated);
    return f.io.ops;
}
static void acknowledgements(){
    Fixture f;assert(f.begin());
    assert(f.ack(18)==b::EventResult::unrelated&&!f.owner.ready());
    assert(f.ack(19,0,{1,8,0})==b::EventResult::unrelated&&!f.owner.ready());
    assert(f.ack(19,0,b::policyCommand,false)==b::EventResult::unrelated&&!f.owner.ready());
    uint8_t shortAck[3]{};
    assert(f.owner.acceptEvent({{1,0,1},shortAck,3})==b::EventResult::unrelated);
    assert(f.ack(19)==b::EventResult::consumed&&f.owner.ready());
    assert(f.owner.finish());assert(f.ack(19)==b::EventResult::unrelated);
    assert(!f.owner.result.hardwareRestored&&f.owner.result.ownershipHeld);
    assert(f.ack(20,7)==b::EventResult::fault);
    assert(f.owner.result.error==b::Error::firmwareRejected&&f.io.invalidated);
    assert(f.owner.result.ownershipHeld&&!f.owner.result.hardwareRestored);
    Fixture rejected;assert(rejected.begin());assert(rejected.ack(19,1)==b::EventResult::fault);
    assert(!rejected.owner.ready()&&rejected.owner.result.requiresRecovery);
    Fixture reuse;reuse.io.sameSequence=true;reuse.ready();assert(!reuse.owner.finish());
    assert(reuse.owner.result.error==b::Error::sequenceReuse&&reuse.io.invalidated);
}
static void arbitration(){
    Fixture busy;busy.io.btBusyReads=2501;assert(!busy.begin());
    assert(busy.owner.result.error==b::Error::btBusy&&busy.owner.result.btPolls==2501);
    assert(!busy.owner.result.ownershipHeld&&!busy.io.submits&&busy.io.writes.empty());
    Fixture release;release.io.btBusyReads=17;release.ready();assert(release.owner.result.btPolls==18);
    Fixture frozen;frozen.io.frozen=true;frozen.io.btBusyReads=9999;assert(!frozen.begin());
    assert(frozen.owner.result.btPolls==2501&&frozen.io.delays==2500);
    Fixture race;race.io.btRace=true;assert(!race.begin());
    assert(race.owner.result.error==b::Error::btBusy&&race.owner.result.requiresRecovery);
    assert(!race.io.submits&&!race.owner.result.grantsAttempted);
    for(uint32_t invalid:{0xffffffffU,0xdeadbeefU}){Fixture f;f.io.btScoreboard=invalid;
        assert(!f.begin()&&f.owner.result.error==b::Error::invalidScoreboard);assert(f.io.writes.empty());}
    Fixture lte;lte.io.busyLteReads=2000;assert(!lte.begin());
    assert(lte.owner.result.error==b::Error::lteTimeout&&lte.io.writes.empty());
    Fixture frozenLte;frozenLte.io.frozen=true;frozenLte.io.busyLteReads=2000;assert(!frozenLte.begin());
    assert(frozenLte.owner.result.ltePolls==1001&&frozenLte.io.writes.empty());
    Fixture lateBt;lateBt.io.jumpAt=4;lateBt.io.jumpUs=100001;assert(!lateBt.begin());
    assert(lateBt.owner.result.error==b::Error::btBusy&&lateBt.io.writes.empty());
    Fixture lateLte;lateLte.io.jumpAt=7;lateLte.io.jumpUs=50001;assert(!lateLte.begin());
    assert(lateLte.owner.result.error==b::Error::lteTimeout&&lateLte.io.writes.empty());
}
static void faults(unsigned operations){
    for(unsigned failure=1;failure<=operations;++failure){
        Fixture f;f.io.failAt=failure;f.io.writeThenFail=true;
        bool ok=f.begin();
        if(ok)ok=f.ack(19)==b::EventResult::consumed;
        if(ok)ok=f.owner.ready();
        if(ok)ok=f.owner.oneshot(0x11,true);
        if(ok)ok=f.owner.oneshot(0x11,false);
        if(ok)ok=f.owner.oneshot(0x12,true);
        if(ok)ok=f.owner.oneshot(0x12,false);
        if(ok)ok=f.owner.finish();
        if(ok)ok=f.ack(20)==b::EventResult::consumed;
        assert(!ok&&f.owner.result.error==b::Error::io);
        assert(f.owner.result.stage==b::Stage::fault&&!f.owner.ready());
        assert(!f.owner.result.hardwareRestored);
        if(f.owner.result.scoreboardAttempted)assert(f.owner.result.ownershipHeld&&f.owner.result.requiresRecovery);
        if(f.owner.result.policyAttempted)assert(f.io.invalidated);
        const auto writes=f.io.writes.size();assert(!f.owner.service());assert(!f.owner.finish());
        assert(f.io.writes.size()==writes);
    }
    for(unsigned d=1;d<=2;++d){Fixture f;f.io.failDelay=d;bool ok=f.begin();
        if(ok)ok=f.ack(19)==b::EventResult::consumed;
        if(ok)ok=f.owner.finish();if(ok)ok=f.ack(20)==b::EventResult::consumed;
        assert(!ok&&f.owner.result.error==b::Error::io&&f.owner.result.ownershipHeld);}
    Fixture submit;submit.io.failSubmit=true;assert(!submit.begin());
    assert(submit.owner.result.error==b::Error::firmwareCommand&&submit.io.invalidated);
    for(unsigned ignore=0;ignore<3;++ignore){Fixture f;
        f.io.ignoreGrant=ignore==0;f.io.ignoreControl=ignore==1;f.io.ignorePriority=ignore==2;
        assert(!f.begin()&&f.owner.result.error==b::Error::readback&&f.owner.result.ownershipHeld);}
    for(unsigned ignore=0;ignore<3;++ignore){Fixture f;f.ready();
        f.io.ignoreGrant=ignore==0;f.io.ignoreControl=ignore==1;f.io.ignorePriority=ignore==2;
        assert(f.owner.finish());assert(f.ack(20)==b::EventResult::fault);
        assert(f.owner.result.error==b::Error::readback&&f.owner.result.ownershipHeld);}
}
static void lifetimes(){
    Fixture timeout;assert(timeout.begin());timeout.io.time+=300000;
    assert(timeout.ack(19)==b::EventResult::fault);
    assert(timeout.owner.result.error==b::Error::watchdog&&!timeout.owner.ready());
    assert(timeout.io.invalidated&&timeout.owner.result.ownershipHeld);
    Fixture noTick;noTick.ready();noTick.io.time+=300000;assert(!noTick.owner.ready());
    assert(noTick.owner.result.error==b::Error::watchdog&&noTick.owner.result.ownershipHeld);
    Fixture shots;shots.ready();
    for(unsigned i=0;i<2;++i){shots.io.time+=90000;assert(shots.owner.oneshot(0x11,true));assert(shots.owner.oneshot(0x11,false));}
    shots.io.time+=130000;assert(!shots.owner.service());assert(shots.owner.result.error==b::Error::watchdog);
    Fixture cancel;cancel.ready();cancel.io.cancel=true;assert(!cancel.owner.service());
    assert(cancel.owner.result.error==b::Error::cancelled&&cancel.owner.result.ownershipHeld);
    Fixture clock;clock.io.backward=true;assert(!clock.begin()&&clock.owner.result.error==b::Error::clock);
    Fixture gate;gate.io.gate=false;assert(!gate.begin()&&gate.io.writes.empty());
    Fixture tx;tx.io.reg[b::schedulerRegister]=1;assert(!tx.begin());
    assert(tx.owner.result.error==b::Error::txNotStopped&&tx.io.writes.empty());
    Fixture txLate;assert(txLate.begin());txLate.io.reg[b::schedulerRegister]=0x8000;
    assert(txLate.ack(19)==b::EventResult::fault&&!txLate.owner.ready());
    Fixture txStop;txStop.ready();txStop.io.reg[b::schedulerRegister]=1;
    assert(!txStop.owner.finish()&&txStop.owner.result.ownershipHeld);
    Fixture firmware;firmware.io.reg[b::firmwareControl]=0x20;
    assert(!firmware.begin()&&firmware.owner.result.error==b::Error::firmwareUnavailable);
    Fixture cmac;cmac.io.reg[b::cmacFunctionRegister]=0;
    assert(!cmac.begin()&&cmac.owner.result.error==b::Error::cmacUnavailable);
    Fixture shadow;shadow.ready();shadow.shadow.value^=1;
    assert(!shadow.owner.ready()&&shadow.owner.result.error==b::Error::ownership);
    Fixture badMap;assert(!badMap.owner.begin({b::RfkType::iqk,3,2,0}));assert(badMap.io.ops==0);
    Fixture nesting;nesting.ready();assert(nesting.owner.oneshot(0x11,true));
    assert(!nesting.owner.finish()&&nesting.owner.result.requiresRecovery);
    Fixture wrongPath;wrongPath.ready();assert(wrongPath.owner.oneshot(0x11,true));
    assert(!wrongPath.owner.oneshot(0x12,false)&&wrongPath.owner.result.requiresRecovery);
    Fixture calibration;calibration.ready();assert(!calibration.owner.calibrationFailed());
    assert(calibration.owner.result.ownershipHeld&&calibration.io.invalidated);
    Fixture recursion;recursion.io.ackDuringSubmit=true;recursion.io.callbackOwner=&recursion.owner;
    recursion.io.callback=[](void *p){static_cast<Coordinator *>(p)->service();};
    assert(!recursion.begin()&&recursion.owner.result.error==b::Error::ownership);
}
int main(){formats();const auto operations=lifecycle();acknowledgements();arbitration();faults(operations);lifetimes();
    std::printf("BT RFK coordination passed: %u I/O fault points, real-ACK matching, bounded arbitration, grants and policy restore\n",operations);
}
