// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace rtl8852be { namespace network { namespace nativescan {

// Internal observations only: neither an Apple private ABI nor permission to
// tune/transmit on these channels. The existing radio/regulatory policy wins.
constexpr size_t maxNetworks=64,maxChannels=64,maxIeBytes=2304;
struct Token {uint64_t generation{},epoch{};};
inline bool same(Token a,Token b){return a.generation==b.generation&&a.epoch==b.epoch;}
enum class Band : uint8_t {ghz2,ghz5};
struct Channel {Band band{};uint8_t number{};};
inline bool same(Channel a,Channel b){return a.band==b.band&&a.number==b.number;}
inline bool valid(Channel c){
    if(c.band==Band::ghz2)return c.number>=1&&c.number<=14;
    if(c.band!=Band::ghz5)return false;
    return (c.number>=36&&c.number<=64&&c.number%4==0)||
        (c.number>=100&&c.number<=144&&c.number%4==0)||
        (c.number>=149&&c.number<=177&&(c.number-149)%4==0);
}
enum class SignalUnit : uint8_t {unknown,percent,dbm};
struct Signal {SignalUnit unit{};int16_t value{};};
inline bool valid(Signal s){
    switch(s.unit){
    case SignalUnit::unknown:return s.value==0;
    case SignalUnit::percent:return s.value>=0&&s.value<=100;
    case SignalUnit::dbm:return s.value>=-127&&s.value<=0;
    default:return false;
    }
}
struct Input {
    const uint8_t *bssid{},*ssid{},*ies{};
    size_t ssidLength{},ieLength{},originalIeLength{};
    Channel channel{};Signal signal{};
    uint64_t observedAtUs{}; // Same monotonic clock as finish(), not beacon TSF.
    uint16_t capability{},beaconInterval{}; // Raw little-endian values decoded by caller.
};
struct Entry {
    uint64_t observedAtUs{};
    uint16_t capability{},beaconInterval{},ieLength{};
    Channel channel{};Signal signal{};
    uint8_t bssid[6]{},ssid[32]{},ssidLength{},ies[maxIeBytes]{};
};
struct Summary {
    Token token{};
    uint64_t completedAtUs{};
    size_t count{},channelCount{};
    uint32_t rejected{};
};
struct Status {
    Token token{};
    size_t count{},plannedChannels{},completedChannels{};
    uint32_t rejected{};
    bool active{},planned{},cancelled{},overflow{},truncatedInput{};
};
enum class AddResult : uint8_t {added,replaced,unchanged,stale,unplanned,invalid,overflow,truncated};
enum class Completion : uint8_t {channel,fullScan};
enum class FinishResult : uint8_t {published,stale,incomplete,loss,invalidTime};

