// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "FirmwareProtocol.hpp"
namespace rtl8852be { namespace network {
// ACK routing is distinct from CH12 DMA retirement. Neither a callback nor a
// timeout frees a DMA allocation; that remains the firmware queue's job.
struct CommandReceiver {
    void *owner{};
    bool (*event)(void *,const FirmwareEvent &,uint64_t epoch,uint64_t token){};
};
struct FirmwareCommandLink {
    void *owner{};
    bool (*available)(void *){};
    bool (*submit)(void *,CommandId,bool doneAck,const uint8_t *,size_t,uint8_t &){};
    void (*invalidate)(void *){};
    bool valid()const{return owner&&available&&submit&&invalidate;}
};
enum class CommandError {none,clock,timeout,transport,receiver,firmwareRejected,exhausted,invalidated};
enum class CommandEvent {unrelated,received,completed,fault};
enum class CommandState : uint8_t {unused,reserved,pending,complete};
struct CommandRecord {
    CommandState state{};CommandId id{};CommandReceiver receiver{};
    uint64_t token{},deadline{};bool receiveAck{},doneAck{},received{};
};
// One instance per VERIFIED firmware + RX queue incarnation, shared by ALL
// command producers. Construct only after reset/drain; no logical reset method.
// Transport: inGate(), nowUs(), publish(bytes,length) which COPIES to and syncs
// actual DMA before ringing its doorbell. No synchronous C2H callback in publish.
// This object owns a 16 KiB staging buffer and must live off the kernel stack.
template<class Transport> class FirmwareCommands {
    Transport &io_;const uint64_t epoch_;
    CommandRecord records_[256]{};uint8_t packet_[16383]{};
    uint32_t next_{};uint64_t lastTime_{};bool clockStarted_{},publishing_{},delivering_{};
    CommandError error_{CommandError::none};
    bool fail(CommandError e){if(error_==CommandError::none)error_=e;return false;}
    bool check(){
        if(!epoch_||error_!=CommandError::none||!io_.inGate())return false;
        const auto now=io_.nowUs();
        if(clockStarted_&&now<lastTime_)return fail(CommandError::clock);
        lastTime_=now;clockStarted_=true;
        for(unsigned i=0;i<256;++i){const auto &r=records_[i];
            if((r.state==CommandState::reserved||r.state==CommandState::pending)&&now>=r.deadline)
                return fail(CommandError::timeout);
        }
        return true;
    }
    bool deliver(CommandRecord &r,const FirmwareEvent &event){
        if(!r.receiver.event)return true;
        // retire/record the ACK BEFORE notifying the owner. The station owner
        // can synchronously enqueue its next command from this callback.
        // Nested event delivery or invalidation still poisons the entire epoch.
        const auto receiver=r.receiver;const auto token=r.token;
        delivering_=true;const bool ok=receiver.event(receiver.owner,event,epoch_,token);delivering_=false;
        return (ok||fail(CommandError::receiver))&&check();
    }
public:
    FirmwareCommands(Transport &io,uint64_t verifiedEpoch):io_(io),epoch_(verifiedEpoch){}
    FirmwareCommands(const FirmwareCommands&)=delete;FirmwareCommands&operator=(const FirmwareCommands&)=delete;
    uint64_t epoch()const{return epoch_;}CommandError error()const{return error_;}
    bool faulted()const{return error_!=CommandError::none;}
    unsigned allocated()const{return next_;}
    const CommandRecord &record(uint8_t sequence)const{return records_[sequence];}
    void invalidate(){fail(CommandError::invalidated);}
    bool service(){return check();}
    // The same allocator covers role/join, BT policy, and fire-and-forget H2C.
    // Like upstream u8 h2c_seq, wrap only onto an unused/completed record.
    // Never replace pending/reserved commands. ACK wire format has no generation:
    // a duplicate delayed across a full 256-command cycle with the same ID cannot
    // be distinguished; this relies on normal firmware completion semantics.
    // RX epoch still rejects events from an old physical queue incarnation.
    bool reserve(CommandReceiver receiver,uint64_t token,uint64_t timeoutUs,uint8_t &sequence){
        sequence=0;if(publishing_||!check()||!timeoutUs||timeoutUs>10000000||
            bool(receiver.owner)!=bool(receiver.event)||UINT64_MAX-lastTime_<timeoutUs)return false;
        if(next_==UINT32_MAX)return fail(CommandError::exhausted);
        auto &r=records_[uint8_t(next_)];
        if(r.state==CommandState::reserved||r.state==CommandState::pending)return fail(CommandError::exhausted);
        sequence=uint8_t(next_++);r=CommandRecord{};
        r.state=CommandState::reserved;r.receiver=receiver;r.token=token;r.deadline=lastTime_+timeoutUs;return true;
    }
    bool publish(const uint8_t *bytes,size_t length){
        if(publishing_||!check()||!bytes||length<8||length>sizeof(packet_))return false;
        const auto w0=little32(bytes),w1=little32(bytes+4);const auto sequence=uint8_t(w0>>24);
        // Reject reserved AX header fields and inconsistent lengths before DMA.
        if((w0&0x00ff0000)||(w1&0xffff0000)||(w1&0x3fff)!=length)return false;
        auto &r=records_[sequence];if(r.state!=CommandState::reserved)return false;
        r.id={uint8_t(w0&3),uint8_t((w0>>2)&63),uint8_t(w0>>8)};
        r.receiveAck=(w1&0x4000)!=0;r.doneAck=(w1&0x8000)!=0;
        // No-ACK commands cannot produce a completion notification.
        if(r.receiver.event&&!r.receiveAck&&!r.doneAck)return false;
        for(size_t i=0;i<length;++i)packet_[i]=bytes[i];
        r.state=CommandState::pending;publishing_=true;
        const bool ok=io_.publish(packet_,length);publishing_=false;
        if(!ok)return fail(CommandError::transport); // may already be device visible
        if(!check())return false;
        if(!r.receiveAck&&!r.doneAck)r.state=CommandState::complete;
        return true;
    }
    bool submit(CommandId id,bool receiveAck,bool doneAck,const uint8_t *payload,size_t length,
                CommandReceiver receiver,uint64_t token,uint64_t timeoutUs,uint8_t &sequence){
        sequence=0;
        if(publishing_||id.category>3||id.commandClass>63||(!payload&&length)||length>sizeof(packet_)-8)return false;
        // An incidental periodic Receive ACK is transport housekeeping, not a
        // caller-requested completion contract. Reject before reserving so a
        // malformed request cannot leave a record that later faults every user.
        if(receiver.event&&!receiveAck&&!doneAck)return false;
        if(!reserve(receiver,token,timeoutUs,sequence))return false;
        size_t encoded=0;
        if(!encodeH2c(id,sequence,receiveAck,doneAck,payload,length,packet_,sizeof(packet_),encoded))return false;
        return publish(packet_,encoded);
    }
    CommandEvent accept(const FirmwareEvent &event,uint64_t receiveEpoch){
        // Epoch comes from the ORIGINAL receive queue, never the current bus.
        if(receiveEpoch!=epoch_)return CommandEvent::unrelated;
        if(publishing_||delivering_){fail(CommandError::receiver);return CommandEvent::fault;}
        if(!check())return CommandEvent::fault;
        FirmwareAck ack{};if(!decodeAck(event,ack))return CommandEvent::unrelated;
        auto &r=records_[ack.sequence];
        if(r.state!=CommandState::pending||!sameCommand(ack.command,r.id))return CommandEvent::unrelated;
        if(ack.done){
            if(!r.doneAck)return CommandEvent::unrelated;
            r.state=CommandState::complete;
            if(ack.returnCode){fail(CommandError::firmwareRejected);(void)deliver(r,event);return CommandEvent::fault;}
            return deliver(r,event)?CommandEvent::completed:CommandEvent::fault;
        }
        if(!r.receiveAck||r.received)return CommandEvent::unrelated;
        r.received=true;
        if(r.doneAck)return CommandEvent::received; // reception is not execution
        r.state=CommandState::complete;
        return deliver(r,event)?CommandEvent::completed:CommandEvent::fault;
    }
};
// Stable per-client binding used by native RFK I/O. It retains no payload and
// borrows the global bus/receiver. All methods run on that bus's workloop gate.
template<class Transport> class FirmwareCommandClient {
    FirmwareCommands<Transport> &commands_;CommandReceiver receiver_;
    const uint64_t token_,timeout_;
    static bool available(void *p){return static_cast<FirmwareCommandClient *>(p)->commands_.service();}
    static bool submit(void *p,CommandId id,bool done,const uint8_t *data,size_t length,uint8_t &sequence){
        auto &c=*static_cast<FirmwareCommandClient *>(p);
        return c.commands_.submit(id,false,done,data,length,done?c.receiver_:CommandReceiver{},c.token_,c.timeout_,sequence);
    }
    static void invalidate(void *p){static_cast<FirmwareCommandClient *>(p)->commands_.invalidate();}
public:
    FirmwareCommandClient(FirmwareCommands<Transport> &commands,CommandReceiver receiver,uint64_t token,uint64_t timeoutUs):
        commands_(commands),receiver_(receiver),token_(token),timeout_(timeoutUs){}
    FirmwareCommandClient(const FirmwareCommandClient&)=delete;FirmwareCommandClient&operator=(const FirmwareCommandClient&)=delete;
    FirmwareCommandLink link(){return {this,available,submit,invalidate};}
};
} }
