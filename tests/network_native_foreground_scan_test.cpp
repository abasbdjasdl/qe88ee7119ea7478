// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/NativeForegroundScan.hpp"
#include "../src/network/NativeScanObservation.hpp"
#include "../src/network/StationController.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
namespace fg=rtl8852be::network::foregroundscan;
namespace ns=rtl8852be::network::nativescan;
namespace st=rtl8852be::station;
namespace net=rtl8852be::network;
static unsigned checks;
static void check(bool ok,const char *expression,int line){
    ++checks;if(!ok){std::fprintf(stderr,"line %d: %s\n",line,expression);std::abort();}
}
#define CHECK(x) check(bool(x),#x,__LINE__)
static bool zero(const void *p,size_t size){
    const auto *b=static_cast<const unsigned char*>(p);for(size_t i=0;i<size;++i)if(b[i])return false;return true;
}
static const ns::Channel plan[]={{ns::Band::ghz2,1},{ns::Band::ghz2,6},{ns::Band::ghz2,11}};
static fg::Readiness ready(){return {true,true,true,false,false,false,false};}

static void admission(){
    fg::Controller c;fg::Status out;
    for(unsigned reason=0;reason<7;++reason){
        auto r=ready();
        if(reason==0)r.ready=false;
        if(reason==1)r.stationIdle=false;
        if(reason==2)r.protocolIdle=false;
        if(reason==3)r.connected=true;
        if(reason==4)r.pendingJoin=true;
        if(reason==5)r.legacyCredentials=true;
        if(reason==6)r.otherWork=true;
        std::memset(&out,0xa5,sizeof(out));
        CHECK(c.begin(r,false,1,10,plan,3,out)==(reason?fg::Admission::busy:fg::Admission::notReady));
        CHECK(zero(&out,sizeof(out))&&!c.active());
    }
    CHECK(c.begin(ready(),true,1,10,plan,3,out)==fg::Admission::unsupported&&zero(&out,sizeof(out)));
    CHECK(c.begin(ready(),false,0,10,plan,3,out)==fg::Admission::invalid);
    CHECK(c.begin(ready(),false,1,10,nullptr,3,out)==fg::Admission::invalid);
    CHECK(c.begin(ready(),false,1,10,plan,0,out)==fg::Admission::invalid);
    CHECK(c.begin(ready(),false,1,10,plan,65,out)==fg::Admission::invalid);
    CHECK(c.begin(ready(),false,1,UINT64_MAX-fg::timeoutUs+1,plan,3,out)==fg::Admission::invalid);
    const ns::Channel duplicates[]={plan[0],plan[0]},invalid[]={{ns::Band::ghz2,0}};
    CHECK(c.begin(ready(),false,1,10,duplicates,2,out)==fg::Admission::invalid);
    CHECK(c.begin(ready(),false,1,10,invalid,1,out)==fg::Admission::invalid);
    ns::Channel owned[]={plan[0],plan[1]};
    CHECK(c.begin(ready(),false,2,10,owned,2,out)==fg::Admission::accepted);
    CHECK(out.phase==fg::Phase::queued&&out.drained&&out.plannedChannels==2&&!out.snapshot.generation);
    owned[0]=plan[2];ns::Channel next;
    CHECK(c.next(next)&&ns::same(next,plan[0])); // copied, not borrowed
    const auto token=out.token;
    CHECK(c.begin(ready(),false,2,11,plan,3,out)==fg::Admission::busy&&zero(&out,sizeof(out)));
    CHECK(!c.copy({token.epoch,token.request+1},out)&&zero(&out,sizeof(out)));
    CHECK(!c.cancel({1,token.request},fg::Reason::caller,11)&&c.active());
    CHECK(c.cancel(token,fg::Reason::caller,11));CHECK(c.status().drained&&!c.active());
    CHECK(c.begin(ready(),false,1,12,plan,3,out)==fg::Admission::invalid);
    CHECK(c.begin(ready(),false,2,12,plan,3,out)==fg::Admission::accepted);
    CHECK(out.token.request>token.request&&!c.copy(token,out)&&zero(&out,sizeof(out)));
}