// About 300 KiB: construct once as a heap-backed controller member, NEVER as a
// kernel-stack local. No allocation/callbacks and no caller pointers retained.
// Caller serializes every operation on the controller gate. Returned references
// are borrowed only while holding that gate; do not retain across mutations.
// A scan here spans the complete requested channel plan. A StationController
// one-channel dwell completion is only channelFinished(), never fullScan.
class Store {
    struct Bank {
        Summary summary{};
        Channel channels[maxChannels]{};
        uint8_t order[maxNetworks]{};
        Entry entries[maxNetworks]{};
    } banks_[2]{};
    Status status_{};
    uint64_t completedMask_{};
    unsigned working_{},published_{1};bool hasPublished_{};
    static void clear(void *p,size_t n){
        auto *bytes=static_cast<uint8_t*>(p);while(n--)*bytes++=0;
    }
    static void copy(uint8_t *to,const uint8_t *from,size_t n){for(size_t i=0;i<n;++i)to[i]=from[i];}
    static bool equal(const uint8_t *a,const uint8_t *b,size_t n){
        for(size_t i=0;i<n;++i)if(a[i]!=b[i])return false;
        return true;
    }
    static bool unicast(const uint8_t *address){
        if(!address)return false;
        uint8_t any=0;for(unsigned i=0;i<6;++i)any|=address[i];
        return any&&!(address[0]&1);
    }
    static bool informationElements(const Input &in){
        bool ssidSeen=false;
        for(size_t offset=0;offset<in.ieLength;){
            if(in.ieLength-offset<2)return false;
            const uint8_t tag=in.ies[offset],length=in.ies[offset+1];
            offset+=2;if(length>in.ieLength-offset)return false;
            if(tag==0){
                // Preserve one frame, not a synthetic combination of a revealed
                // SSID and a later hidden beacon's unrelated raw IE bytes.
                if(ssidSeen||length>32||length!=in.ssidLength||
                   (length&&!equal(in.ies+offset,in.ssid,length)))return false;
                ssidSeen=true;
            }
            offset+=length;
        }
        // A caller may retain IE-only observations with no named SSID, but it
        // cannot attach a remembered/revealed name absent from these frame IEs.
        return ssidSeen||!in.ssidLength;
    }
    bool current(Token token)const{return status_.active&&same(token,status_.token);}
    size_t channelIndex(Channel channel)const{
        const auto &bank=banks_[working_];
        for(size_t i=0;i<status_.plannedChannels;++i)if(same(channel,bank.channels[i]))return i;
        return maxChannels;
    }
    void rejected(){if(status_.rejected!=UINT32_MAX)++status_.rejected;}
    static bool precedes(const Entry &a,const Entry &b){
        if(a.channel.band!=b.channel.band)return uint8_t(a.channel.band)<uint8_t(b.channel.band);
        if(a.channel.number!=b.channel.number)return a.channel.number<b.channel.number;
        for(unsigned i=0;i<6;++i)if(a.bssid[i]!=b.bssid[i])return a.bssid[i]<b.bssid[i];
        return false;
    }
public:
    Store()=default;Store(const Store&)=delete;Store& operator=(const Store&)=delete;
    const Status &status()const{return status_;}
    const Summary *completed()const{return hasPublished_?&banks_[published_].summary:nullptr;}
    const Entry *completedEntry(size_t index)const{
        if(!hasPublished_||index>=banks_[published_].summary.count)return nullptr;
        const auto &bank=banks_[published_];return &bank.entries[bank.order[index]];
    }
    const Channel *completedChannel(size_t index)const{
        return hasPublished_&&index<banks_[published_].summary.channelCount?
            &banks_[published_].channels[index]:nullptr;
    }
    bool begin(uint64_t generation,uint64_t epoch){
        if(status_.active||!generation||!epoch||epoch<status_.token.epoch||
           (epoch==status_.token.epoch&&generation<=status_.token.generation))return false;
        clear(&banks_[working_],sizeof(Bank));status_={};completedMask_=0;
        status_.token={generation,epoch};status_.active=true;return true;
    }
    bool plan(Token token,const Channel *channels,size_t count){
        if(!current(token)||status_.planned||!channels||!count||count>maxChannels)return false;
        // Validate the complete list before mutating it. No plan changes after
        // admission; geometry is not regulatory authorization to scan it.
        for(size_t i=0;i<count;++i){
            if(!valid(channels[i]))return false;
            for(size_t j=0;j<i;++j)if(same(channels[i],channels[j]))return false;
        }
        for(size_t i=0;i<count;++i)banks_[working_].channels[i]=channels[i];
        status_.plannedChannels=count;status_.planned=true;return true;
    }
    AddResult add(Token token,const Input &in){
        if(!current(token))return AddResult::stale;
        if(!status_.planned||channelIndex(in.channel)==maxChannels)return AddResult::unplanned;
        if(status_.overflow)return AddResult::overflow;
        if(status_.truncatedInput)return AddResult::truncated;
        if(in.ieLength!=in.originalIeLength){
            status_.truncatedInput=true;rejected();return AddResult::truncated;
        }
        if(in.ieLength>maxIeBytes){status_.overflow=true;rejected();return AddResult::overflow;}
        if(!unicast(in.bssid)||in.ssidLength>32||(in.ssidLength&&!in.ssid)||
           (in.ieLength&&!in.ies)||!valid(in.signal)||!informationElements(in)){
            rejected();return AddResult::invalid;
        }
        auto &bank=banks_[working_];size_t index=0;
        // Keep separate observations if one BSSID appears on different bands/
        // channels. Latest timestamp wins for a duplicate key; equal timestamps
        // retain the first record. No IE merge, signal conversion or RSSI sort.
        for(;index<status_.count;++index){
            const auto &entry=bank.entries[index];
            if(same(in.channel,entry.channel)&&equal(in.bssid,entry.bssid,6)){
                if(in.observedAtUs<=entry.observedAtUs)return AddResult::unchanged;
                break;
            }
        }
        if(index==maxNetworks){status_.overflow=true;rejected();return AddResult::overflow;}
        const bool replacement=index<status_.count;
        auto &out=bank.entries[index];clear(&out,sizeof(out));
        out.observedAtUs=in.observedAtUs;out.channel=in.channel;out.signal=in.signal;
        out.capability=in.capability;out.beaconInterval=in.beaconInterval;
        out.ssidLength=uint8_t(in.ssidLength);out.ieLength=uint16_t(in.ieLength);
        copy(out.bssid,in.bssid,6);copy(out.ssid,in.ssid,in.ssidLength);copy(out.ies,in.ies,in.ieLength);
        if(!replacement)++status_.count;
        return replacement?AddResult::replaced:AddResult::added;
    }
    bool channelFinished(Token token,Channel channel){
        if(!current(token)||!status_.planned)return false;
        const size_t index=channelIndex(channel);if(index==maxChannels)return false;
        const uint64_t bit=uint64_t(1)<<index;
        if(!(completedMask_&bit)){completedMask_|=bit;++status_.completedChannels;}
        return true;
    }
    FinishResult finish(Token token,Completion scope,uint64_t completedAtUs){
        if(!current(token))return FinishResult::stale;
        if(scope!=Completion::fullScan||!status_.planned||
           status_.completedChannels!=status_.plannedChannels)return FinishResult::incomplete;
        if(status_.overflow||status_.truncatedInput){status_.active=false;return FinishResult::loss;}
        auto &bank=banks_[working_];
        // A completed view must never contain an observation from its future,
        // or move the last complete snapshot backwards on the monotonic clock.
        // Retain both banks and the active token so a corrected completion can
        // be supplied without discarding the previous complete snapshot.
        if(hasPublished_&&completedAtUs<banks_[published_].summary.completedAtUs)return FinishResult::invalidTime;
        for(size_t i=0;i<status_.count;++i)
            if(bank.entries[i].observedAtUs>completedAtUs)return FinishResult::invalidTime;
        // Sort only 64 one-byte indices, never move/copy a whole snapshot or
        // create a large stack temporary. Order is independent of signal units.
        for(size_t i=0;i<status_.count;++i){
            size_t j=i;while(j&&precedes(bank.entries[i],bank.entries[bank.order[j-1]])){
                bank.order[j]=bank.order[j-1];--j;
            }
            bank.order[j]=uint8_t(i);
        }
        bank.summary.token=token;bank.summary.completedAtUs=completedAtUs;
        bank.summary.count=status_.count;bank.summary.channelCount=status_.plannedChannels;
        bank.summary.rejected=status_.rejected;
        published_=working_;working_^=1;hasPublished_=true;status_.active=false;
        return FinishResult::published;
    }
    bool cancel(Token token){
        if(!current(token))return false;
        status_.active=false;status_.cancelled=true;
        clear(&banks_[working_],sizeof(Bank));return true;
    }
};
static_assert(maxNetworks<=256&&maxChannels<=64,"scan cache index/mask bound");
} } }
