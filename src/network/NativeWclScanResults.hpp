// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeForegroundScan.hpp"
#include "NativeWclBeacon.hpp"
#include "NativeWclScan.hpp"

namespace rtl8852be { namespace network { namespace nativewclresults {

// Offline sequencing only. A Frame is a draft, never permission to post to
// IO80211 or evidence that WCL, PostOffice, or the native menu is available.
constexpr uint32_t resultEvent=nativewclbeacon::scanResultEvent,doneEvent=237;
constexpr size_t doneBytes=4;
enum class Phase : uint8_t {idle,ready,reserved,completed,aborted};
enum class Status : uint8_t {
    ready,busy,invalidRequest,unsupportedProfile,stale,invalidSnapshot,
    invalidEntry,invalidBuffer,bufferTooSmall,frameReady,finished,aborted
};
struct Frame {
    foregroundscan::Token request{};
    nativescan::Token snapshot{};
    uint64_t delivery{};
    uint32_t event{};
    size_t bytes{},ordinal{};
    bool emissionReady{false}; // Always false. A live event sender does not exist.
};

// One heap-backed, gate-owned instance per native interface. The Store and
// foreground controller are borrowed under the same gate on every operation.
// No Entry, output payload, interface, key, or caller pointer is retained.
class Bridge {
    foregroundscan::Token request_{};
    foregroundscan::Token lastArmed_{};
    nativescan::Token snapshot_{};
    uint64_t completedAtUs_{},delivery_{},pendingDelivery_{};
    uint8_t indices_[nativescan::maxNetworks]{};
    size_t count_{},cursor_{};
    uint32_t pendingEvent_{};size_t pendingBytes_{};
    Phase phase_{Phase::idle};
    static void clear(void *p,size_t n){auto *b=static_cast<uint8_t*>(p);for(size_t i=0;i<n;++i)b[i]=0;}
    static bool validRequest(const nativewclscan::Request &request,
                             const foregroundscan::Status &scan,const nativescan::Store &store){
        if(!scan.token.epoch||!scan.token.request||scan.phase!=foregroundscan::Phase::complete||
           !scan.drained||scan.reason!=foregroundscan::Reason::none||
           !scan.snapshot.generation||scan.snapshot.epoch!=scan.token.epoch||
           request.channelCount<1||request.channelCount>nativewclscan::maxChannels||
           request.channelCount!=scan.plannedChannels||
           scan.completedChannels!=scan.plannedChannels||
           (request.bssType!=nativewclscan::BssType::infrastructure&&
            request.bssType!=nativewclscan::BssType::any))return false;
        if(request.mode!=nativewclscan::Mode::active&&request.mode!=nativewclscan::Mode::passive)
            return false;
        const bool active=request.mode==nativewclscan::Mode::active;
        if(active!=scan.activeScan||scan.dwellMs!=
           (active?request.activeDwellMs:request.passiveDwellMs)||
           request.homeRestMs||request.homeAwayMs)return false;
        for(size_t i=0;i<request.channelCount;++i){
            const auto *channel=store.completedChannel(i);
            if(!channel||channel->band!=nativescan::Band::ghz2||
               channel->number!=request.channels[i])return false;
        }
        return true;
    }
    bool current(const foregroundscan::Status &scan,const nativescan::Store &store)const{
        const auto *summary=store.completed();
        return phase_!=Phase::idle&&summary&&
            foregroundscan::same(request_,scan.token)&&
            scan.phase==foregroundscan::Phase::complete&&scan.drained&&
            nativescan::same(snapshot_,scan.snapshot)&&
            nativescan::same(snapshot_,summary->token)&&
           summary->completedAtUs==completedAtUs_&&
            summary->count<=nativescan::maxNetworks;
    }
    static bool eligible(const nativescan::Entry &entry,nativewclscan::BssType type){
        // 802.11 capability bit 0 is ESS and bit 1 is IBSS. The narrow
        // infrastructure request must not turn an ad-hoc BSS into an AP.
        return type==nativewclscan::BssType::any||
            ((entry.capability&3)==1);
    }
    static bool safeBuffer(const nativescan::Store &store,const void *buffer,size_t capacity){
        return buffer&&
            !nativewclbeacon::detail::overlaps(buffer,
                nativewclbeacon::detail::writableBytes(capacity),&store,sizeof(store));
    }
    bool disjoint(const void *buffer,size_t capacity,const void *other,size_t size)const{
        const size_t writable=nativewclbeacon::detail::writableBytes(capacity);
        return !nativewclbeacon::detail::overlaps(buffer,writable,this,sizeof(*this))&&
            !nativewclbeacon::detail::overlaps(buffer,writable,other,size);
    }
    static void wipe(void *buffer,size_t capacity){
        if(buffer)clear(buffer,nativewclbeacon::detail::writableBytes(capacity));
    }
public:
    Phase phase()const{return phase_;}
    size_t selectedCount()const{return count_;}
    size_t committedCount()const{return cursor_;}
    bool retire(){
        if(phase_!=Phase::completed&&phase_!=Phase::aborted)return false;
        request_={};snapshot_={};completedAtUs_=pendingDelivery_=0;
        pendingEvent_=0;pendingBytes_=0;count_=cursor_=0;clear(indices_,sizeof(indices_));
        phase_=Phase::idle;return true;
    }
    void abort(){
        if(phase_==Phase::ready||phase_==Phase::reserved){
            phase_=Phase::aborted;pendingDelivery_=0;pendingEvent_=0;pendingBytes_=0;
        }
    }
    // `profileVerified` is an assertion from the future owner, not an OS/KC
    // probe. The caller must independently pin the runtime KC before passing
    // true. A full-size disjoint scratch buffer permits preflighting all
    // selected entries before any draft can be reserved.
    Status begin(nativewclbeacon::TargetProfile profile,bool profileVerified,
                 const nativewclscan::Request &request,
                 const foregroundscan::Status &scan,const nativescan::Store &store,
                 void *scratch,size_t scratchCapacity){
        if(phase_!=Phase::idle)return Status::busy;
        if(profile!=nativewclbeacon::TargetProfile::darwin24_4_0_d8b50fc2||!profileVerified)
            return Status::unsupportedProfile;
        if(!safeBuffer(store,scratch,scratchCapacity)||
           !disjoint(scratch,scratchCapacity,&request,sizeof(request))||
           nativewclbeacon::detail::overlaps(scratch,
               nativewclbeacon::detail::writableBytes(scratchCapacity),&scan,sizeof(scan)))
            return Status::invalidBuffer;
        if(scratchCapacity<nativewclbeacon::maxPayloadBytes){
            wipe(scratch,scratchCapacity);return Status::bufferTooSmall;
        }
        const auto *summary=store.completed();
        if(!summary||!nativescan::same(summary->token,scan.snapshot)||
           summary->channelCount!=scan.plannedChannels||
           summary->count>nativescan::maxNetworks||
           summary->completedAtUs<scan.startedAtUs||
           summary->completedAtUs!=scan.finishedAtUs){
            wipe(scratch,scratchCapacity);return Status::invalidSnapshot;
        }
        if(!validRequest(request,scan,store)){
            wipe(scratch,scratchCapacity);return Status::invalidRequest;
        }
        if(lastArmed_.epoch>scan.token.epoch||
           (lastArmed_.epoch==scan.token.epoch&&lastArmed_.request>=scan.token.request)){
            wipe(scratch,scratchCapacity);return Status::stale;
        }
        size_t selected=0;
        for(size_t i=0;i<summary->count;++i){
            const auto *entry=store.completedEntry(i);
            if(!entry){wipe(scratch,scratchCapacity);return Status::invalidSnapshot;}
            if(!eligible(*entry,request.bssType))continue;
            const auto encoded=nativewclbeacon::encode(profile,*entry,scratch,scratchCapacity);
            if(encoded.status!=nativewclbeacon::Status::knownPartial||encoded.emissionReady){
                wipe(scratch,scratchCapacity);return Status::invalidEntry;
            }
            indices_[selected++]=uint8_t(i);
        }
        wipe(scratch,scratchCapacity);
        request_=lastArmed_=scan.token;snapshot_=scan.snapshot;
        completedAtUs_=summary->completedAtUs;count_=selected;cursor_=0;
        phase_=Phase::ready;return Status::ready;
    }
    // The output belongs to the caller until a future, independently verified
    // PostOffice adapter has synchronously copied it. This method itself never
    // dispatches an event. One reservation is allowed at a time.
    Status reserve(const foregroundscan::Status &scan,const nativescan::Store &store,
                   void *buffer,size_t capacity,Frame &out){
        // Check the Frame address before clearing it: an invalid caller might
        // alias out onto a borrowed snapshot or this state machine itself.
        // Such aliases are rejected without modifying any of their bytes.
        if(nativewclbeacon::detail::overlaps(&out,sizeof(out),&scan,sizeof(scan))||
           nativewclbeacon::detail::overlaps(&out,sizeof(out),&store,sizeof(store))||
           nativewclbeacon::detail::overlaps(&out,sizeof(out),this,sizeof(*this))||
           (buffer&&nativewclbeacon::detail::overlaps(
                &out,sizeof(out),buffer,capacity?capacity:1)))
            return Status::invalidBuffer;
        clear(&out,sizeof(out));
        if(!safeBuffer(store,buffer,capacity)||
           !disjoint(buffer,capacity,&out,sizeof(out))||
           nativewclbeacon::detail::overlaps(buffer,
               nativewclbeacon::detail::writableBytes(capacity),&scan,sizeof(scan)))
            return Status::invalidBuffer;
        if(phase_==Phase::reserved)return Status::busy;
        if(phase_==Phase::completed)return Status::finished;
        if(phase_==Phase::aborted)return Status::aborted;
        if(phase_!=Phase::ready)return Status::invalidRequest;
        if(!current(scan,store)){abort();wipe(buffer,capacity);return Status::stale;}
        if(delivery_==UINT64_MAX){abort();wipe(buffer,capacity);return Status::invalidRequest;}
        if(cursor_<count_){
            const auto *entry=store.completedEntry(indices_[cursor_]);
            if(!entry){abort();wipe(buffer,capacity);return Status::invalidSnapshot;}
            const auto encoded=nativewclbeacon::encode(
                nativewclbeacon::TargetProfile::darwin24_4_0_d8b50fc2,*entry,buffer,capacity);
            if(encoded.status!=nativewclbeacon::Status::knownPartial||encoded.emissionReady){
                if(encoded.status==nativewclbeacon::Status::bufferTooSmall)return Status::bufferTooSmall;
                abort();return Status::invalidEntry;
            }
            out.bytes=encoded.payloadBytes;out.event=resultEvent;
        }else{
            if(capacity<doneBytes){wipe(buffer,capacity);return Status::bufferTooSmall;}
            // Pinned KC producer's observed successful status is u32 zero.
            // Unknown failure mappings are excluded from this bridge.
            wipe(buffer,capacity);out.bytes=doneBytes;out.event=doneEvent;
        }
        out.request=request_;out.snapshot=snapshot_;
        out.delivery=++delivery_;out.ordinal=cursor_;
        pendingDelivery_=out.delivery;pendingEvent_=out.event;pendingBytes_=out.bytes;
        phase_=Phase::reserved;return Status::frameReady;
    }
    // `accepted` means only that a future sender accepted a synchronous copy,
    // never proof that WCL/userland consumed it. Rejecting a post aborts this
    // request and forbids a misleading successful SCAN_DONE.
    bool commit(const Frame &frame,bool accepted,
                const foregroundscan::Status &scan,const nativescan::Store &store){
        if(phase_!=Phase::reserved||!pendingDelivery_||
           frame.delivery!=pendingDelivery_||frame.event!=pendingEvent_||
           frame.bytes!=pendingBytes_||frame.emissionReady||
           frame.ordinal!=cursor_||
           !foregroundscan::same(frame.request,request_)||
           !nativescan::same(frame.snapshot,snapshot_))return false;
        if(!accepted||!current(scan,store)){abort();return false;}
        pendingDelivery_=0;pendingEvent_=0;pendingBytes_=0;
        if(frame.event==doneEvent)phase_=Phase::completed;
        else{++cursor_;phase_=Phase::ready;}
        return true;
    }
};

static_assert(nativescan::maxNetworks<=256,"compact cache index bound");
} } }
