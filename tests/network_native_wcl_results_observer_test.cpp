// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/NativeScanObservation.hpp"
#include "../src/network/NativeForegroundScan.hpp"
#include "../src/network/NativeWclScanResults.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace ns=rtl8852be::network::nativescan;
namespace fg=rtl8852be::network::foregroundscan;
namespace ws=rtl8852be::network::nativewclscan;
namespace wb=rtl8852be::network::nativewclbeacon;
namespace wr=rtl8852be::network::nativewclresults;
static unsigned checks;
static void check(bool yes,const char *what,int line){
    ++checks;if(!yes){std::fprintf(stderr,"line %d: %s\n",line,what);std::abort();}
}
#define CHECK(x) check(bool(x),#x,__LINE__)

int main(){
    auto observer=std::make_unique<ns::Observer>();
    auto scratch=std::make_unique<uint8_t[]>(wb::maxPayloadBytes);
    auto payload=std::make_unique<uint8_t[]>(wb::maxPayloadBytes);
    fg::Controller foreground;wr::Bridge bridge;
    const ns::Channel channel{ns::Band::ghz2,6};
    fg::Readiness ready{};ready.ready=ready.stationIdle=ready.protocolIdle=true;
    fg::Status started{};
    CHECK(foreground.begin(ready,false,7,100,&channel,1,120,started)==fg::Admission::accepted);
    CHECK(observer->begin(7,fg::observerMode,false,&channel,1));
    CHECK(foreground.accepted({7,1}));
    CHECK(observer->channelAccepted(7,fg::observerMode,false,channel,{7,1}));
    uint8_t beacon[47]{};
    beacon[0]=0x80;
    for(unsigned i=0;i<6;++i)beacon[4+i]=0xff;
    const uint8_t bssid[6]={2,1,2,3,4,5};
    for(unsigned i=0;i<6;++i)beacon[10+i]=beacon[16+i]=bssid[i];
    beacon[32]=100;beacon[34]=1;
    beacon[36]=0;beacon[37]=1;beacon[38]='A';
    beacon[39]=1;beacon[40]=1;beacon[41]=0x82;
    beacon[42]=3;beacon[43]=1;beacon[44]=6;
    // Two trailing bytes are a well-formed vendor-independent empty IE.
    beacon[45]=42;beacon[46]=0;
    CHECK(observer->observe({7,1},beacon,sizeof(beacon),channel,
                            {ns::SignalUnit::percent,67},110));
    CHECK(observer->channelFinished({7,1},false));
    CHECK(foreground.channelFinished({7,1},false,130));
    CHECK(observer->finish(7,fg::observerMode,false,160));
    ns::Summary summary{};CHECK(observer->copySummary(summary));
    CHECK(foreground.complete(summary.token,160));
    ws::Request request{};request.mode=ws::Mode::passive;
    request.bssType=ws::BssType::infrastructure;
    request.activeDwellMs=request.passiveDwellMs=120;
    request.channelCount=1;request.channels[0]=6;
    const auto profile=wb::TargetProfile::darwin24_4_0_d8b50fc2;
    CHECK(bridge.begin(profile,true,request,foreground.status(),
                       observer->completedStoreUnderGate(),scratch.get(),
                       wb::maxPayloadBytes)==wr::Status::ready);
    wr::Frame draft{};
    CHECK(bridge.reserve(foreground.status(),observer->completedStoreUnderGate(),
                         payload.get(),wb::maxPayloadBytes,draft)==wr::Status::frameReady);
    CHECK(draft.event==201&&!draft.emissionReady&&draft.request.request==started.token.request);
    // Replacing the observer publication must reject this reserved draft;
    // a scan result cannot be attributed to a newer generation.
    CHECK(observer->begin(7,fg::observerMode,false,&channel,1));
    CHECK(observer->channelAccepted(7,fg::observerMode,false,channel,{7,2}));
    CHECK(observer->channelFinished({7,2},false));
    CHECK(observer->finish(7,fg::observerMode,false,170));
    CHECK(!bridge.commit(draft,true,foreground.status(),observer->completedStoreUnderGate()));
    CHECK(bridge.phase()==wr::Phase::aborted&&bridge.retire());
    std::printf("Native WCL result/observer handoff: %u checks passed\n",checks);
}