static void stateAndDeadlines(){
    fg::Controller c;fg::Status out;
    CHECK(c.begin(ready(),false,3,100,plan,2,out)==fg::Admission::accepted);
    CHECK(!c.complete({1,3},101));
    CHECK(!c.accepted({2,1})&&!c.accepted({3,0}));CHECK(c.accepted({3,5}));
    CHECK(c.observing({3,5})&&!c.observing({3,6})&&!c.accepted({3,6}));
    CHECK(!c.channelFinished({3,6},false,102)&&c.status().completedChannels==0);
    CHECK(c.channelFinished({3,5},false,102));CHECK(!c.channelFinished({3,5},false,103));
    CHECK(!c.accepted({3,5})&&c.accepted({3,6}));
    CHECK(c.channelFinished({3,6},false,104)&&c.readyToFinish());
    CHECK(!c.complete({},105)&&!c.complete({1,4},105));
    CHECK(c.complete({9,3},105)&&c.status().phase==fg::Phase::complete);
    CHECK(c.status().snapshot.generation==9&&c.status().drained);

    CHECK(c.begin(ready(),false,3,200,plan,2,out)==fg::Admission::accepted);
    const auto token=out.token;
    CHECK(c.accepted({3,7}));CHECK(!c.expired(out.deadlineUs-1));
    CHECK(c.expired(out.deadlineUs)&&c.draining()&&!c.status().drained);
    CHECK(!c.observing({3,7})&&!c.complete({10,3},out.deadlineUs));
    CHECK(c.cancel(token,fg::Reason::caller,out.deadlineUs+1));
    CHECK(c.status().reason==fg::Reason::timeout); // first reason wins
    CHECK(c.channelFinished({3,7},false,out.deadlineUs+2)); // racing success still cancels
    CHECK(c.status().phase==fg::Phase::timedOut&&c.status().drained&&!c.status().snapshot.generation);
    CHECK(c.begin(ready(),false,3,300+fg::timeoutUs,plan,1,out)==fg::Admission::accepted);
    CHECK(c.expired(out.deadlineUs)&&c.status().phase==fg::Phase::timedOut&&c.status().drained);

    CHECK(c.begin(ready(),false,3,400+2*fg::timeoutUs,plan,1,out)==fg::Admission::accepted);
    CHECK(c.accepted({3,8}));c.fail(fg::Reason::backend,401+2*fg::timeoutUs);
    CHECK(c.status().phase==fg::Phase::failed&&!c.status().drained&&!c.active());
    CHECK(c.begin(ready(),false,3,402+2*fg::timeoutUs,plan,1,out)==fg::Admission::busy);
    c.hardwareStopped(403+2*fg::timeoutUs);CHECK(c.status().drained);
    CHECK(c.begin(ready(),false,4,404+2*fg::timeoutUs,plan,1,out)==fg::Admission::accepted);
    CHECK(c.accepted({4,1})); // physical epoch permits fresh station sequence
    CHECK(!c.clockValid(out.startedAtUs-1));
    CHECK(c.channelFinished({4,1},false,out.startedAtUs-1));
    CHECK(c.status().phase==fg::Phase::failed&&c.status().reason==fg::Reason::clock);
}

