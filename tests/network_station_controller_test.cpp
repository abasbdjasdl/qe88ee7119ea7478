// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/StationController.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <array>
using namespace rtl8852be;
using namespace rtl8852be::station;
struct Backend {
    bool gated=true,resetProven=true,reuseSequence=false;uint64_t expectedEpoch=1;
    unsigned nextSequence=0,operations=0,failAt=0,authStarts=0,probes=0,cancellations=0,scanCompletions=0,faults=0;
    bool lastScanCancelled=false;Traffic mode=Traffic::none;
    std::vector<ActionRequest> requests;std::vector<std::array<uint8_t,12>> commands;
    std::vector<Traffic> trafficHistory;
    bool op(){++operations;return operations!=failAt;}
    bool inGate(){return gated;}
    bool setTraffic(Traffic t){if(!op())return false;mode=t;trafficHistory.push_back(t);return true;}
    bool reserveH2cSequence(uint8_t &out){if(!op())return false;out=reuseSequence?0:uint8_t(nextSequence++);return true;}
    bool publishH2c(const uint8_t *p,size_t n,Token t){
        assert(n==12&&t.epoch==expectedEpoch&&t.operation);if(!op())return false;
        std::array<uint8_t,12> copy{};for(unsigned i=0;i<12;++i)copy[i]=p[i];commands.push_back(copy);return true;
    }
    bool beginAction(const ActionRequest &r){if(!op())return false;requests.push_back(r);return true;}
    bool beginAuthentication(Token t,const Peer &p){assert(t.operation&&unicast(p.bssid));if(!op())return false;++authStarts;return true;}
    bool sendProbe(Token t,const Peer *,const ScanChannel &c){assert(t.operation&&c.active&&mode==Traffic::scanProbe);if(!op())return false;++probes;return true;}
    bool cancelProtocol(Token t){assert(t.operation);if(!op())return false;++cancellations;return true;}
    void scanFinished(Token t,bool cancelled){assert(t.operation);++scanCompletions;lastScanCancelled=cancelled;}
    void recoveryRequired(Token t,Error e){assert(t.epoch&&e!=Error::none);++faults;}
    bool firmwareRestartVerified(uint64_t epoch){return resetProven&&epoch==expectedEpoch;}
};
using Device=Controller<Backend>;
static Address address(uint8_t tail){return {{0x02,0x11,0x22,0x33,0x44,tail}};}
static Interface interface(){Interface i{};i.address=address(1);i.macid=7;i.home={0,0,1,1};return i;}
static Peer peer(){Peer p{};p.bssid=address(2);p.channel={1,2,42,36};p.ssidLength=3;p.ssid[0]='a';p.ssid[1]='p';p.ssid[2]='1';p.protectedNetwork=true;return p;}
static Association assoc(){return {peer().bssid,peer().channel,42};}
static bool finish(Device &d,Backend &b,uint64_t now,bool success=true){
    const auto r=b.requests.back();return d.actionComplete(r.token,r.action,success,now);
}
static bool ackCommand(Device &d,const std::array<uint8_t,12> &packet,uint64_t now,
                       uint8_t rc=0,bool done=true,uint64_t epoch=1,int sequenceOverride=-1){
    uint8_t payload[4]{};const uint8_t sequence=sequenceOverride<0?packet[3]:uint8_t(sequenceOverride);
    const uint32_t command=network::little32(packet.data())&0xffff;
    network::store32(payload,command|(done?(uint32_t(rc)<<16)|(uint32_t(sequence)<<24):uint32_t(sequence)<<16));
    return d.firmwareEvent({{1,0,uint8_t(done?1:0)},payload,4},epoch,now);
}
static bool ack(Device &d,Backend &b,uint64_t now){return ackCommand(d,b.commands.back(),now);}
static bool boot(Device &d,Backend &b){
    return d.restart(1,0)&&d.start(interface(),1)&&finish(d,b,2)&&ack(d,b,3)&&ack(d,b,4)&&finish(d,b,5);
}
static bool connect(Device &d,Backend &b,uint64_t now){
    if(!d.connect(peer(),now)||!finish(d,b,now+1))return false;
    const auto t=d.associationToken();
    return d.authenticated(t,peer().bssid,true,now+2)&&d.associationReceived(t,assoc(),now+3)&&
        finish(d,b,now+4)&&ack(d,b,now+5)&&finish(d,b,now+6);
}
static bool disconnect(Device &d,Backend &b,uint64_t now){
    return d.disconnect(now)&&finish(d,b,now+1)&&ack(d,b,now+2)&&finish(d,b,now+3);
}
static bool scan(Device &d,Backend &b,uint64_t now){
    const ScanChannel plan[]={{{0,0,1,1},20,true},{{1,0,52,52},30,false}};
    return d.scan(plan,2,now)&&finish(d,b,now+1)&&finish(d,b,now+2)&&
        d.tick(now+20002)&&finish(d,b,now+20003)&&d.tick(now+50003)&&
        finish(d,b,now+50004)&&finish(d,b,now+50005);
}
static void happyAndWire(){
    Backend b;Device d(b);assert(boot(d,b));assert(d.state()==State::idle&&d.roleCreated());
    assert(b.commands.size()==2);
    const auto &role=b.commands[0];assert(network::little32(role.data())==0x00000421);
    assert(network::little32(role.data()+4)==0xc00c&&network::little32(role.data()+8)==0x2007);
    const auto &noLink=b.commands[1];assert(network::little32(noLink.data())==0x01000021);
    assert(network::little32(noLink.data()+4)==0x800c&&network::little32(noLink.data()+8)==0x04000107);
    assert(scan(d,b,10));assert(b.probes==1&&b.scanCompletions==1&&d.state()==State::idle);
    assert(connect(d,b,60000));assert(d.associated()&&!d.portAuthorized()&&b.mode==Traffic::controlledPort);
    assert(network::little32(b.commands.back().data()+8)==0x06000007);
    ScanChannel ch{{0,0,1,1},20,true};assert(!d.scan(&ch,1,60007)); // handshake not interrupted
    assert(d.authorizePort(d.associationToken(),60008));assert(d.portAuthorized());
    assert(scan(d,b,70000));assert(d.portAuthorized()&&b.mode==Traffic::authorized);
    const auto restore=b.requests[b.requests.size()-2];assert(restore.action==Action::scanRestore&&sameChannel(restore.channel,peer().channel));
    assert(disconnect(d,b,130000));assert(d.state()==State::idle&&!d.associated());
    assert(d.stop(130004)&&ack(d,b,130005)&&finish(d,b,130006));
    assert(d.state()==State::stopped&&!d.roleCreated()&&b.mode==Traffic::none&&b.faults==0);
    assert((network::little32(b.commands.back().data()+8)&0x1c00)==0x400);
}
static void acknowledgments(){
    Backend b;Device d(b);assert(d.restart(1,0)&&d.start(interface(),1)&&finish(d,b,2));
    const auto role=b.commands.back();assert(d.commandPending());
    assert(!ackCommand(d,role,3,0,false));assert(d.state()==State::creatingRole);
    assert(!ackCommand(d,role,4,0,true,0)); // callback from old RX epoch
    assert(!ackCommand(d,role,5,0,true,1,255));
    auto wrong=role;wrong[1]=0;assert(!ackCommand(d,wrong,6));
    assert(ackCommand(d,role,7));assert(d.state()==State::initialNoLink);
    assert(!ackCommand(d,role,8)); // duplicated prior command, same epoch
    assert(ack(d,b,9)&&finish(d,b,10));
    assert(!d.authorizePort({},11));
    assert(d.connect(peer(),12)&&finish(d,b,13));const auto t=d.associationToken();
    assert(!d.associationReceived(t,assoc(),14)); // authentication evidence is mandatory
    assert(!d.authenticated({1,t.operation+1},peer().bssid,true,15));
    assert(!d.authenticated(t,address(3),true,16));
    assert(d.authenticated(t,peer().bssid,true,17));
    auto invalid=assoc();invalid.aid=0;assert(!d.associationReceived(t,invalid,18));
    invalid=assoc();invalid.bssid=address(3);assert(!d.associationReceived(t,invalid,19));
    assert(d.associationReceived(t,assoc(),20)&&finish(d,b,21));
    assert(!d.associated()&&!d.portAuthorized());
    assert(ack(d,b,22));assert(!d.associated()); // actual CAM completion still outstanding
    assert(finish(d,b,23)&&d.associated()&&!d.portAuthorized());
    assert(!d.authorizePort({1,t.operation+1},24));
    assert(d.authorizePort(t,25));
    assert(disconnect(d,b,26));assert(!d.authorizePort(t,30));
    assert(connect(d,b,31));assert(!same(t,d.associationToken()));assert(!d.authorizePort(t,38));
}
static void epochsAndTimeouts(){
    // Hardware action may include draining prior DMA and a full RFK tune;
    // passing the old 2s limit does not invent completion/readiness.
    {Backend b;Device d(b);assert(d.restart(1,0)&&d.start(interface(),1));
     assert(d.tick(2000001)&&d.tick(12000000)&&b.commands.empty());
     assert(!d.tick(12000001)&&d.requiresRecovery()&&b.commands.empty());}
    {Backend b;Device d(b);b.resetProven=false;assert(!d.restart(1,0));b.resetProven=true;assert(boot(d,b));
     assert(!d.restart(1,6));assert(!d.restart(2,6));b.expectedEpoch=2;assert(d.restart(2,6));}
    {Backend b;Device d(b);b.reuseSequence=true;assert(d.restart(1,0)&&d.start(interface(),1)&&finish(d,b,2));
     assert(ack(d,b,3));assert(d.error()==Error::none&&b.commands.size()==2&&b.faults==0);}
    {Backend b;Device d(b);assert(d.restart(1,0)&&d.start(interface(),1)&&finish(d,b,2));
     const auto packet=b.commands.back();assert(!d.tick(2000002));assert(d.requiresRecovery());
     assert(!ackCommand(d,packet,2000003));assert(!d.start(interface(),2000004));
     b.expectedEpoch=2;b.nextSequence=0;assert(d.restart(2,0)&&d.start(interface(),1)&&finish(d,b,2));
     assert(!ackCommand(d,packet,3,0,true,1));assert(d.state()==State::creatingRole);}
    {Backend b;Device d(b);assert(boot(d,b));assert(d.connect(peer(),10)&&finish(d,b,11));
     assert(d.tick(10000011)&&d.state()==State::disconnectingCmac);assert(!d.requiresRecovery());
     assert(finish(d,b,10000012)&&ack(d,b,10000013)&&finish(d,b,10000014));assert(d.state()==State::idle&&d.error()==Error::timeout);}
    {Backend b;Device d(b);assert(boot(d,b));assert(!d.tick(4)&&d.error()==Error::clock);}
    {Backend b;Device d(b);assert(d.restart(1,UINT64_MAX-1));assert(!d.start(interface(),UINT64_MAX-1));assert(d.error()==Error::clock);}
    {Backend b;Device d(b);assert(d.restart(1,0)&&d.start(interface(),1)&&finish(d,b,2));
     assert(!ackCommand(d,b.commands.back(),3,7));assert(d.error()==Error::firmwareRejected&&b.mode==Traffic::none);}
    {Backend b;Device d(b);assert(boot(d,b));uint64_t now=10;
     for(unsigned i=0;i<127;++i){assert(connect(d,b,now));now+=10;assert(disconnect(d,b,now));now+=10;}
     assert(b.commands.size()==256&&b.nextSequence==256);assert(d.connect(peer(),now)&&finish(d,b,now+1));
     assert(d.authenticated(d.associationToken(),peer().bssid,true,now+2)&&d.associationReceived(d.associationToken(),assoc(),now+3));
     assert(finish(d,b,now+4)&&b.commands.size()==257);assert(ack(d,b,now+5));}
}
static void cancellations(){
    // Cancel at scanBegin, tune pending, dwell, restore pending, end pending.
    for(unsigned step=0;step<5;++step){
        Backend b;Device d(b);assert(boot(d,b));ScanChannel c{{0,0,6,6},10,true};
        assert(d.scan(&c,1,10));uint64_t now=11;
        if(step>=1)assert(finish(d,b,now++));
        if(step>=2)assert(finish(d,b,now++));
        if(step>=3){now+=10000;assert(d.tick(now++));}
        if(step>=4)assert(finish(d,b,now++));
        const auto t=d.scanToken();assert(d.cancelScan(t,now++));
        while(d.state()!=State::idle){assert(finish(d,b,now++));}
        assert(b.scanCompletions==1&&b.lastScanCancelled&&b.mode==Traffic::management);
        assert(!d.cancelScan(t,now));
    }
    // Disconnect while each association phase owns an asynchronous operation.
    for(unsigned step=0;step<5;++step){
        Backend b;Device d(b);assert(boot(d,b));assert(d.connect(peer(),10));uint64_t now=11;
        if(step>=1)assert(finish(d,b,now++));
        if(step>=2){assert(d.authenticated(d.associationToken(),peer().bssid,true,now++));assert(d.associationReceived(d.associationToken(),assoc(),now++));}
        if(step>=3)assert(finish(d,b,now++));
        if(step>=4)assert(ack(d,b,now++));
        assert(d.disconnect(now++));assert(b.mode==Traffic::none);
        unsigned loops=0;
        while(d.state()!=State::idle){assert(++loops<10);if(d.commandPending())assert(ack(d,b,now++));else assert(finish(d,b,now++));}
        assert(!d.associated()&&b.cancellations==1&&b.mode==Traffic::management);
    }
    {Backend b;Device d(b);assert(boot(d,b)&&connect(d,b,10));assert(d.authorizePort(d.associationToken(),17));
     ScanChannel c{{0,0,6,6},10,true};assert(d.scan(&c,1,18)&&finish(d,b,19)&&finish(d,b,20));
     assert(d.disconnect(21));assert(finish(d,b,22)&&finish(d,b,23));
     assert(d.state()==State::disconnectingCmac&&b.mode==Traffic::none);assert(finish(d,b,24)&&ack(d,b,25)&&finish(d,b,26));}
    {Backend b;Device d(b);assert(boot(d,b)&&connect(d,b,10));const auto t=d.associationToken();assert(d.authorizePort(t,17));
     ScanChannel c{{0,0,6,6},10,true};assert(d.scan(&c,1,18)&&finish(d,b,19)&&finish(d,b,20));
     assert(d.revokePort(t,21)&&d.cancelScan(d.scanToken(),22));assert(finish(d,b,23)&&finish(d,b,24));
     assert(d.associated()&&!d.portAuthorized()&&b.mode==Traffic::controlledPort);
     assert(d.authorizePort(t,25)&&d.revokePort(t,26));assert(d.state()==State::associated);}
    {Backend b;Device d(b);assert(boot(d,b)&&d.connect(peer(),10)&&finish(d,b,11));
     assert(d.authenticated(d.associationToken(),peer().bssid,false,12));assert(d.error()==Error::authentication);
     assert(finish(d,b,13)&&ack(d,b,14)&&finish(d,b,15)&&d.state()==State::idle);}
    {Backend b;Device d(b);assert(boot(d,b)&&d.connect(peer(),10)&&finish(d,b,11));const auto t=d.associationToken();
     assert(d.authenticated(t,peer().bssid,true,12));assert(!d.associationRejected(t,peer().bssid,0,13));
     assert(d.associationRejected(t,peer().bssid,17,14));assert(d.error()==Error::association);
     assert(finish(d,b,15)&&ack(d,b,16)&&finish(d,b,17)&&d.state()==State::idle);}
}
static bool wholeFlow(Device &d,Backend &b){
    return boot(d,b)&&scan(d,b,10)&&connect(d,b,60000)&&d.authorizePort(d.associationToken(),60007)&&
        scan(d,b,70000)&&disconnect(d,b,130000)&&d.stop(130004)&&ack(d,b,130005)&&finish(d,b,130006);
}
static void faultsAndValidation(){
    Backend baseline;Device original(baseline);assert(wholeFlow(original,baseline));
    const auto total=baseline.operations;
    for(unsigned fail=1;fail<=total;++fail){
        Backend b;b.failAt=fail;Device d(b);assert(!wholeFlow(d,b));
        if(fail==1){assert(d.state()==State::stopped);continue;}
        assert(d.requiresRecovery()&&b.faults==1&&b.mode==Traffic::none);
        assert(!d.authorizePort(d.associationToken(),200000));
    }
    for(unsigned which=0;which<12;++which){
        // Every asynchronous action in the complete flow must reject failed work.
        Backend b;Device d(b);assert(d.restart(1,0)&&d.start(interface(),1));
        bool injected=false;uint64_t now=2;
        auto complete=[&](){const auto request=b.requests.back();const bool bad=unsigned(request.action)==which;
            const bool result=finish(d,b,now++,!bad);if(bad){assert(!result&&d.requiresRecovery());injected=true;}return result;};
        if(!complete())continue;
        assert(ack(d,b,now++)&&ack(d,b,now++));if(!complete())continue;
        ScanChannel c{{0,0,6,6},10,true};assert(d.scan(&c,1,now++));if(!complete())continue;if(!complete())continue;
        now+=10000;assert(d.tick(now++));if(!complete())continue;if(!complete())continue;
        assert(d.connect(peer(),now++));if(!complete())continue;
        assert(d.authenticated(d.associationToken(),peer().bssid,true,now++));assert(d.associationReceived(d.associationToken(),assoc(),now++));
        if(!complete())continue;assert(ack(d,b,now++));if(!complete())continue;
        assert(d.disconnect(now++));if(!complete())continue;assert(ack(d,b,now++));if(!complete())continue;
        assert(d.stop(now++)&&ack(d,b,now++));if(!complete())continue;
        assert(injected);
    }
    {Backend b;Device d(b);assert(boot(d,b));b.gated=false;assert(!d.connect(peer(),10));assert(d.state()==State::idle);b.gated=true;
     ScanChannel invalid{{1,2,42,36},20,true};assert(!d.scan(&invalid,1,11));invalid={{0,0,1,1},0,true};assert(!d.scan(&invalid,1,12));
     auto p=peer();p.bssid.bytes[0]=1;assert(!d.connect(p,13));p=peer();p.ssidLength=33;assert(!d.connect(p,14));
     assert(d.connect(peer(),15));const auto r=b.requests.back();assert(!d.actionComplete({1,r.token.operation+1},r.action,true,16));
     assert(!d.actionComplete(r.token,Action::idleTables,true,17));assert(d.state()==State::preparingAuthentication);}
    {Backend b;Device d(b);assert(d.restart(1,0));auto c=interface();c.macIndex=1;assert(!d.start(c,1));
     c=interface();c.macid=128;assert(!d.start(c,2));c=interface();c.port=5;assert(!d.start(c,3));
     assert(d.state()==State::stopped&&b.commands.empty()&&b.requests.empty());}
    printf("station backend fault positions: %u\n",total);
}
int main(){happyAndWire();acknowledgments();epochsAndTimeouts();cancellations();faultsAndValidation();puts("station controller tests passed");}
