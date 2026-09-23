// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeScanCache.hpp"
namespace rtl8852be { namespace network { namespace nativescan {

// A validated raw-air observation, not evidence that net80211 accepted a node.
// frameBytes excludes the trailing hardware FCS. Input borrows this one frame
// only until Store::add() has copied it; no remembered SSID/IE merge is allowed.
inline bool advertisement(const uint8_t *frame,size_t frameBytes,Channel channel,
                          Signal signal,uint64_t nowUs,Input &out){
    out={};
    if(!frame||frameBytes<36||!valid(channel)||!valid(signal))return false;
    if((frame[0]&0x0f)!=0||(frame[0]!=0x80&&frame[0]!=0x50))return false;
    // No distribution-system addressing, encryption, fragments or HT-control
    // header ambiguity in this deliberately narrow beacon/probe observer.
    if((frame[1]&(0x03|0x04|0x40|0x80))||(frame[22]&0x0f))return false;
    uint8_t addressBits=0,destinationBits=0;bool broadcast=true;
    for(unsigned i=0;i<6;++i){
        addressBits|=frame[16+i];
        destinationBits|=frame[4+i];broadcast=broadcast&&frame[4+i]==0xff;
        if(frame[10+i]!=frame[16+i])return false;
    }
    if(!addressBits||(frame[16]&1))return false;
    if(!destinationBits||((frame[4]&1)&&!broadcast)||(frame[0]==0x80&&!broadcast))return false;
    const uint8_t *ssid=nullptr;size_t ssidLength=0;
    bool seenSsid=false,seenRates=false,seenDs=false,seenHt=false,seenRsn=false,seenWpa=false;
    for(size_t offset=36;offset<frameBytes;){
        if(frameBytes-offset<2)return false;
        const uint8_t tag=frame[offset],length=frame[offset+1];offset+=2;
        if(length>frameBytes-offset)return false;
        if(tag==0){
            if(seenSsid||length>32)return false;
            seenSsid=true;ssid=frame+offset;ssidLength=length;
        }else if(tag==1){
            // A deliberately narrow ordinary Supported Rates IE; extended
            // rates remain in the untouched IE byte sequence.
            if(seenRates||!length||length>8)return false;seenRates=true;
        }else if(tag==3){
            if(seenDs||length!=1||frame[offset]!=channel.number)return false;seenDs=true;
        }else if(tag==61){
            if(seenHt||length!=22||frame[offset]!=channel.number)return false;seenHt=true;
        }else if(tag==48){
            if(seenRsn)return false;seenRsn=true;
        }else if(tag==221&&length>=4&&frame[offset]==0&&frame[offset+1]==0x50&&
                 frame[offset+2]==0xf2&&frame[offset+3]==1){
            if(seenWpa)return false;seenWpa=true;
        }else if(tag==255&&!length){
            return false;
        }
        offset+=length;
    }
    if(!seenSsid||!seenRates)return false;
    out.bssid=frame+16;out.ssid=ssid;out.ssidLength=ssidLength;
    out.ies=frame+36;out.ieLength=out.originalIeLength=frameBytes-36;
    out.channel=channel;out.signal=signal;out.observedAtUs=nowUs;
    out.beaconInterval=uint16_t(frame[32])|(uint16_t(frame[33])<<8);
    out.capability=uint16_t(frame[34])|(uint16_t(frame[35])<<8);
    return true;
}

struct DwellToken {uint64_t epoch{},operation{};};
inline bool same(DwellToken a,DwellToken b){return a.epoch==b.epoch&&a.operation==b.operation;}

// Allocate once on the heap (contains Store's two ~150 KiB banks). Observes
// existing per-channel work; never calls a radio/protocol API or starts a scan.
// All calls, including copies, require the owner's workloop gate.
class Observer {
    Store store_;
    Channel plan_[maxChannels]{};size_t planCount_{};
    uint64_t epoch_{},generation_{},completedMask_{},lastOperation_{};
    Token pass_{};DwellToken dwell_{};Channel dwellChannel_{};
    int mode_{};bool activeMode_{},open_{},blocked_{},pending_{};
    size_t index(Channel c)const{
        for(size_t i=0;i<planCount_;++i)if(same(c,plan_[i]))return i;
        return maxChannels;
    }
    static void clear(void *p,size_t n){auto *bytes=static_cast<uint8_t*>(p);while(n--)*bytes++=0;}
    void block(){if(store_.status().active)store_.cancel(pass_);blocked_=true;pending_=false;dwell_={};}
public:
    Observer()=default;Observer(const Observer&)=delete;Observer& operator=(const Observer&)=delete;
    bool open()const{return open_;}
    bool begin(uint64_t epoch,int mode,bool active,const Channel *plan,size_t count){
        if(open_)return false;
        open_=true;blocked_=false;pending_=false;dwell_={};completedMask_=0;planCount_=0;
        if(!epoch||epoch<epoch_){block();return false;}
        if(epoch!=epoch_){epoch_=epoch;generation_=0;lastOperation_=0;}
        if(generation_==UINT64_MAX){block();return false;}
        pass_={++generation_,epoch};mode_=mode;activeMode_=active;
        if(!store_.begin(pass_.generation,pass_.epoch)||!store_.plan(pass_,plan,count)){
            block();return false;
        }
        for(size_t i=0;i<count;++i)plan_[i]=plan[i];planCount_=count;return true;
    }
    bool channelAccepted(uint64_t epoch,int mode,bool active,Channel channel,DwellToken operation){
        if(!open_||blocked_)return false;
        const size_t i=index(channel);
        if(pending_||epoch!=pass_.epoch||mode!=mode_||active!=activeMode_||
           operation.epoch!=pass_.epoch||operation.operation<=lastOperation_||i==maxChannels||
           (completedMask_&(uint64_t(1)<<i))){block();return false;}
        lastOperation_=operation.operation;dwell_=operation;dwellChannel_=channel;pending_=true;return true;
    }
    bool observing(DwellToken operation,Channel channel)const{
        return open_&&!blocked_&&pending_&&same(dwell_,operation)&&same(dwellChannel_,channel);
    }
    bool observe(DwellToken operation,const uint8_t *frame,size_t bytes,Channel channel,
                 Signal signal,uint64_t nowUs){
        if(!observing(operation,channel))return false;
        Input input;if(!advertisement(frame,bytes,channel,signal,nowUs,input))return false;
        const auto result=store_.add(pass_,input);
        return result==AddResult::added||result==AddResult::replaced||result==AddResult::unchanged;
    }
    bool channelFinished(DwellToken operation,bool cancelled){
        // A stale completion must neither finish nor cancel a newer dwell.
        if(!open_||blocked_||!pending_||!same(dwell_,operation))return false;
        if(cancelled){cancel();return false;}
        const size_t i=index(dwellChannel_);
        if(i==maxChannels||!store_.channelFinished(pass_,dwellChannel_)){block();return false;}
        completedMask_|=uint64_t(1)<<i;pending_=false;dwell_={};return true;
    }
    bool finish(uint64_t epoch,int mode,bool active,uint64_t nowUs){
        if(!open_)return false;
        bool published=false;
        if(!blocked_&&!pending_&&epoch==pass_.epoch&&mode==mode_&&active==activeMode_)
            published=store_.finish(pass_,Completion::fullScan,nowUs)==FinishResult::published;
        cancel();return published;
    }
    void cancel(){
        if(store_.status().active)store_.cancel(pass_);
        open_=blocked_=pending_=false;dwell_={};planCount_=0;
    }
    void rejectedChannel(){if(open_)block();}
    bool copySummary(Summary &out)const{
        clear(&out,sizeof(out));const auto *summary=store_.completed();if(!summary)return false;
        out=*summary;return true;
    }
    bool copyEntry(Token token,size_t entryIndex,Entry &out)const{
        clear(&out,sizeof(out));const auto *summary=store_.completed();
        if(!summary||!same(token,summary->token))return false;
        const auto *entry=store_.completedEntry(entryIndex);if(!entry)return false;
        // Copy directly into caller-owned storage; no large stack temporary.
        auto *dest=reinterpret_cast<uint8_t*>(&out);const auto *source=reinterpret_cast<const uint8_t*>(entry);
        for(size_t i=0;i<sizeof(out);++i)dest[i]=source[i];return true;
    }
    bool copyChannel(Token token,size_t channelIndex,Channel &out)const{
        clear(&out,sizeof(out));const auto *summary=store_.completed();
        if(!summary||!same(token,summary->token))return false;
        const auto *channel=store_.completedChannel(channelIndex);if(!channel)return false;
        out=*channel;return true;
    }
    // Controller-internal loan for the WCL draft bridge. The caller must hold
    // this Observer's hardware gate for the entire bridge operation; no Store
    // pointer/reference may escape into a frontend or survive a new pass.
    const Store &completedStoreUnderGate()const{return store_;}
};
} } }
