// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/NativeWclScanPlan.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace w=rtl8852be::network::nativewclscan;
namespace bridge=rtl8852be::network::nativewclscanplan;
namespace fg=rtl8852be::network::foregroundscan;
static unsigned checks;
static void check(bool value,int line){++checks;if(!value){std::fprintf(stderr,"line %d\n",line);std::abort();}}
#define CHECK(value) check(bool(value),__LINE__)
static bool zero(const void *p,size_t length){
    const auto *bytes=static_cast<const uint8_t*>(p);
    for(size_t i=0;i<length;++i)if(bytes[i])return false;
    return true;
}
int main(){
    w::Request request{};request.mode=w::Mode::active;
    request.activeDwellMs=80;request.passiveDwellMs=120;
    request.channelCount=3;request.channels[0]=11;request.channels[1]=6;request.channels[2]=1;
    const w::Result decoded{w::Status::knownSubset,false};
    fg::RequestedPlan out{};
    auto result=bridge::map(decoded,request,out);
    CHECK(result.status==bridge::Status::planned&&!result.emissionReady);
    CHECK(out.active&&out.dwellMs==80&&out.channelCount==3);
    CHECK(out.channels[0]==11&&out.channels[1]==6&&out.channels[2]==1);
    request.mode=w::Mode::passive;
    result=bridge::map(decoded,request,out);
    CHECK(result.status==bridge::Status::planned&&!out.active&&out.dwellMs==120);
    request.homeAwayMs=50;
    result=bridge::map(decoded,request,out);
    CHECK(result.status==bridge::Status::unsupportedHomeTiming&&zero(&out,sizeof(out)));
    request.homeAwayMs=0;request.homeRestMs=10;
    result=bridge::map(decoded,request,out);
    CHECK(result.status==bridge::Status::unsupportedHomeTiming&&zero(&out,sizeof(out)));
    request.homeRestMs=0;request.passiveDwellMs=9;
    result=bridge::map(decoded,request,out);
    CHECK(result.status==bridge::Status::unsupportedDwell&&zero(&out,sizeof(out)));
    request.passiveDwellMs=100;request.channels[1]=11;
    result=bridge::map(decoded,request,out);
    CHECK(result.status==bridge::Status::invalidChannels&&zero(&out,sizeof(out)));
    request.channels[1]=6;request.channelCount=0;
    result=bridge::map(decoded,request,out);
    CHECK(result.status==bridge::Status::invalidChannels&&zero(&out,sizeof(out)));
    request.channelCount=3;
    result=bridge::map({w::Status::unsupportedFilters,false},request,out);
    CHECK(result.status==bridge::Status::notDecoded&&zero(&out,sizeof(out)));
    std::printf("native WCL scan plan: %u checks passed; no WCL emission\n",checks);
}
