// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "WirelessSelection.hpp"
namespace rtl8852be { namespace network { namespace authevents {
// RX observations, NOT verified authentication, key installation or port access.
enum Kind : uint32_t { empty=0,authentication=1,association=2,eapol=3 };
struct Event {
    uint32_t version,kind;
    uint64_t generation,epoch,operation,sampledAtUs;
    uint32_t dropped,length;
    uint8_t own[6],peer[6],source[6],channel,flags,reserved[4];
    uint8_t body[2048];
};
static_assert(sizeof(Event)==2120,"authentication event ABI changed");
inline bool equal(const uint8_t *a,const uint8_t *b){for(unsigned i=0;i<6;++i)if(a[i]!=b[i])return false;return true;}
inline bool unicast(const uint8_t *a){unsigned v=0;for(unsigned i=0;i<6;++i)v|=a[i];return v&&!(a[0]&1);}
// One exclusive reader. All operations require the controller command gate.
// No allocation, callbacks or caller-owned pointers survive RX admission.
class Queue {
    void *owner_{};uint64_t generation_{},epoch_{},operation_{};
    uint8_t own_[6]{},peer_[6]{},channel_{};
    bool active_{},overflow_{};
    unsigned head_{},count_{};uint32_t dropped_{};
    Event entries_[8]{};
    void bump(){if(!++generation_)++generation_;}
    void clear(){selection::wipe(entries_,sizeof(entries_));head_=count_=0;}
    bool loss(){dropped_=count_+1;clear();overflow_=true;return false;}
    void header(Event &out)const{
        out={};out.version=1;out.generation=generation_;out.epoch=epoch_;out.operation=operation_;
        out.channel=channel_;out.dropped=dropped_;out.flags=(active_?1:0)|(overflow_?2:0);
        for(unsigned i=0;i<6;++i){out.own[i]=own_[i];out.peer[i]=peer_[i];}
    }
public:
    Queue()=default;Queue(const Queue&)=delete;Queue& operator=(const Queue&)=delete;
    ~Queue(){disable();}
    bool owned(void *who)const{return who&&owner_==who;}
    bool needsBinding()const{return owner_&&!active_&&!overflow_;}
    bool enable(void *who){
        if(!who||(owner_&&owner_!=who))return false;
        if(owner_==who)return true;
        owner_=who;invalidate();return true;
    }
    void invalidate(){
        clear();active_=overflow_=false;epoch_=operation_=0;channel_=0;dropped_=0;
        selection::wipe(own_,6);selection::wipe(peer_,6);bump();
    }
    void disable(){invalidate();owner_=nullptr;}
    bool disable(void *who){if(!owned(who))return false;disable();return true;}
    bool bind(uint64_t epoch,uint64_t operation,const uint8_t *own,const uint8_t *peer,uint8_t channel){
        if(!owner_)return false;
        invalidate();
        if(!epoch||!operation||!own||!peer||!unicast(own)||!unicast(peer)||equal(own,peer)||!channel)return false;
        epoch_=epoch;operation_=operation;channel_=channel;
        for(unsigned i=0;i<6;++i){own_[i]=own[i];peer_[i]=peer[i];}
        active_=true;return true;
    }
    bool observe(const uint8_t *frame,size_t length,uint8_t channel,uint64_t timestamp){
        // Excludes encrypted, fragmented, four-address and HT-control frames.
        if(!active_||overflow_||!frame||length<24||channel!=channel_||(frame[0]&3)||
           (frame[1]&0xc4)||(frame[22]&15)||!equal(frame+4,own_)||!equal(frame+10,peer_))return false;
        const uint8_t subtype=frame[0]&0xfc;size_t offset=24,bodyLength=length-24;Kind kind=empty;
        if(subtype==0xb0||subtype==0x10||subtype==0x30){
            if((frame[1]&3)||!equal(frame+16,peer_)||bodyLength<6)return false;
            kind=subtype==0xb0?authentication:association;
        }else if(subtype==0x08||subtype==0x88){
            if((frame[1]&3)!=2)return false; // Infrastructure downlink only
            if(subtype==0x88){if(length<26||(frame[24]&0x80))return false;offset=26;}
            const uint8_t snap[8]={0xaa,0xaa,3,0,0,0,0x88,0x8e};
            if(length-offset<12)return false;
            for(unsigned i=0;i<8;++i)if(frame[offset+i]!=snap[i])return false;
            offset+=8;
            if(frame[offset]<1||frame[offset]>3||frame[offset+1]>4)return false;
            bodyLength=4+(size_t(frame[offset+2])<<8)+frame[offset+3];
            if(bodyLength>length-offset)return false;
            kind=eapol;
        }else return false;
        if(bodyLength>sizeof(entries_[0].body))return loss();
        if(count_==8){
            // Losing an auth frame invalidates the stream: never silently
            // return a partial transcript to an authentication consumer.
            return loss();
        }
        auto &out=entries_[(head_+count_)%8];header(out);out.kind=kind;out.length=uint32_t(bodyLength);
        for(unsigned i=0;i<6;++i)out.source[i]=frame[(kind==eapol?16:10)+i];
        out.sampledAtUs=timestamp;if(frame[1]&8)out.flags|=4;
        for(size_t i=0;i<bodyLength;++i)out.body[i]=frame[offset+i];
        ++count_;return true;
    }
    bool read(void *who,Event &out){
        out={};if(!owned(who))return false;
        if(!count_){header(out);return true;}
        out=entries_[head_];selection::wipe(&entries_[head_],sizeof(Event));head_=(head_+1)%8;--count_;return true;
    }
};
} } }