// Actual StationController plus real Observer/Store, driven by a deterministic
// hardware backend. No net80211 S_SCAN state is needed to collect the air frame.
struct Harness {
    fg::Controller scan;std::unique_ptr<ns::Observer> observer{new ns::Observer};
    st::Controller<Harness> station{*this};
    std::vector<st::ActionRequest> actions;std::vector<std::array<uint8_t,12>> commands;
    uint64_t now{10};unsigned sequence{},probes{},authStarts{},callbacks{},faults{},launches{};
    bool inCallback{},deferAdvance{},permitAuthentication{};st::Traffic traffic{st::Traffic::none};
    bool inGate(){return true;}
    bool setTraffic(st::Traffic value){traffic=value;return true;}
    bool reserveH2cSequence(uint8_t &out){out=uint8_t(sequence++);return true;}
    bool publishH2c(const uint8_t *data,size_t bytes,st::Token){
        CHECK(bytes==12);std::array<uint8_t,12> copy{};std::memcpy(copy.data(),data,bytes);commands.push_back(copy);return true;
    }
    bool beginAction(const st::ActionRequest &action){CHECK(!inCallback);actions.push_back(action);return true;}
    bool beginAuthentication(st::Token,const st::Peer&){++authStarts;return permitAuthentication;}
    bool sendProbe(st::Token,const st::Peer*,const st::ScanChannel&){++probes;return false;}
    bool cancelProtocol(st::Token){return true;}
    bool firmwareRestartVerified(uint64_t epoch){return epoch==1;}
    void recoveryRequired(st::Token,st::Error){++faults;scan.fail(fg::Reason::backend,now);observer->cancel();}
    void scanFinished(st::Token token,bool cancelled){
        inCallback=true;++callbacks;const unsigned submitted=launches;
        CHECK(scan.owns({token.epoch,token.operation}));
        if(cancelled||scan.draining())observer->cancel();
        else CHECK(observer->channelFinished({token.epoch,token.operation},false));
        CHECK(scan.channelFinished({token.epoch,token.operation},cancelled,now));
        CHECK(st::same(station.scanToken(),token)); // it is cleared only after return
        CHECK(launches==submitted);deferAdvance=true;inCallback=false;
    }
    bool finish(bool ok=true){
        const auto action=actions.back();++now;return station.actionComplete(action.token,action.action,ok,now);
    }
    bool ack(){
        auto packet=commands.back();uint8_t bytes[4]{};
        net::store32(bytes,(net::little32(packet.data())&0xffff)|(uint32_t(packet[3])<<24));
        return station.firmwareEvent({{1,0,1},bytes,4},1,++now);
    }
    Harness(){
        st::Interface interface{};interface.address={{2,1,2,3,4,5}};interface.home={0,0,1,1};
        CHECK(station.restart(1,0)&&station.start(interface,1));
        now=1;CHECK(finish()&&ack()&&ack()&&finish());CHECK(station.state()==st::State::idle);
    }
    bool launch(){
        CHECK(!inCallback);ns::Channel channel;if(!scan.next(channel))return false;
        st::ScanChannel dwell{{uint8_t(channel.band==ns::Band::ghz5),0,channel.number,channel.number},fg::dwellMs,false};
        CHECK(station.scan(&dwell,1,++now));const auto token=station.scanToken();
        CHECK(scan.accepted({token.epoch,token.operation}));
        CHECK(observer->channelAccepted(1,fg::observerMode,false,channel,{token.epoch,token.operation}));
        ++launches;return true;
    }
    fg::Token begin(size_t count=3){
        fg::Status out;CHECK(scan.begin(ready(),false,1,++now,plan,count,out)==fg::Admission::accepted);
        CHECK(observer->begin(1,fg::observerMode,false,plan,count));CHECK(launch());return out.token;
    }
    void air(){
        CHECK(station.state()==st::State::scanningDwell);const auto token=station.scanToken();
        const auto channel=plan[scan.status().completedChannels];
        std::vector<uint8_t> frame(36,0);frame[0]=0x80;frame[32]=100;
        for(unsigned i=0;i<6;++i)frame[4+i]=0xff;
        const uint8_t bssid[]={2,1,2,3,4,channel.number};
        std::memcpy(frame.data()+10,bssid,6);std::memcpy(frame.data()+16,bssid,6);
        frame.insert(frame.end(),{0,1,'x',1,1,0x82,3,1,channel.number});
        CHECK(observer->observe({token.epoch,token.operation},frame.data(),frame.size(),channel,
                                {ns::SignalUnit::dbm,-55},now));
    }
    void toPhase(unsigned phase){
        if(phase>=1)CHECK(finish()); // begin -> tune
        if(phase>=2)CHECK(finish()); // tune -> dwell
        if(phase>=3){now+=uint64_t(fg::dwellMs)*1000;CHECK(station.tick(now));} // restore
        if(phase>=4)CHECK(finish()); // restore -> end
    }
    void finishDwell(){toPhase(2);air();now+=uint64_t(fg::dwellMs)*1000;
        CHECK(station.tick(now));CHECK(finish()&&finish());CHECK(station.state()==st::State::idle);
        CHECK(!station.scanToken().operation&&deferAdvance);
    }
    void advanceSamePoll(){
        if(deferAdvance)return;
        if(scan.readyToFinish()){
            CHECK(observer->finish(1,fg::observerMode,false,++now));ns::Summary summary;
            CHECK(observer->copySummary(summary));CHECK(scan.complete(summary.token,now));
        }else if(scan.active()&&!scan.draining())CHECK(launch());
    }
    void poll(){deferAdvance=false;advanceSamePoll();}
    void drain(){
        unsigned count=0;
        while(station.state()!=st::State::idle&&count++<5)CHECK(finish());
        CHECK(station.state()==st::State::idle&&count<=5);
    }
};

