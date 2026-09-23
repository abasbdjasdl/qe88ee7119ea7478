// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/NativeScanObservation.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
using namespace rtl8852be::network::nativescan;

// Intentionally independent of assert/NDEBUG: every operation always executes.
static unsigned checks;
static void check(bool condition,const char *expression,int line){
    ++checks;
    if(!condition){std::fprintf(stderr,"line %d: %s\n",line,expression);std::abort();}
}
#define CHECK(x) check(bool(x),#x,__LINE__)
static const Channel channels[]={{Band::ghz2,6},{Band::ghz5,36}};
static const Signal signal={SignalUnit::percent,67};
static void ie(std::vector<uint8_t> &frame,uint8_t tag,const std::vector<uint8_t> &bytes){
    CHECK(bytes.size()<=255);
    frame.push_back(tag);frame.push_back(uint8_t(bytes.size()));
    frame.insert(frame.end(),bytes.begin(),bytes.end());
}
static std::vector<uint8_t> frameFor(Channel channel,bool probe=false,bool hidden=false){
    std::vector<uint8_t> frame(36,0);
    frame[0]=probe?0x50:0x80;
    for(unsigned i=0;i<6;++i)frame[4+i]=0xff;
    const uint8_t bssid[6]={2,0x11,0x22,0x33,0x44,0x55};
    std::memcpy(frame.data()+10,bssid,6);std::memcpy(frame.data()+16,bssid,6);
    frame[22]=0x30;frame[23]=0x12; // sequence 0x123, fragment zero
    frame[24]=0x99; // beacon TSF is deliberately unrelated to monotonic nowUs
    frame[32]=100;frame[34]=0x31;frame[35]=4;
    ie(frame,0,hidden?std::vector<uint8_t>{}:std::vector<uint8_t>{'s',0,'c','n'});
    ie(frame,1,{0x82,0x84});ie(frame,3,{channel.number});
    return frame;
}
static bool parse(const std::vector<uint8_t> &frame,Input &out,Channel channel=channels[0]){
    return advertisement(frame.data(),frame.size(),channel,signal,123456,out);
}
static bool empty(const Input &out){
    return !out.bssid&&!out.ssid&&!out.ies&&!out.ssidLength&&!out.ieLength&&
        !out.originalIeLength&&!out.observedAtUs&&!out.capability&&!out.beaconInterval;
}
static void reject(const std::vector<uint8_t> &frame,Channel channel=channels[0]){
    Input out;auto good=frameFor(channels[0]);CHECK(parse(good,out));
    CHECK(!parse(frame,out,channel));CHECK(empty(out));
}
static bool zero(const void *value,size_t size){
    const auto *bytes=static_cast<const uint8_t*>(value);
    for(size_t i=0;i<size;++i)if(bytes[i])return false;
    return true;
}
static void parsing(){
    Input out;auto frame=frameFor(channels[0]);CHECK(parse(frame,out));
    CHECK(out.bssid==frame.data()+16&&out.ies==frame.data()+36);
    CHECK(out.ssidLength==4&&out.ssid[0]=='s'&&out.ssid[1]==0);
    CHECK(out.ieLength==frame.size()-36&&out.ieLength==out.originalIeLength);
    CHECK(out.capability==0x431&&out.beaconInterval==100&&out.observedAtUs==123456);
    CHECK(same(out.channel,channels[0])&&out.signal.unit==SignalUnit::percent&&out.signal.value==67);
    frame=frameFor(channels[1],true);frame[4]=2;
    for(unsigned i=1;i<6;++i)frame[4+i]=uint8_t(i);
    CHECK(parse(frame,out,channels[1]));
    frame=frameFor(channels[0],false,true);CHECK(parse(frame,out)&&out.ssidLength==0);
    frame=frameFor(channels[0]);ie(frame,221,{1,2,3,4});ie(frame,255,{35});
    std::vector<uint8_t> ht(22,0);ht[0]=6;ie(frame,61,ht);CHECK(parse(frame,out));
    for(size_t n=0;n<36;++n){std::vector<uint8_t> shortFrame(n,0);reject(shortFrame);}
    CHECK(!advertisement(nullptr,40,channels[0],signal,1,out)&&empty(out));
    frame=frameFor(channels[0]);
    CHECK(!advertisement(frame.data(),frame.size(),{Band::ghz5,35},signal,1,out)&&empty(out));
    CHECK(!advertisement(frame.data(),frame.size(),channels[0],{SignalUnit::percent,-1},1,out)&&empty(out));
    CHECK(!advertisement(frame.data(),frame.size(),channels[0],{SignalUnit::percent,101},1,out)&&empty(out));
    for(uint8_t fc0:std::vector<uint8_t>{0x81,0x84,0x08,0x40,0x90}){
        auto bad=frame;bad[0]=fc0;reject(bad);
    }
    for(uint8_t fc1:std::vector<uint8_t>{1,2,3,4,0x40,0x80}){
        auto bad=frame;bad[1]=fc1;reject(bad);
    }
    auto bad=frame;bad[22]|=1;reject(bad);
    bad=frame;bad[10]^=2;reject(bad); // TA must match BSSID in this narrow observer
    bad=frame;std::memset(bad.data()+10,0,12);reject(bad);
    bad=frame;bad[10]|=1;bad[16]|=1;reject(bad);
    bad=frame;std::memset(bad.data()+4,0,6);reject(bad);
    bad=frame;bad[4]=1;reject(bad); // non-broadcast multicast DA
    bad=frame;bad[4]=2;reject(bad); // beacon destination must be broadcast
    bad=frameFor(channels[0],true);bad[4]=1;reject(bad);
    // FCS ownership contract: this parser receives a frame without FCS. It
    // cannot identify all possible CRC byte values; the DMA caller strips it.
    frame.insert(frame.end(),{0xde,0xad,0xbe,0xef});
    CHECK(advertisement(frame.data(),frame.size()-4,channels[0],signal,1,out));
    reject(frame); // these particular unstripped bytes form a truncated IE
}
static void elements(){
    auto frame=frameFor(channels[0]);Input out;
    auto bad=frame;bad.push_back(42);reject(bad);
    bad=frame;bad.insert(bad.end(),{42,3,1,2});reject(bad);
    bad=frame;bad.erase(bad.begin()+36,bad.begin()+42);reject(bad); // missing SSID
    bad=frame;bad.erase(bad.begin()+42,bad.begin()+46);reject(bad); // missing rates
    bad=frame;bad[37]=33;reject(bad);
    bad=frame;bad.erase(bad.begin()+36,bad.begin()+42);ie(bad,0,std::vector<uint8_t>(32,'x'));CHECK(parse(bad,out));
    bad=frame;bad.erase(bad.begin()+36,bad.begin()+42);ie(bad,0,std::vector<uint8_t>(33,'x'));reject(bad);
    bad=frame;ie(bad,0,{});reject(bad);
    bad=frame;ie(bad,1,{0x82});reject(bad);
    bad=frame;bad.erase(bad.begin()+42,bad.begin()+46);ie(bad,1,{});reject(bad);
    bad=frame;bad.erase(bad.begin()+42,bad.begin()+46);ie(bad,1,std::vector<uint8_t>(8,0x82));CHECK(parse(bad,out));
    bad=frame;bad.erase(bad.begin()+42,bad.begin()+46);ie(bad,1,std::vector<uint8_t>(9,0x82));reject(bad);
    bad=frame;bad[48]=11;reject(bad); // DS primary channel contradicts radio
    bad=frame;ie(bad,3,{6});reject(bad);
    bad=frame;bad.erase(bad.begin()+46,bad.end());ie(bad,3,{});reject(bad);
    bad=frame;bad.erase(bad.begin()+46,bad.end());ie(bad,3,{6,0});reject(bad);
    std::vector<uint8_t> ht(22,0);ht[0]=6;
    bad=frame;ie(bad,61,ht);ie(bad,61,ht);reject(bad);
    bad=frame;ht[0]=11;ie(bad,61,ht);reject(bad);ht[0]=6;
    bad=frame;ht.resize(21);ie(bad,61,ht);reject(bad);
    bad=frame;ht.resize(23);ie(bad,61,ht);reject(bad);
    bad=frame;ie(bad,255,{});reject(bad);
    const std::vector<uint8_t> rsn={1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,2,0,0};
    bad=frame;ie(bad,48,rsn);ie(bad,48,rsn);reject(bad);
    const std::vector<uint8_t> wpa={0,0x50,0xf2,1,1,0,0,0x50,0xf2,2,1,0,0,0x50,0xf2,2,1,0,0,0x50,0xf2,2};
    bad=frame;ie(bad,221,wpa);ie(bad,221,wpa);reject(bad);
    // Unknown well-formed IEs remain byte-for-byte observations, not inferred
    // authentication support. Multiple unrelated vendor IEs are legitimate.
    ie(frame,221,{1,2,3,4});ie(frame,221,{5,6,7,8});CHECK(parse(frame,out));
}
static Token publish(Observer &observer,uint64_t epoch,uint64_t firstOperation,uint64_t time){
    CHECK(observer.begin(epoch,17,true,channels,2));
    auto frame=frameFor(channels[0]);const DwellToken first{epoch,firstOperation},second{epoch,firstOperation+1};
    CHECK(observer.channelAccepted(epoch,17,true,channels[0],first));
    CHECK(observer.observe(first,frame.data(),frame.size(),channels[0],signal,time));
    CHECK(observer.channelFinished(first,false));
    CHECK(observer.channelAccepted(epoch,17,true,channels[1],second));
    frame=frameFor(channels[1]);CHECK(observer.observe(second,frame.data(),frame.size(),channels[1],signal,time+1));
    CHECK(observer.channelFinished(second,false));CHECK(observer.finish(epoch,17,true,time+2));
    Summary summary;CHECK(observer.copySummary(summary)&&summary.count==2&&summary.channelCount==2);
    return summary.token;
}
static void lifecycle(){
    auto owner=std::make_unique<Observer>();auto &observer=*owner;
    auto entry=std::make_unique<Entry>();Summary summary;Channel channel;
    std::memset(&summary,0xa5,sizeof(summary));CHECK(!observer.copySummary(summary)&&zero(&summary,sizeof(summary)));
    std::memset(entry.get(),0xa5,sizeof(*entry));CHECK(!observer.copyEntry({1,7},0,*entry)&&zero(entry.get(),sizeof(*entry)));
    CHECK(observer.begin(7,17,true,channels,2)&&observer.open());
    CHECK(!observer.begin(7,17,true,channels,2));
    const DwellToken first{7,40},second{7,41};auto frame=frameFor(channels[0]);
    CHECK(!observer.observe(first,frame.data(),frame.size(),channels[0],signal,100));
    CHECK(observer.channelAccepted(7,17,true,channels[0],first));
    CHECK(observer.observing(first,channels[0])&&!observer.observing({7,39},channels[0]));
    CHECK(!observer.observe({7,39},frame.data(),frame.size(),channels[0],signal,99));
    CHECK(!observer.channelFinished({7,39},true)&&observer.observing(first,channels[0]));
    CHECK(observer.observe(first,frame.data(),frame.size(),channels[0],signal,100));
    frame[38]='X'; // Store must have already copied the complete observation.
    CHECK(observer.channelFinished(first,false));
    CHECK(!observer.copySummary(summary)); // channel completion never publishes
    CHECK(!observer.channelFinished(first,false)); // stale completion is harmless
    CHECK(observer.channelAccepted(7,17,true,channels[1],second));
    frame=frameFor(channels[1]);CHECK(observer.observe(second,frame.data(),frame.size(),channels[1],signal,101));
    CHECK(observer.channelFinished(second,false));CHECK(!observer.copySummary(summary));
    CHECK(observer.finish(7,17,true,102)&&!observer.open());
    CHECK(observer.copySummary(summary)&&summary.count==2&&summary.channelCount==2);
    const Token published=summary.token;CHECK(same(published,{1,7}));
    CHECK(observer.copyEntry(published,0,*entry)&&same(entry->channel,channels[0])&&entry->ssid[0]=='s');
    CHECK(observer.copyEntry(published,1,*entry)&&same(entry->channel,channels[1]));
    CHECK(observer.copyChannel(published,1,channel)&&same(channel,channels[1]));
    for(unsigned kind=0;kind<3;++kind){
        std::memset(entry.get(),0xa5,sizeof(*entry));
        CHECK(!observer.copyEntry(kind==0?Token{published.generation+1,7}:kind==1?Token{1,8}:published,kind==2?2:0,*entry));
        CHECK(zero(entry.get(),sizeof(*entry)));
    }
    std::memset(&channel,0xa5,sizeof(channel));CHECK(!observer.copyChannel(published,2,channel)&&zero(&channel,sizeof(channel)));
    CHECK(!observer.finish(7,17,true,103));
    // Mid-pass cancellation retains the last complete pass and its copy token.
    CHECK(observer.begin(7,17,true,channels,2));
    CHECK(observer.channelAccepted(7,17,true,channels[0],{7,42}));observer.cancel();
    CHECK(!observer.open()&&observer.copySummary(summary)&&same(summary.token,published));
    CHECK(observer.copyEntry(published,0,*entry));
    // Incomplete passes cannot replace the previous complete snapshot.
    CHECK(observer.begin(7,17,true,channels,2));
    CHECK(observer.channelAccepted(7,17,true,channels[0],{7,43}));
    CHECK(observer.channelFinished({7,43},false)&&!observer.finish(7,17,true,200));
    CHECK(observer.copySummary(summary)&&same(summary.token,published));
    // Repeating an already completed channel blocks the whole current pass.
    CHECK(observer.begin(7,17,true,channels,2));
    CHECK(observer.channelAccepted(7,17,true,channels[0],{7,44}));CHECK(observer.channelFinished({7,44},false));
    CHECK(!observer.channelAccepted(7,17,true,channels[0],{7,45}));
    CHECK(!observer.channelAccepted(7,17,true,channels[1],{7,46})&&!observer.finish(7,17,true,201));
    CHECK(observer.copySummary(summary)&&same(summary.token,published));
    // Mode, active/passive mode, and physical epoch changes invalidate a pass.
    for(unsigned kind=0;kind<3;++kind){
        CHECK(observer.begin(7,17,true,channels,2));
        CHECK(!observer.channelAccepted(kind==2?8:7,kind==0?18:17,kind!=1,channels[0],{7,50+kind}));
        CHECK(!observer.finish(7,17,true,210+kind));
        CHECK(observer.copySummary(summary)&&same(summary.token,published));
    }
    CHECK(observer.begin(7,17,true,channels,2));CHECK(observer.channelAccepted(7,17,true,channels[0],{7,60}));
    CHECK(!observer.channelFinished({7,60},true)&&!observer.open());
    CHECK(observer.copySummary(summary)&&same(summary.token,published));
    const Token newer=publish(observer,8,70,300);CHECK(newer.epoch==8&&newer.generation==1);
    std::memset(entry.get(),0xa5,sizeof(*entry));CHECK(!observer.copyEntry(published,0,*entry)&&zero(entry.get(),sizeof(*entry)));
    CHECK(!observer.begin(7,17,true,channels,2));observer.cancel();
    CHECK(observer.copySummary(summary)&&same(summary.token,newer));
}
int main(){parsing();elements();lifecycle();std::printf("native scan observation checks: %u\n",checks);}
