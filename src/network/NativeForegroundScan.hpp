// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeScanCache.hpp"
namespace rtl8852be { namespace network { namespace foregroundscan {

// Kernel-internal control contract; no Apple/private userspace wire layout.
// A request uses only the caller's already-authorized channel snapshot. This
// owner never calls a radio, net80211 state transition, or callback recursively.
constexpr uint16_t dwellMs=120;
constexpr uint64_t timeoutUs=120000000;
constexpr int observerMode=-1; // Distinct from all real net80211 PHY modes.
struct Token {uint64_t epoch{},request{};};
struct Dwell {uint64_t epoch{},operation{};};
inline bool same(Token a,Token b){return a.epoch==b.epoch&&a.request==b.request;}
inline bool same(Dwell a,Dwell b){return a.epoch==b.epoch&&a.operation==b.operation;}
enum class Phase : uint8_t {none,queued,scanning,draining,complete,cancelled,timedOut,failed};
enum class Reason : uint8_t {none,caller,selection,disabled,shutdown,timeout,backend,observation,protocol,clock,probe};
enum class Admission : uint8_t {accepted,busy,notReady,unsupported,invalid};
struct Readiness {
    bool ready{},stationIdle{},protocolIdle{},connected{},pendingJoin{},legacyCredentials{},otherWork{};
    bool activeAllowed{}; // Owner checked actual channel flags and current 2.4 GHz policy.
};
// Kernel-internal request after any native message has been decoded and copied.
// Channel numbers are an explicit ordered subset of the currently allowed
// 2.4 GHz plan. The hardware owner rechecks all of them under its gate.
struct RequestedPlan {
    bool active{};uint16_t dwellMs{};uint8_t channelCount{};
    uint8_t channels[11]{};
};
struct Status {
    Token token{};nativescan::Token snapshot{};
    uint64_t startedAtUs{},deadlineUs{},finishedAtUs{};
    size_t plannedChannels{},completedChannels{};
    size_t probesSubmitted{};uint16_t dwellMs{};bool activeScan{};
    Phase phase{};Reason reason{};
    // true only after the matching restored-home scan completion (or an
    // independently proven hardware shutdown); cancellation acceptance is false.
    bool drained{};
};

class Controller {
    nativescan::Channel channels_[nativescan::maxChannels]{};
    Status status_{};Dwell dwell_{};
    uint64_t serial_{},epoch_{},lastOperation_{};
    bool probeRequested_{},probeSubmitted_{},probeCompleted_{};
    static void clear(void *p,size_t n){auto *b=static_cast<uint8_t*>(p);while(n--)*b++=0;}
    Phase cancelledPhase()const{return status_.reason==Reason::timeout?Phase::timedOut:Phase::cancelled;}
    void endCancellation(uint64_t now){
        status_.phase=cancelledPhase();status_.drained=true;status_.finishedAtUs=now;dwell_={};
    }
public:
    bool active()const{return status_.phase==Phase::queued||status_.phase==Phase::scanning||status_.phase==Phase::draining;}
    bool draining()const{return status_.phase==Phase::draining;}
    bool owns(Dwell token)const{return active()&&dwell_.operation&&same(dwell_,token);}
    bool observing(Dwell token)const{return status_.phase==Phase::scanning&&owns(token);}
    Dwell operation()const{return dwell_;}
    Token token()const{return status_.token;}
    const Status &status()const{return status_;}
    Admission begin(Readiness r,bool activeScan,uint64_t epoch,uint64_t now,
                    const nativescan::Channel *plan,size_t count,uint16_t requestedDwellMs,Status &out){
        clear(&out,sizeof(out));
        if(activeScan&&!r.activeAllowed)return Admission::unsupported;
        if(!r.ready)return Admission::notReady;
        if(active()||dwell_.operation||r.connected||r.pendingJoin||r.legacyCredentials||r.otherWork||
           !r.stationIdle||!r.protocolIdle)return Admission::busy;
        if(!epoch||epoch<epoch_||serial_==UINT64_MAX||!plan||!count||
           requestedDwellMs<10||requestedDwellMs>1000||
           count>nativescan::maxChannels||UINT64_MAX-now<timeoutUs)return Admission::invalid;
        for(size_t i=0;i<count;++i){
            if(!nativescan::valid(plan[i]))return Admission::invalid;
            for(size_t j=0;j<i;++j)if(nativescan::same(plan[i],plan[j]))return Admission::invalid;
        }
        if(epoch!=epoch_)lastOperation_=0;
        epoch_=epoch;status_={};status_.token={epoch,++serial_};
        status_.startedAtUs=now;status_.deadlineUs=now+timeoutUs;
        status_.plannedChannels=count;status_.dwellMs=requestedDwellMs;
        status_.phase=Phase::queued;status_.drained=true;dwell_={};
        status_.activeScan=activeScan;probeRequested_=probeSubmitted_=probeCompleted_=false;
        for(size_t i=0;i<count;++i)channels_[i]=plan[i];
        out=status_;return Admission::accepted;
    }
    Admission begin(Readiness r,bool activeScan,uint64_t epoch,uint64_t now,
                    const nativescan::Channel *plan,size_t count,Status &out){
        return begin(r,activeScan,epoch,now,plan,count,dwellMs,out);
    }
    bool next(nativescan::Channel &out)const{
        out={};if(status_.phase!=Phase::queued||dwell_.operation||
                  status_.completedChannels>=status_.plannedChannels)return false;
        out=channels_[status_.completedChannels];return true;
    }
    bool accepted(Dwell operation){
        nativescan::Channel unused;
        if(!next(unused)||operation.epoch!=status_.token.epoch||
           !operation.operation||operation.operation<=lastOperation_)return false;
        lastOperation_=operation.operation;dwell_=operation;
        probeRequested_=probeSubmitted_=probeCompleted_=false;
        status_.phase=Phase::scanning;status_.drained=false;return true;
    }
    bool requestProbe(Dwell operation,nativescan::Channel channel){
        if(!observing(operation)||!status_.activeScan||probeRequested_||
           !nativescan::same(channel,channels_[status_.completedChannels]))return false;
        probeRequested_=true;return true;
    }
    bool pendingProbe(Dwell operation,nativescan::Channel &channel)const{
        channel={};if(!observing(operation)||!status_.activeScan||!probeRequested_||probeSubmitted_)return false;
        channel=channels_[status_.completedChannels];return true;
    }
    bool submittedProbe(Dwell operation){
        nativescan::Channel channel;if(!pendingProbe(operation,channel))return false;
        probeSubmitted_=true;++status_.probesSubmitted;return true;
    }
    bool completedProbe(Dwell operation){
        if(!observing(operation)||!status_.activeScan||!probeSubmitted_||probeCompleted_)return false;
        probeCompleted_=true;return true;
    }
    bool cancel(Token token,Reason reason,uint64_t now){
        if(!active()||!same(token,status_.token)||reason==Reason::none)return false;
        if(status_.phase==Phase::draining)return true; // Preserve the first cause.
        status_.reason=reason;status_.snapshot={};
        if(dwell_.operation)status_.phase=Phase::draining;
        else endCancellation(now);
        return true;
    }
    bool expired(uint64_t now){
        if(!active()||draining()||now<status_.deadlineUs)return false;
        return cancel(status_.token,Reason::timeout,now);
    }
    bool clockValid(uint64_t now)const{return !active()||now>=status_.startedAtUs;}
    bool channelFinished(Dwell operation,bool cancelled,uint64_t now){
        if(!owns(operation))return false; // A stale completion cannot finish a new dwell.
        if(!clockValid(now)){fail(Reason::clock,now);return true;}
        expired(now);
        dwell_={};status_.drained=true;
        if(draining()){endCancellation(now);return true;}
        if(cancelled){status_.reason=Reason::backend;endCancellation(now);return true;}
        if(status_.activeScan&&!probeCompleted_){fail(Reason::probe,now);return true;}
        ++status_.completedChannels;status_.phase=Phase::queued;return true;
    }
    bool readyToFinish()const{
        return status_.phase==Phase::queued&&status_.plannedChannels&&
               status_.completedChannels==status_.plannedChannels&&!dwell_.operation;
    }
    bool complete(nativescan::Token snapshot,uint64_t now){
        if(!readyToFinish()||!snapshot.generation||snapshot.epoch!=status_.token.epoch||
           !clockValid(now)||now>=status_.deadlineUs)return false;
        status_.snapshot=snapshot;status_.phase=Phase::complete;status_.drained=true;
        status_.finishedAtUs=now;return true;
    }
    void fail(Reason reason,uint64_t now){
        if(!active())return;
        status_.reason=reason;status_.phase=Phase::failed;status_.snapshot={};
        status_.drained=!dwell_.operation;status_.finishedAtUs=now;
        // Keep the outstanding token until real hardware stop is proven.
    }
    void hardwareStopped(uint64_t now){
        if(!status_.token.request)return;
        if(active()){if(status_.reason==Reason::none)status_.reason=Reason::shutdown;endCancellation(now);}
        else if(status_.phase==Phase::failed){dwell_={};status_.drained=true;}
    }
    bool copy(Token token,Status &out)const{
        clear(&out,sizeof(out));if(!token.request||!same(token,status_.token))return false;
        out=status_;return true;
    }
};
} } }