static void realPass(){
    Harness h;const auto token=h.begin();
    for(unsigned i=0;i<3;++i){
        const unsigned launches=h.launches;h.finishDwell();
        CHECK(h.scan.status().completedChannels==i+1&&h.scan.status().phase==fg::Phase::queued);
        h.advanceSamePoll();CHECK(h.launches==launches);h.poll();
    }
    fg::Status done;CHECK(h.scan.copy(token,done)&&done.phase==fg::Phase::complete&&done.drained);
    CHECK(done.snapshot.generation&&done.plannedChannels==3&&done.completedChannels==3);
    ns::Summary summary;CHECK(h.observer->copySummary(summary)&&ns::same(summary.token,done.snapshot));
    CHECK(summary.channelCount==3&&summary.count==3&&h.probes==0&&h.authStarts==0&&h.faults==0);
    unsigned tuned=0,restored=0;
    for(const auto &action:h.actions){
        if(action.action==st::Action::scanTune){
            CHECK(!action.activeScan&&action.channel.band==0&&action.channel.width==0);
            CHECK(action.channel.primary==plan[tuned++].number);
        }
        if(action.action==st::Action::scanRestore){++restored;CHECK(action.channel.primary==1);}
    }
    CHECK(tuned==3&&restored==3&&h.callbacks==3);
    // A cancelled replacement keeps the last complete cache untouched.
    const auto replacement=h.begin(1);CHECK(h.scan.cancel(replacement,fg::Reason::selection,++h.now));
    h.observer->cancel();CHECK(h.station.disconnect(h.now));CHECK(!h.scan.status().drained);
    h.drain();CHECK(h.scan.status().phase==fg::Phase::cancelled&&h.scan.status().drained);
    ns::Summary kept;CHECK(h.observer->copySummary(kept)&&ns::same(kept.token,summary.token));
}

static void cancellationAndFailure(){
    for(unsigned phase=0;phase<5;++phase){
        Harness h;const auto token=h.begin(1);h.toPhase(phase);
        const auto operation=h.station.scanToken();
        CHECK(h.scan.cancel(token,fg::Reason::caller,++h.now));h.observer->cancel();
        CHECK(h.station.cancelScan(operation,h.now));
        CHECK(h.scan.draining()&&!h.scan.status().drained&&h.callbacks==0);
        CHECK(!h.scan.channelFinished({operation.epoch,operation.operation+1},true,h.now));
        h.drain();CHECK(h.scan.status().phase==fg::Phase::cancelled&&h.scan.status().drained);
        CHECK(h.callbacks==1&&h.probes==0&&h.authStarts==0&&h.faults==0);
    }
    for(unsigned phase:std::array<unsigned,4>{{0,1,3,4}}){
        Harness h;h.begin(1);h.toPhase(phase);CHECK(!h.finish(false));
        CHECK(h.station.requiresRecovery()&&h.faults==1&&h.callbacks==0);
        CHECK(h.scan.status().phase==fg::Phase::failed&&!h.scan.status().drained);
        h.scan.hardwareStopped(++h.now);CHECK(h.scan.status().drained);
    }
    {Harness h;h.begin(1);h.now+=12000000;CHECK(!h.station.tick(h.now));
     CHECK(h.faults==1&&h.callbacks==0&&h.scan.status().phase==fg::Phase::failed);}
    for(auto reason:{fg::Reason::selection,fg::Reason::disabled,fg::Reason::shutdown}){
        fg::Controller c;fg::Status out;CHECK(c.begin(ready(),false,1,10,plan,1,out)==fg::Admission::accepted);
        CHECK(c.accepted({1,1})&&c.cancel(out.token,reason,11)&&!c.status().drained);
        c.hardwareStopped(12);CHECK(c.status().drained&&!c.active()&&!c.status().snapshot.generation);
        CHECK(c.status().reason==reason);
    }
}

static void connectedRemainsConnected(){
    Harness h;h.permitAuthentication=true;
    st::Peer peer{};peer.bssid={{2,2,3,4,5,6}};peer.channel={0,0,6,6};
    peer.ssidLength=1;peer.ssid[0]='x';
    CHECK(h.station.connect(peer,++h.now)&&h.finish());
    const auto association=h.station.associationToken();
    CHECK(h.station.authenticated(association,peer.bssid,true,++h.now));
    CHECK(h.station.associationReceived(association,{peer.bssid,peer.channel,1},++h.now));
    CHECK(h.finish()&&h.ack()&&h.finish());
    CHECK(h.station.authorizePort(association,++h.now));
    const auto actionCount=h.actions.size(),commandCount=h.commands.size();
    auto readiness=ready();readiness.stationIdle=h.station.state()==st::State::idle;
    readiness.connected=h.station.associated();readiness.protocolIdle=false;
    fg::Status out;std::memset(&out,0x7c,sizeof(out));
    CHECK(h.scan.begin(readiness,false,1,++h.now,plan,3,out)==fg::Admission::busy);
    CHECK(zero(&out,sizeof(out))&&h.station.portAuthorized()&&h.traffic==st::Traffic::authorized);
    CHECK(st::same(association,h.station.associationToken()));
    CHECK(h.actions.size()==actionCount&&h.commands.size()==commandCount&&h.callbacks==0&&h.launches==0);
}

int main(){admission();stateAndDeadlines();realPass();cancellationAndFailure();connectedRemainsConnected();
    std::printf("native foreground scan: %u checks passed\n",checks);}
