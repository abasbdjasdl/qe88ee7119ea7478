// SPDX-License-Identifier: BSD-3-Clause
// AX register-message protocol adapted from pinned rtw89 fw.c/fw.h/mac.c.
#pragma once
#include <stdint.h>
namespace rtl8852be { namespace firmware {
constexpr uint32_t h2cData[4]={0x8140,0x8144,0x8148,0x814c};
constexpr uint32_t c2hData[4]={0x8150,0x8154,0x8158,0x815c};
constexpr uint32_t h2cControl=0x8160,c2hControl=0x8164,hostCounters=0x1f5;
constexpr uint32_t firmwareControl=0x1e0,schedulerTx=0xc348;
constexpr uint8_t schedulerCommand=5,schedulerReply=4;
enum class MailError {none,invalid,ownership,io,timeout,clock,cancelled,staleReply,malformedReply,unexpectedReply,readback};
struct Request {uint8_t function{},contentBytes{};uint32_t words[4]{};};
struct Reply {uint32_t words[4]{};uint8_t function{},contentBytes{},sequence{};bool ack{};};
struct MailResult {
    MailError error{MailError::none};uint32_t address{},value{};unsigned operations{},polls{};
    bool triggerAttempted{},replyCaptured{},replyAcknowledged{},stateUncertain{};
    Reply reply{};
};
// One instance owns this device's register mailbox and host counter byte.
// All users, including unsolicited startup PHY capability readers, must use it
// on the same serialized workloop. A fault cannot be reset without a new epoch.
template<class Io> class Mailbox {
    Io &io_;bool active_{},faulted_{},paused_{};uint16_t savedTx_{};
    uint64_t begin_{},previous_{};bool clockStarted_{};
    bool fail(MailError error,uint32_t a=0,uint32_t v=0){
        if(result.error==MailError::none){result.error=error;result.address=a;result.value=v;}
        result.stateUncertain=result.triggerAttempted||result.replyCaptured;faulted_=true;return false;
    }
    bool check(){
        if(faulted_||result.error!=MailError::none)return false;
        if(!io_.inGate())return fail(MailError::ownership);
        if(io_.cancelled())return fail(MailError::cancelled);
        const auto t=io_.nowUs();if(!clockStarted_){begin_=previous_=t;clockStarted_=true;}
        if(t<previous_)return fail(MailError::clock);previous_=t;
        if(t-begin_>1100000||result.operations>1100000)return fail(MailError::timeout);
        return true;
    }
    bool read8(uint32_t a,uint8_t &v){v=0;if(!check())return false;++result.operations;return (io_.read8(a,v)||fail(MailError::io,a))&&check();}
    bool read16(uint32_t a,uint16_t &v){v=0;if(!check())return false;++result.operations;return (io_.read16(a,v)||fail(MailError::io,a))&&check();}
    bool read32(uint32_t a,uint32_t &v){v=0;if(!check())return false;++result.operations;return (io_.read32(a,v)||fail(MailError::io,a))&&check();}
    bool write8(uint32_t a,uint8_t v){if(!check())return false;++result.operations;return (io_.write8(a,v)||fail(MailError::io,a,v))&&check();}
    bool write32(uint32_t a,uint32_t v){if(!check())return false;++result.operations;return (io_.write32(a,v)||fail(MailError::io,a,v))&&check();}
    bool delay(unsigned us){return check()&&(io_.delayUs(us)||fail(MailError::io));}
    bool waitControl(uint32_t address,uint8_t wanted,unsigned step,unsigned timeout){
        const auto start=io_.nowUs();
        for(unsigned i=0;i<=timeout/step;++i){uint8_t v=0;
            if(!read8(address,v))return false;++result.polls;
            if(v>1)return fail(MailError::io,address,v);
            if(previous_<start)return fail(MailError::clock,address);
            // MMIO itself may consume the remaining budget. A late ready bit
            // must not turn a timed-out transaction into success.
            if(previous_-start>timeout)break;
            if(v==wanted)return true;if(previous_-start==timeout)break;
            if(!delay(step))return false;
        }
        return fail(MailError::timeout,address);
    }
    bool incrementCounter(bool sending){
        uint8_t v=0;if(!read8(hostCounters,v))return false;
        const auto shift=sending?0:4;const auto mask=uint8_t(15<<shift);
        const auto n=uint8_t((((v>>shift)+1)&15)<<shift);
        return write8(hostCounters,uint8_t((v&~mask)|n));
    }
    bool start(){
        // Recursive use faults the owner instead of corrupting its transaction.
        if(active_)return fail(MailError::ownership);
        if(faulted_)return false;
        result={};clockStarted_=false;
        if(!check())return false;active_=true;
        uint32_t fw=0;
        if(!read32(firmwareControl,fw)||fw==0xffffffff||fw==0xdeadbeef||(fw&0xe0)!=0xe0){
            fail(MailError::invalid,firmwareControl,fw);active_=false;return false;}
        return true;
    }
    struct ActiveGuard {bool &active;~ActiveGuard(){active=false;}};
    bool receive(){
        if(!waitControl(c2hControl,1,1,1000000))return false;
        for(unsigned i=0;i<4;++i)if(!read32(c2hData[i],result.reply.words[i]))return false;
        result.replyCaptured=true;
        // Capture all words before releasing ownership to firmware, as upstream.
        if(!write8(c2hControl,0))return false;result.replyAcknowledged=true;
        if(!incrementCounter(false))return false;
        const auto w=result.reply.words[0],length=(w>>8)&15;
        result.reply.function=uint8_t(w&127);result.reply.sequence=uint8_t((w>>12)&15);result.reply.ack=(w&128)!=0;
        if(length<1||length>4)return fail(MailError::malformedReply,c2hData[0],w);
        result.reply.contentBytes=uint8_t(length*4-2);return check();
    }
    bool exchangeInner(const Request &q,uint8_t expected){
        uint8_t stale=0;if(!read8(c2hControl,stale))return false;
        if(stale)return rejectStale(stale);
        if(!waitControl(h2cControl,0,1000,5000))return false;
        // Recheck after waiting: do not mistake an older response for this request.
        if(!read8(c2hControl,stale))return false;if(stale)return rejectStale(stale);
        const unsigned words=(q.contentBytes+2+3)/4;
        for(unsigned i=0;i<4;++i){uint32_t w=i<words?q.words[i]:0;
            if(i==0)w=(w&0xffff0000)|q.function|(words<<8);
            // Zero payload padding as well as unused registers.
            const unsigned bytes=q.contentBytes+2;
            if(i<words&&i*4+4>bytes){const auto valid=bytes-i*4;w&=(uint32_t(1)<<(valid*8))-1;}
            if(!write32(h2cData[i],w))return false;
        }
        if(!incrementCounter(true)||!check())return false;
        result.triggerAttempted=true;
        if(!write8(h2cControl,1)||!receive())return false;
        if(result.reply.function!=expected)return fail(MailError::unexpectedReply,c2hData[0],result.reply.words[0]);
        return true;
    }
    bool rejectStale(uint8_t v){
        if(v!=1)return fail(MailError::io,c2hControl,v);
        // Nothing was written: allow the owner to receivePending(), then retry.
        result.error=MailError::staleReply;result.address=c2hControl;result.value=v;return false;
    }
    bool setScheduler(uint16_t value){
        Request q{};q.function=schedulerCommand;q.contentBytes=6;q.words[0]=uint32_t(value)<<16;q.words[1]=0xffff;
        if(!exchangeInner(q,schedulerReply))return false;
        uint16_t actual=0;if(!read16(schedulerTx,actual))return false;
        return check()&&(actual==value||fail(MailError::readback,schedulerTx,actual));
    }
public:
    MailResult result{};
    explicit Mailbox(Io &io):io_(io){}
    Mailbox(const Mailbox &)=delete;Mailbox &operator=(const Mailbox &)=delete;
    bool faulted()const{return faulted_;}bool paused()const{return paused_;}
    bool exchange(const Request &q,uint8_t expected){
        if(q.function>127||q.function==schedulerCommand||q.contentBytes>14||expected>127)return false;
        if(!start())return false;ActiveGuard guard{active_};return exchangeInner(q,expected);
    }
    // Owner explicitly retrieves pending responses, e.g. startup PHY_CAP. No
    // request is sent and no pending response is silently discarded on exchange.
    bool receivePending(){if(!start())return false;ActiveGuard guard{active_};return receive();}
    // Absolute scheduler update for the station owner. Does not consume or
    // overwrite a pauseScheduler()/resumeScheduler() owner's saved mask.
    // A nested pause lease must be released by that owner before using this API.
    bool setSchedulerMask(uint16_t value){
        if(paused_)return false;
        if(!start())return false;ActiveGuard guard{active_};return setScheduler(value);
    }
    bool pauseScheduler(){
        if(paused_||!start())return false;ActiveGuard guard{active_};uint16_t old=0;
        if(!read16(schedulerTx,old)||!setScheduler(0))return false;
        savedTx_=old;paused_=true;return true;
    }
    bool resumeScheduler(){
        if(!paused_||!start())return false;ActiveGuard guard{active_};
        uint16_t current=0;if(!read16(schedulerTx,current))return false;
        if(current!=0)return fail(MailError::readback,schedulerTx,current);
        if(!setScheduler(savedTx_))return false;paused_=false;return true;
    }
};
} }
