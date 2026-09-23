// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <memory>
#include <vector>
#include "../src/network/NativeWclScanResults.hpp"

namespace ns=rtl8852be::network::nativescan;
namespace fg=rtl8852be::network::foregroundscan;
namespace ws=rtl8852be::network::nativewclscan;
namespace wb=rtl8852be::network::nativewclbeacon;
namespace wr=rtl8852be::network::nativewclresults;
static constexpr auto profile=wb::TargetProfile::darwin24_4_0_d8b50fc2;
static constexpr ns::Channel channels[]={{ns::Band::ghz2,1},{ns::Band::ghz2,6}};
static constexpr uint8_t shortIes[]={0,1,'A',1,1,0x82};
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)

static void le32(uint8_t *p,uint32_t value){
    for(unsigned i=0;i<4;++i)p[i]=uint8_t(value>>(i*8));
}
static uint32_t le32(const uint8_t *p){
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
static bool zeros(const uint8_t *p,size_t n){for(size_t i=0;i<n;++i)if(p[i])return false;return true;}
static ws::Request decoded(ws::BssType bss=ws::BssType::infrastructure){
    auto bytes=std::make_unique<uint8_t[]>(ws::messageBytes);
    memset(bytes.get(),0,ws::messageBytes);
    le32(bytes.get()+0x10,uint32_t(bss));le32(bytes.get()+0x40,2);
    le32(bytes.get()+0x48,120);le32(bytes.get()+0x4c,120);
    le32(bytes.get()+0x54,2);
    for(unsigned i=0;i<2;++i){
        uint8_t *entry=bytes.get()+ws::channelOffset+i*ws::channelStride;
        le32(entry,1);le32(entry+4,channels[i].number);le32(entry+8,0x0a);
    }
    ws::Policy policy{};policy.profile=ws::TargetProfile::darwin24_4_0_d8b50fc2;
    policy.privateMac=ws::PrivateMacPolicy::unsupported;
    policy.permittedPassiveChannels=(1u<<0)|(1u<<5);
    ws::Request request{};
    CHECK(ws::decode(bytes.get(),ws::messageBytes,policy,request).status==ws::Status::knownSubset);
    CHECK(request.mode==ws::Mode::passive&&request.channelCount==2);
    return request;
}
static std::vector<uint8_t> longIes(size_t size){
    std::vector<uint8_t> ies(size);ies[0]=0;ies[1]=1;ies[2]='A';
    size_t off=3;
    while(off<size){
        size_t length=size-off-2;if(length>255)length=255;
        if(size-off-length-2==1)--length;
        ies[off]=221;ies[off+1]=uint8_t(length);
        off+=length+2;
    }
    return ies;
}
struct Fixture {
    std::unique_ptr<ns::Store> cache{new ns::Store};
    fg::Controller foreground{};
    wr::Bridge bridge{};
    ws::Request request{};
    std::unique_ptr<uint8_t[]> scratch{new uint8_t[wb::maxPayloadBytes]};
    std::unique_ptr<uint8_t[]> payload{new uint8_t[wb::maxPayloadBytes+8]};
    Fixture(unsigned entries=3,bool oversized=false,ws::BssType bss=ws::BssType::infrastructure)
        :request(decoded(bss)){
        fg::Readiness ready{};ready.ready=ready.stationIdle=ready.protocolIdle=true;
        fg::Status accepted{};
        CHECK(foreground.begin(ready,false,1,100,channels,2,120,accepted)==fg::Admission::accepted);
        const ns::Token token{1,1};
        CHECK(cache->begin(1,1)&&cache->plan(token,channels,2));
        for(unsigned i=0;i<entries;++i){
            uint8_t address[6]={2,1,2,3,4,uint8_t(i+1)};
            const auto ie=oversized?longIes(2049):std::vector<uint8_t>(shortIes,shortIes+sizeof(shortIes));
            const uint8_t ssid[]={'A'};
            ns::Input in{};in.bssid=address;in.ssid=ssid;in.ssidLength=1;
            in.ies=ie.data();in.ieLength=in.originalIeLength=ie.size();
            in.channel=channels[i%2];in.signal={ns::SignalUnit::dbm,-55};
            in.observedAtUs=101+i;in.capability=i==1?2:1;
            in.beaconInterval=100;
            CHECK(cache->add(token,in)==ns::AddResult::added);
        }
        CHECK(foreground.accepted({1,1})&&foreground.channelFinished({1,1},false,110));
        CHECK(foreground.accepted({1,2})&&foreground.channelFinished({1,2},false,120));
        CHECK(cache->channelFinished(token,channels[0]));
        CHECK(cache->channelFinished(token,channels[1]));
        CHECK(cache->finish(token,ns::Completion::fullScan,130)==ns::FinishResult::published);
        // The production controller passes one timestamp to observer.finish
        // and foreground.complete in the same gated advanceForeground call.
        CHECK(foreground.complete(token,130));
    }
    wr::Status arm(bool verified=true){
        return bridge.begin(profile,verified,request,foreground.status(),*cache,
                            scratch.get(),wb::maxPayloadBytes);
    }
    wr::Status take(wr::Frame &frame,size_t capacity=wb::maxPayloadBytes){
        return bridge.reserve(foreground.status(),*cache,payload.get(),capacity,frame);
    }
    bool commit(const wr::Frame &frame,bool accepted=true){
        return bridge.commit(frame,accepted,foreground.status(),*cache);
    }
};

static void resultFlow(){
    Fixture f;CHECK(f.arm()==wr::Status::ready);
    CHECK(f.bridge.selectedCount()==2&&f.bridge.committedCount()==0);
    wr::Frame frame{},duplicate{};
    CHECK(f.take(frame)==wr::Status::frameReady);
    CHECK(frame.event==201&&frame.bytes==64+sizeof(shortIes)&&frame.ordinal==0);
    CHECK(!frame.emissionReady&&frame.request.request==1&&frame.snapshot.generation==1);
    CHECK(le32(f.payload.get())==sizeof(shortIes)&&f.payload[0x27]==1);
    CHECK(f.take(duplicate)==wr::Status::busy&&!duplicate.bytes);
    auto forged=frame;forged.bytes--;
    CHECK(!f.commit(forged)&&f.commit(frame)&&!f.commit(frame));
    CHECK(f.bridge.committedCount()==1);
    CHECK(f.take(frame)==wr::Status::frameReady&&frame.event==201&&frame.ordinal==1);
    CHECK(f.payload[0x27]==1&&f.payload[0x29+5]==3);
    CHECK(f.commit(frame)&&f.bridge.committedCount()==2);
    memset(f.payload.get(),0xa5,wb::maxPayloadBytes+8);
    CHECK(f.take(frame)==wr::Status::frameReady&&frame.event==237&&frame.bytes==4);
    CHECK(frame.ordinal==2&&zeros(f.payload.get(),wb::maxPayloadBytes));
    CHECK(f.commit(frame)&&f.bridge.phase()==wr::Phase::completed);
    CHECK(f.take(duplicate)==wr::Status::finished&&!duplicate.bytes);
    CHECK(!f.commit(frame)&&f.bridge.retire()&&f.bridge.phase()==wr::Phase::idle);
    CHECK(f.arm()==wr::Status::stale); // Retirement cannot replay one request.
}
static void filteringAndEmpty(){
    Fixture any(3,false,ws::BssType::any);
    CHECK(any.arm()==wr::Status::ready&&any.bridge.selectedCount()==3);
    Fixture empty(0);
    CHECK(empty.arm()==wr::Status::ready&&empty.bridge.selectedCount()==0);
    wr::Frame done{};CHECK(empty.take(done)==wr::Status::frameReady);
    CHECK(done.event==237&&done.bytes==4&&done.ordinal==0);
    CHECK(empty.commit(done)&&!empty.commit(done));
}
static void rejections(){
    {Fixture f;CHECK(f.arm(false)==wr::Status::unsupportedProfile);
     CHECK(f.bridge.phase()==wr::Phase::idle);
     auto wrong=f.request;wrong.channels[0]=11;
     CHECK(f.bridge.begin(profile,true,wrong,f.foreground.status(),*f.cache,
                          f.scratch.get(),wb::maxPayloadBytes)==wr::Status::invalidRequest);
     CHECK(f.bridge.phase()==wr::Phase::idle);}
    {Fixture f;memset(f.scratch.get(),0xa5,wb::maxPayloadBytes);
     CHECK(f.bridge.begin(profile,true,f.request,f.foreground.status(),*f.cache,
                          f.scratch.get(),63)==wr::Status::bufferTooSmall);
     CHECK(zeros(f.scratch.get(),63)&&f.scratch[63]==0xa5);
     // An aliased scratch must not zero the cache.
     const auto before=*f.cache->completed();
     CHECK(f.bridge.begin(profile,true,f.request,f.foreground.status(),*f.cache,
                          f.cache.get(),wb::maxPayloadBytes)==wr::Status::invalidBuffer);
     CHECK(ns::same(f.cache->completed()->token,before.token));
     CHECK(f.arm()==wr::Status::ready);
     wr::Frame frame{};
     frame.event=201;frame.bytes=99;
     CHECK(f.bridge.reserve(f.foreground.status(),*f.cache,f.cache.get(),2112,frame)==wr::Status::invalidBuffer);
     CHECK(!frame.bytes&&f.bridge.phase()==wr::Phase::ready);
     memset(f.payload.get(),0xa5,wb::maxPayloadBytes+8);
     CHECK(f.take(frame,63)==wr::Status::bufferTooSmall&&zeros(f.payload.get(),63));
     CHECK(f.bridge.phase()==wr::Phase::ready);
     CHECK(f.take(frame)==wr::Status::frameReady);
     CHECK(!f.commit(frame,false)&&f.bridge.phase()==wr::Phase::aborted);
     CHECK(f.take(frame)==wr::Status::aborted&&f.bridge.retire());}
    {Fixture f(1,true);CHECK(f.arm()==wr::Status::invalidEntry);
     CHECK(f.bridge.phase()==wr::Phase::idle&&f.bridge.selectedCount()==0);}
    {Fixture f;CHECK(f.arm()==wr::Status::ready);
     wr::Frame frame{};auto stale=f.foreground.status();++stale.token.request;
     CHECK(f.bridge.reserve(stale,*f.cache,f.payload.get(),wb::maxPayloadBytes,frame)==wr::Status::stale);
     CHECK(!frame.bytes&&f.bridge.phase()==wr::Phase::aborted);}
    {Fixture f;CHECK(f.arm()==wr::Status::ready);
     wr::Frame frame{};CHECK(f.take(frame)==wr::Status::frameReady);
     auto stale=f.foreground.status();stale.phase=fg::Phase::cancelled;
     CHECK(!f.bridge.commit(frame,true,stale,*f.cache));
     CHECK(f.bridge.phase()==wr::Phase::aborted&&!f.bridge.committedCount());}
    {Fixture f;CHECK(f.arm()==wr::Status::ready);
     const ns::Token newer{2,1};
     CHECK(f.cache->begin(2,1)&&f.cache->plan(newer,channels,2));
     CHECK(f.cache->channelFinished(newer,channels[0])&&f.cache->channelFinished(newer,channels[1]));
     CHECK(f.cache->finish(newer,ns::Completion::fullScan,150)==ns::FinishResult::published);
     wr::Frame frame{};CHECK(f.take(frame)==wr::Status::stale);
     CHECK(!frame.bytes&&f.bridge.phase()==wr::Phase::aborted);}
}
static void frameAliases(){
    static_assert(sizeof(fg::Status)>=sizeof(wr::Frame),"scan alias fixture size");
    static_assert(sizeof(ns::Store)>=sizeof(wr::Frame),"store alias fixture size");
    static_assert(sizeof(wr::Bridge)>=sizeof(wr::Frame),"bridge alias fixture size");
    Fixture f;CHECK(f.arm()==wr::Status::ready);
    auto scan=f.foreground.status();
    uint8_t before[sizeof(wr::Frame)]{};
    memcpy(before,&scan,sizeof(before));
    auto &scanAlias=*reinterpret_cast<wr::Frame*>(&scan);
    CHECK(f.bridge.reserve(scan,*f.cache,f.payload.get(),wb::maxPayloadBytes,scanAlias)==
          wr::Status::invalidBuffer);
    CHECK(memcmp(before,&scan,sizeof(before))==0&&f.bridge.phase()==wr::Phase::ready);

    memcpy(before,f.cache.get(),sizeof(before));
    auto &storeAlias=*reinterpret_cast<wr::Frame*>(f.cache.get());
    CHECK(f.bridge.reserve(f.foreground.status(),*f.cache,f.payload.get(),
                           wb::maxPayloadBytes,storeAlias)==wr::Status::invalidBuffer);
    CHECK(memcmp(before,f.cache.get(),sizeof(before))==0&&f.cache->completed());
    CHECK(f.bridge.phase()==wr::Phase::ready);

    memcpy(before,&f.bridge,sizeof(before));
    auto &bridgeAlias=*reinterpret_cast<wr::Frame*>(&f.bridge);
    CHECK(f.bridge.reserve(f.foreground.status(),*f.cache,f.payload.get(),
                           wb::maxPayloadBytes,bridgeAlias)==wr::Status::invalidBuffer);
    CHECK(memcmp(before,&f.bridge,sizeof(before))==0&&f.bridge.phase()==wr::Phase::ready);

    wr::Frame frame{};frame.event=201;frame.bytes=99;
    memcpy(before,&frame,sizeof(before));
    CHECK(f.bridge.reserve(f.foreground.status(),*f.cache,&frame,sizeof(frame),frame)==
          wr::Status::invalidBuffer);
    CHECK(memcmp(before,&frame,sizeof(before))==0&&f.bridge.phase()==wr::Phase::ready);
    auto *inside=reinterpret_cast<uint8_t*>(&frame)+sizeof(frame)-1;
    CHECK(f.bridge.reserve(f.foreground.status(),*f.cache,inside,0,frame)==
          wr::Status::invalidBuffer);
    CHECK(memcmp(before,&frame,sizeof(before))==0);
    CHECK(f.take(frame)==wr::Status::frameReady&&f.commit(frame));
}
int main(){
    resultFlow();filteringAndEmpty();rejections();frameAliases();
    printf("Native WCL scan result bridge: %u checks passed\n",checks);
}
