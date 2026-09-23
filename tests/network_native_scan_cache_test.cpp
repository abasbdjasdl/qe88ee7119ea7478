// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/NativeScanCache.hpp"
#ifdef NDEBUG
#undef NDEBUG // Test assertions include calls and must execute in optimized builds.
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
using namespace rtl8852be::network::nativescan;
static const uint8_t bssid[6]={2,1,2,3,4,5},ssid[3]={'a',0,'b'};
static const uint8_t ies[]={0,3,'a',0,'b',1,2,0x82,0x84,221,0};
static const Channel channels[]={{Band::ghz2,1},{Band::ghz2,6},{Band::ghz5,36}};
static Input observation(){
    Input in;in.bssid=bssid;in.ssid=ssid;in.ssidLength=sizeof(ssid);
    in.ies=ies;in.ieLength=in.originalIeLength=sizeof(ies);in.channel=channels[0];
    in.signal={SignalUnit::percent,54};in.observedAtUs=100;
    in.capability=0x431;in.beaconInterval=100;return in;
}
static void complete(Store &s,Token token){
    for(auto channel:channels)assert(s.channelFinished(token,channel));
    assert(s.finish(token,Completion::fullScan,200)==FinishResult::published);
}
static void lifecycle(){
    // Like its eventual controller owner, keep the large store off the stack.
    auto owner=std::make_unique<Store>();auto &s=*owner;const Token first{1,10};
    assert(!s.completed()&&!s.completedEntry(0)&&!s.completedChannel(0));
    assert(!s.begin(0,10)&&!s.begin(1,0)&&s.begin(first.generation,first.epoch));
    assert(!s.begin(2,10));
    assert(s.add(first,observation())==AddResult::unplanned);
    assert(!s.plan({2,10},channels,3)&&!s.plan(first,nullptr,3)&&!s.plan(first,channels,0));
    Channel bad[]={{Band::ghz2,1},{Band::ghz2,1}};
    assert(!s.plan(first,bad,2)&&!s.status().planned);
    bad[1]={Band::ghz5,35};assert(!s.plan(first,bad,2));
    assert(s.plan(first,channels,3)&&!s.plan(first,channels,3));
    assert(s.add(first,observation())==AddResult::added);
    assert(!s.channelFinished({1,9},channels[0]));
    assert(s.channelFinished(first,channels[0])&&s.channelFinished(first,channels[0]));
    assert(s.status().completedChannels==1);
    assert(s.finish(first,Completion::channel,200)==FinishResult::incomplete);
    assert(s.finish(first,Completion::fullScan,200)==FinishResult::incomplete&&!s.completed());
    complete(s,first);
    const auto *entry=s.completedEntry(0);assert(entry&&entry->ssidLength==3&&entry->ssid[1]==0);
    assert(entry->ieLength==sizeof(ies)&&!memcmp(entry->ies,ies,sizeof(ies)));
    assert(entry->signal.unit==SignalUnit::percent&&entry->signal.value==54);
    assert(entry->capability==0x431&&entry->beaconInterval==100);
    assert(!s.completedEntry(1)&&!s.completedChannel(3)&&same(*s.completedChannel(2),channels[2]));
    assert(s.completed()->count==1&&same(s.completed()->token,first));
    assert(s.finish(first,Completion::fullScan,201)==FinishResult::stale);
    assert(!s.begin(1,10)&&!s.begin(100,9));
    assert(s.begin(2,10)&&s.plan({2,10},channels,3));
    assert(s.add(first,observation())==AddResult::stale&&!s.cancel(first));
    assert(same(s.completed()->token,first)&&s.completedEntry(0)==entry);
    assert(s.cancel({2,10})&&s.status().cancelled&&!s.status().active);
    assert(s.completedEntry(0)==entry&&same(s.completed()->token,first));
    assert(s.begin(1,11)&&s.plan({1,11},channels,3));
    complete(s,{1,11}); // Empty *full* sweep replaces the old completed cache.
    assert(s.completed()->count==0&&!s.completedEntry(0)&&same(s.completed()->token,{1,11}));
}
static void validationAndLoss(){
    auto owner=std::make_unique<Store>();auto &s=*owner;Token t{1,1};
    assert(s.begin(1,1)&&s.plan(t,channels,3));
    assert(s.add(t,observation())==AddResult::added);complete(s,t);
    t={2,1};assert(s.begin(2,1)&&s.plan(t,channels,3));
    for(unsigned n=0;n<12;++n){
        Input in=observation();uint8_t address[6];memcpy(address,bssid,6);
        uint8_t malformed[40]{};
        switch(n){
        case 0:in.bssid=nullptr;break;
        case 1:memset(address,0,6);in.bssid=address;break;
        case 2:address[0]=3;in.bssid=address;break;
        case 3:in.ssidLength=33;break;
        case 4:in.ssid=nullptr;break;
        case 5:in.ies=nullptr;break;
        case 6:in.signal={SignalUnit::percent,101};break;
        case 7:in.signal={SignalUnit::dbm,-128};break;
        case 8:in.signal={SignalUnit::unknown,1};break;
        case 9:in.ies=malformed;in.ieLength=in.originalIeLength=1;break;
        case 10:malformed[0]=1;malformed[1]=3;in.ies=malformed;in.ieLength=in.originalIeLength=4;break;
        case 11:malformed[0]=0;malformed[1]=0;in.ies=malformed;in.ieLength=in.originalIeLength=2;break;
        }
        assert(s.add(t,in)==AddResult::invalid);
    }
    assert(s.status().count==0&&s.status().rejected==12);
    auto in=observation();in.originalIeLength++;
    assert(s.add(t,in)==AddResult::truncated&&s.status().truncatedInput);
    in=observation();assert(s.add(t,in)==AddResult::truncated);
    for(auto c:channels)assert(s.channelFinished(t,c));
    assert(s.finish(t,Completion::fullScan,300)==FinishResult::loss);
    assert(s.completed()->token.generation==1&&s.completed()->count==1);
    t={3,1};assert(s.begin(3,1)&&s.plan(t,channels,3));
    std::vector<uint8_t> large(maxIeBytes+1);in=observation();in.ies=large.data();
    in.ieLength=in.originalIeLength=large.size();
    assert(s.add(t,in)==AddResult::overflow&&s.status().overflow);
    for(auto c:channels)assert(s.channelFinished(t,c));
    assert(s.finish(t,Completion::fullScan,400)==FinishResult::loss);
    assert(s.completed()->token.generation==1);
    t={4,1};assert(s.begin(4,1)&&s.plan(t,channels,3));
    in=observation();in.channel={Band::ghz5,40};assert(s.add(t,in)==AddResult::unplanned);
    in=observation();in.ssid=nullptr;in.ssidLength=0;const uint8_t hidden[]={0,0};
    in.ies=hidden;in.ieLength=in.originalIeLength=sizeof(hidden);
    assert(s.add(t,in)==AddResult::added);complete(s,t);
    assert(s.completedEntry(0)->ssidLength==0&&s.completedEntry(0)->ies[1]==0);
}
static void duplicatesAndCapacity(){
    auto owner=std::make_unique<Store>();auto &s=*owner;Token t{1,1};
    assert(s.begin(1,1)&&s.plan(t,channels,3));
    auto in=observation();in.channel=channels[2];assert(s.add(t,in)==AddResult::added);
    in.channel=channels[0];assert(s.add(t,in)==AddResult::added); // same BSSID, distinct channel
    in.signal={SignalUnit::dbm,-55};assert(s.add(t,in)==AddResult::unchanged);
    in.observedAtUs=101;assert(s.add(t,in)==AddResult::replaced);
    in.observedAtUs=99;assert(s.add(t,in)==AddResult::unchanged);
    uint8_t lower[6]={2,1,2,3,4,4};in.bssid=lower;assert(s.add(t,in)==AddResult::added);
    complete(s,t);assert(s.completed()->count==3);
    assert(s.completedEntry(0)->bssid[5]==4&&s.completedEntry(1)->bssid[5]==5);
    assert(s.completedEntry(1)->signal.unit==SignalUnit::dbm&&s.completedEntry(1)->signal.value==-55);
    assert(same(s.completedEntry(2)->channel,channels[2]));
    t={2,1};assert(s.begin(2,1)&&s.plan(t,channels,3));
    uint8_t address[6]={2,1,2,3,4,0};in=observation();in.bssid=address;
    for(unsigned i=0;i<maxNetworks;++i){address[5]=uint8_t(i);assert(s.add(t,in)==AddResult::added);}
    address[5]=0;in.observedAtUs++;assert(s.add(t,in)==AddResult::replaced);
    address[5]=64;assert(s.add(t,in)==AddResult::overflow&&s.status().count==maxNetworks);
    assert(s.status().overflow&&s.status().rejected==1&&s.completed()->count==3);
    assert(s.cancel(t)&&s.status().overflow&&s.status().cancelled&&s.completed()->count==3);
}
static void exactBounds(){
    auto owner=std::make_unique<Store>();auto &s=*owner;
    // Exact-length allocations allow ASan to check every TLV admission boundary.
    auto in=observation();in.ssid=nullptr;in.ssidLength=0;
    std::vector<uint8_t> bytes(maxIeBytes,0); // 1152 zero-length SSID IEs is invalid.
    for(size_t i=0;i<bytes.size();i+=2)bytes[i]=221; // valid unknown/vendor empty IEs
    Token t{1,1};assert(s.begin(1,1)&&s.plan(t,channels,3));
    in.ies=bytes.data();in.ieLength=in.originalIeLength=bytes.size();
    assert(s.add(t,in)==AddResult::added);complete(s,t);
    assert(s.completedEntry(0)->ieLength==maxIeBytes);
    uint32_t random=9;
    for(uint64_t generation=2;generation<2002;++generation){
        random=random*1664525u+1013904223u;
        std::vector<uint8_t> sample(random%(maxIeBytes+1));
        for(auto &b:sample){random=random*1664525u+1013904223u;b=uint8_t(random>>24);}
        t={generation,1};assert(s.begin(generation,1)&&s.plan(t,channels,3));
        in.ies=sample.data();in.ieLength=in.originalIeLength=sample.size();
        const auto result=s.add(t,in);assert(result==AddResult::added||result==AddResult::invalid);
        assert(s.cancel(t));
    }
    // Cover every supported primary channel, and the plan length boundary.
    Channel full[maxChannels];size_t count=0;
    for(unsigned c=1;c<=14;++c)full[count++]={Band::ghz2,uint8_t(c)};
    for(unsigned c=36;c<=177;++c){Channel ch{Band::ghz5,uint8_t(c)};if(valid(ch))full[count++]=ch;}
    // This chip geometry has fewer than 64 channels: an oversized plan is still
    // rejected before reading the caller buffer, including a 65 count on it.
    t={3000,1};assert(s.begin(3000,1)&&!s.plan(t,full,65)&&s.plan(t,full,count));
    for(size_t i=0;i<count;++i)assert(s.channelFinished(t,full[i]));
    assert(s.finish(t,Completion::channel,1)==FinishResult::incomplete);
    assert(s.finish(t,Completion::fullScan,201)==FinishResult::published);
}
static void ownershipAndSingleChannel(){
    auto owner=std::make_unique<Store>();auto &s=*owner;const Token t{1,1};
    assert(!valid(Channel{static_cast<Band>(255),1})&&!valid(Channel{Band::ghz2,0}));
    assert(!valid(Signal{static_cast<SignalUnit>(255),0})&&!valid(Signal{SignalUnit::dbm,1}));
    assert(s.begin(1,1)&&s.plan(t,channels,1));
    {
        uint8_t mutableBssid[6],mutableSsid[3],mutableIe[sizeof(ies)];
        memcpy(mutableBssid,bssid,6);memcpy(mutableSsid,ssid,3);memcpy(mutableIe,ies,sizeof(ies));
        auto in=observation();in.bssid=mutableBssid;in.ssid=mutableSsid;in.ies=mutableIe;
        assert(s.add(t,in)==AddResult::added);
        memset(mutableBssid,0,6);memset(mutableSsid,0,3);memset(mutableIe,0,sizeof(ies));
    }
    assert(s.channelFinished(t,channels[0]));
    // Even with a one-channel requested plan, a dwell event cannot publish.
    assert(s.finish(t,Completion::channel,200)==FinishResult::incomplete&&!s.completed());
    assert(s.finish(t,Completion::fullScan,200)==FinishResult::published);
    assert(!memcmp(s.completedEntry(0)->bssid,bssid,6)&&!memcmp(s.completedEntry(0)->ssid,ssid,3));
    assert(!memcmp(s.completedEntry(0)->ies,ies,sizeof(ies)));
    // The old completed record is a valid source for a new bank, but never a
    // pointer to retain across arbitrary future store mutations.
    const Entry *previous=s.completedEntry(0);const Token next{2,1};
    assert(s.begin(2,1)&&s.plan(next,channels,1));
    auto in=observation();in.bssid=previous->bssid;in.ssid=previous->ssid;in.ies=previous->ies;
    assert(s.add(next,in)==AddResult::added);
    assert(s.channelFinished(next,channels[0]));
    assert(s.finish(next,Completion::fullScan,201)==FinishResult::published);
    assert(!memcmp(s.completedEntry(0)->ies,ies,sizeof(ies)));
}
static void ssidCoherence(){
    auto owner=std::make_unique<Store>();auto &s=*owner;const Token t{1,1};
    assert(s.begin(1,1)&&s.plan(t,channels,1));
    auto in=observation();
    // Complete but unnamed IE bytes cannot borrow a cached nonempty SSID.
    const uint8_t unnamed[]={1,2,0x82,0x84,221,0};
    in.ies=unnamed;in.ieLength=in.originalIeLength=sizeof(unnamed);
    assert(s.add(t,in)==AddResult::invalid);
    in.ies=nullptr;in.ieLength=in.originalIeLength=0;
    assert(s.add(t,in)==AddResult::invalid);
    // Equal lengths still require byte equality, including embedded NULs.
    const uint8_t changed[]={'a',1,'b'};in=observation();in.ssid=changed;
    assert(s.add(t,in)==AddResult::invalid);
    const uint8_t duplicate[]={0,3,'a',0,'b',0,3,'a',0,'b'};
    in=observation();in.ies=duplicate;in.ieLength=in.originalIeLength=sizeof(duplicate);
    assert(s.add(t,in)==AddResult::invalid);
    const uint8_t duplicateHidden[]={0,0,0,0};
    in.ssid=nullptr;in.ssidLength=0;in.ies=duplicateHidden;
    in.ieLength=in.originalIeLength=sizeof(duplicateHidden);
    assert(s.add(t,in)==AddResult::invalid);
    // An absent name remains absent; neither empty IE input nor a hidden SSID
    // synthesizes the old network name in the published record.
    in.ies=unnamed;in.ieLength=in.originalIeLength=sizeof(unnamed);
    assert(s.add(t,in)==AddResult::added);
    assert(s.channelFinished(t,channels[0]));
    assert(s.finish(t,Completion::fullScan,200)==FinishResult::published);
    assert(!s.completedEntry(0)->ssidLength&&s.completed()->rejected==5);
    assert(s.completedEntry(0)->ieLength==sizeof(unnamed));
}
static void completionTime(){
    auto owner=std::make_unique<Store>();auto &s=*owner;Token t{1,1};
    assert(s.begin(1,1)&&s.plan(t,channels,3));
    assert(s.add(t,observation())==AddResult::added);complete(s,t);
    const auto *old=s.completedEntry(0);
    t={2,1};assert(s.begin(2,1)&&s.plan(t,channels,1));
    auto in=observation();in.observedAtUs=250;
    assert(s.add(t,in)==AddResult::added&&s.channelFinished(t,channels[0]));
    assert(s.finish(t,Completion::fullScan,249)==FinishResult::invalidTime);
    assert(s.status().active&&s.completedEntry(0)==old&&s.completed()->completedAtUs==200);
    assert(s.finish(t,Completion::fullScan,250)==FinishResult::published);
    // Even an empty full sweep cannot regress the completion clock, including
    // after a new hardware epoch; both timestamps are host monotonic time.
    t={1,2};assert(s.begin(1,2)&&s.plan(t,channels,1));
    assert(s.channelFinished(t,channels[0]));
    assert(s.finish(t,Completion::fullScan,249)==FinishResult::invalidTime);
    assert(s.completed()->count==1&&s.status().active);
    assert(s.finish(t,Completion::fullScan,250)==FinishResult::published);
    assert(!s.completed()->count);
    t={2,2};assert(s.begin(2,2)&&s.plan(t,channels,1));
    in.observedAtUs=UINT64_MAX;
    assert(s.add(t,in)==AddResult::added&&s.channelFinished(t,channels[0]));
    assert(s.finish(t,Completion::fullScan,UINT64_MAX-1)==FinishResult::invalidTime);
    assert(s.finish(t,Completion::fullScan,UINT64_MAX)==FinishResult::published);
}
int main(){
    lifecycle();validationAndLoss();duplicatesAndCapacity();exactBounds();ownershipAndSingleChannel();
    ssidCoherence();completionTime();
    printf("Native scan cache: full-plan publication, generations, preserved snapshots, IE/SSID consistency, monotonic time, units, duplicates and loss passed (%zu bytes)\n",sizeof(Store));
}
