// SPDX-License-Identifier: BSD-3-Clause
// RTL8852B RF calibration arbitration adapted from Realtek rtw89 coex.c,
// coex.h, mac.c, fw.c/fw.h/core.h/reg.h, BSD option, fixed commit
// d1fced1b8a741dc9f92b47c69489c24385945f6e.
// Copyright(c) 2019-2022 Realtek Corporation
#pragma once
#include "FirmwareProtocol.hpp"

namespace rtl8852be { namespace bt {
constexpr uint32_t scoreboardRegister=0xac, firmwareControl=0x1e0;
constexpr uint32_t cmacFunctionRegister=0xc000;
constexpr uint32_t schedulerRegister=0xc348, controlPathRegister=0x73;
constexpr uint32_t lteControl=0xdaf0,lteWriteData=0xdaf4,lteReadData=0xdaf8;
constexpr uint32_t grantRegister=0x38,priorityRegister=0xc67c;
constexpr uint32_t wlRfkBit=1U<<11,tdmaBit=1U<<9;
constexpr uint32_t btRfkRun=1U<<5,btRfkRequest=1U<<6;
constexpr uint32_t grantMask=0xff00ff00,calibrationGrants=0x77007700;
constexpr uint64_t requestTimeoutUs=100000,calibrationTimeoutUs=300000;
constexpr network::CommandId policyCommand{2,0x10,3},rfkInfoCommand{2,0x10,5};

enum class RfkType : uint8_t {iqk=0,lck=1,dpk=2,txgapk=3,dack=4,rxdck=5,tssi=6,channel=7};
enum class RfkState : uint8_t {stop=0,start=1,oneshotStart=2,oneshotStop=3};
struct Request {RfkType type{};uint8_t paths{},phy{},band{};};
inline bool validRequest(const Request &r){
    // 8852BE in this driver is single-CMAC/non-DBCC. Do not silently route PHY1.
    return uint8_t(r.type)<=7&&r.paths>=1&&r.paths<=3&&r.phy==1&&r.band<=1;
}
// This is the driver's W2B shadow, NOT a read of R_AX_SCOREBOARD. The register's
// read side is B2W data. All coexistence users must share this shadow and lane.
struct Scoreboard {uint32_t value{};bool valid{};};
struct PolicySnapshot {
    uint8_t tdma[8]{};uint16_t offDuration{};uint32_t offTable{};uint16_t offType{};
    bool valid{};
};
struct Protocol {
    uint8_t tdmaVersion{},slotVersion{};
    // Upstream's three RTL8852B firmware version entries all select 3 / 1.
    // Version is (major << 24) | (minor << 16) | (sub << 8) | index.
    static Protocol select(uint32_t version){
        return version>=0x001b0000&&version<0x001e0000?Protocol{3,1}:Protocol{};
    }
    bool valid()const{return tdmaVersion==3&&slotVersion==1;}
};
inline bool encodePolicy(const Protocol &v,const PolicySnapshot &p,uint8_t (&out)[26]){
    if(!v.valid()||!p.valid)return false;
    // _append_tdma v3 includes fver + 3 reserved bytes before eight TDMA bytes.
    for(auto &b:out)b=0;
    out[0]=0;out[1]=12;out[2]=v.tdmaVersion;
    for(unsigned i=0;i<8;++i)out[6+i]=p.tdma[i];
    // _append_slot_v1: TLV {type=1,len=10}, fver, sid=OFF, slot[8].
    out[14]=1;out[15]=10;out[16]=v.slotVersion;out[17]=0;
    network::store16(out+18,p.offDuration);network::store32(out+20,p.offTable);
    network::store16(out+24,p.offType);return true;
}
inline PolicySnapshot calibrationPolicy(){
    // BTC_CXP_OFF_WL: t_def[CXTD_OFF], s_def[CXST_OFF], cxtbl[1].
    PolicySnapshot p{};p.offDuration=100;p.offTable=0xaaaaaaaa;p.valid=true;return p;
}
inline bool encodeRfkInfo(const Request &r,RfkState state,uint8_t (&out)[6]){
    if(!validRequest(r)||uint8_t(state)>3)return false;
    out[0]=4;out[1]=4; // CXDRVINFO_RFK, sizeof(rfk bitfield).
    network::store32(out+2,uint32_t(state)|(uint32_t(r.paths)<<2)|
        (uint32_t(r.phy)<<6)|(uint32_t(r.band)<<8)|(uint32_t(r.type)<<10));return true;
}

enum class Error {none,invalid,ownership,io,clock,cancelled,btBusy,invalidScoreboard,
    firmwareUnavailable,cmacUnavailable,txNotStopped,lteTimeout,readback,firmwareCommand,firmwareRejected,
    watchdog,sequenceReuse,calibrationFailure};
enum class Stage {idle,waitingStartAck,calibrating,waitingStopAck,complete,fault};
enum class EventResult {unrelated,consumed,fault};
struct Result {
    Error error{Error::none};Stage stage{Stage::idle};
    uint32_t address{},value{},btScoreboard{};unsigned operations{},btPolls{},ltePolls{};
    bool ownershipHeld{},requiresRecovery{},startAcknowledged{},stopAcknowledged{},oneshot{};
    bool scoreboardAttempted{},grantsAttempted{},policyAttempted{},hardwareRestored{};
};

// Io contract (one serialized controller workloop, borrowed lifetime):
//   inGate(), cancelled(), nowUs(), delayUs(n), read/write 8/16/32(address,value)
//   submitH2c(CommandId, bool doneAck, payload, bytes, uint8_t &sequence)
//   invalidateFirmwareEpoch()
// submitH2c must COPY payload and allocate a safe sequence in the SHARED device
// command queue; it must not invoke this coordinator recursively. Acceptance is
// not a firmware ACK. Feed actual C2H events to acceptEvent(). Once invalidated,
// the queue cannot reuse sequences until a verified hardware/firmware reset and
// stale event/DMA drain. C2H carries no epoch; software epoch labels alone cannot
// disambiguate an old ACK after wire sequence wrap.
//
// begin() only submits work. CalibrationControl.begin must consume an already
// ready() lease. Never block a workloop waiting for its own C2H callback.
template<class Io> class RfkCoordination {
    Io &io_;Scoreboard &shadow_;Protocol protocol_;PolicySnapshot baseline_;
    Request request_{};uint32_t savedShadow_{},expectedShadow_{},savedGrant_{};uint16_t savedPriority_{};
    uint8_t savedControl_{},pendingSequence_{},oneshotMap_{};uint32_t usedSequences_[8]{};
    bool clockStarted_{},busy_{},pending_{};
    uint64_t lastTime_{},acquiredAt_{};

    bool fail(Error e,uint32_t address=0,uint32_t value=0){
        if(result.error==Error::none){result.error=e;result.address=address;result.value=value;}
        result.requiresRecovery=result.ownershipHeld;
        result.stage=Stage::fault;pending_=false;
        // A command can have reached hardware even if its transport returned an
        // error. Never permit ambiguous ACKs to authorize a later calibration.
        if(result.policyAttempted)io_.invalidateFirmwareEpoch();
        return false;
    }
    bool timeCheck(){
        const auto now=io_.nowUs();
        if(clockStarted_&&now<lastTime_)return fail(Error::clock);
        clockStarted_=true;lastTime_=now;return true;
    }
    bool check(){
        if(result.error!=Error::none)return false;
        if(!io_.inGate())return fail(Error::ownership);
        if(io_.cancelled())return fail(Error::cancelled);
        if(!timeCheck())return false;
        if(result.ownershipHeld&&(!shadow_.valid||shadow_.value!=expectedShadow_))return fail(Error::ownership);
        if(result.ownershipHeld&&lastTime_-acquiredAt_>=calibrationTimeoutUs)
            return fail(Error::watchdog);
        return true;
    }
    bool enter(){
        if(busy_)return fail(Error::ownership);
        if(!check())return false;busy_=true;return true;
    }
    struct Guard {bool &b;~Guard(){b=false;}};
    bool read8(uint32_t a,uint8_t &v){v=0;if(!check())return false;++result.operations;
        return (io_.read8(a,v)||fail(Error::io,a))&&check();}
    bool read16(uint32_t a,uint16_t &v){v=0;if(!check())return false;++result.operations;
        return (io_.read16(a,v)||fail(Error::io,a))&&check();}
    bool read32(uint32_t a,uint32_t &v){v=0;if(!check())return false;++result.operations;
        return (io_.read32(a,v)||fail(Error::io,a))&&check();}
    bool write8(uint32_t a,uint8_t v){if(!check())return false;++result.operations;
        return (io_.write8(a,v)||fail(Error::io,a,v))&&check();}
    bool write16(uint32_t a,uint16_t v){if(!check())return false;++result.operations;
        return (io_.write16(a,v)||fail(Error::io,a,v))&&check();}
    bool write32(uint32_t a,uint32_t v){if(!check())return false;++result.operations;
        return (io_.write32(a,v)||fail(Error::io,a,v))&&check();}
    bool delay(unsigned us){return check()&&(io_.delayUs(us)||fail(Error::io))&&check();}
    bool verifyTxStopped(){
        uint16_t tx=0;if(!read16(schedulerRegister,tx))return false;
        return tx==0||fail(Error::txNotStopped,schedulerRegister,tx);
    }
    bool readBt(){
        uint32_t v=0;if(!read32(scoreboardRegister,v))return false;
        if(v==0xffffffff||v==0xdeadbeef)return fail(Error::invalidScoreboard,scoreboardRegister,v);
        result.btScoreboard=v;return true;
    }
    bool awaitBt(){
        const auto start=lastTime_;
        // Count bound also terminates with a frozen model/OS clock.
        for(unsigned i=0;i<=requestTimeoutUs/40;++i){
            if(!readBt())return false;++result.btPolls;
            if(lastTime_-start>requestTimeoutUs)break;
            if(!(result.btScoreboard&(btRfkRun|btRfkRequest)))return true;
            if(lastTime_-start>=requestTimeoutUs||i==requestTimeoutUs/40)break;
            if(!delay(40))return false;
        }
        // Unlike upstream's timeout override, do not claim BT permission after
        // a local polling timeout or reuse an old firmware timeout indication.
        return fail(Error::btBusy,scoreboardRegister,result.btScoreboard);
    }
    bool lteReady(){
        const auto start=lastTime_;
        for(unsigned i=0;i<=1000;++i){uint8_t v=0;
            if(!read8(lteControl+3,v))return false;++result.ltePolls;
            if(lastTime_-start>50000)break;
            if(v&0x20)return true;
            if(lastTime_-start>=50000||i==1000)break;
            if(!delay(50))return false;
        }
        // Upstream logs then writes after timeout; this port refuses that write.
        return fail(Error::lteTimeout,lteControl);
    }
    bool readGrant(uint32_t &value){
        return lteReady()&&write32(lteControl,0x800f0000|grantRegister)&&
            lteReady()&&read32(lteReadData,value);
    }
    bool writeGrant(uint32_t value){
        if(!lteReady()||!write32(lteWriteData,value)||
           !write32(lteControl,0xc00f0000|grantRegister))return false;
        uint32_t readback=0;if(!readGrant(readback))return false;
        return readback==value||fail(Error::readback,grantRegister,readback);
    }
    bool writeScoreboard(uint32_t value){
        uint32_t bt=0;if(!read32(scoreboardRegister,bt))return false;
        if(bt==0xffffffff||bt==0xdeadbeef)return fail(Error::invalidScoreboard,scoreboardRegister,bt);
        // rtw89_mac_cfg_sb, powered-on TP-major form. FW upper bits preserved;
        // driver lower 24 bits originate exclusively in the shared W2B shadow.
        const uint32_t word=0x80000000|((bt&0x7f000000)|0x01000000)|(value&0x00ffffff);
        result.scoreboardAttempted=true;
        if(!write32(scoreboardRegister,word))return false;
        // The read direction returns BT state: a W2B readback comparison would
        // be invalid. Preserve the mandatory 1 ms information delivery delay.
        expectedShadow_=shadow_.value=value&0x00ffffff;
        return delay(1000);
    }
    bool setGrants(bool restore){
        uint8_t c=restore?savedControl_:uint8_t(savedControl_|4);
        uint16_t p=restore?savedPriority_:uint16_t(0x100);
        result.grantsAttempted=true;
        if(!write8(controlPathRegister,c)||
           !writeGrant(restore?savedGrant_:(savedGrant_&~grantMask)|calibrationGrants)||
           !write16(priorityRegister,p))return false;
        uint8_t cr=0;uint16_t pr=0;
        if(!read8(controlPathRegister,cr)||!read16(priorityRegister,pr))return false;
        if(cr!=c)return fail(Error::readback,controlPathRegister,cr);
        if(pr!=p)return fail(Error::readback,priorityRegister,pr);
        return true;
    }
    bool submit(const PolicySnapshot &policy,Stage next){
        uint8_t bytes[26]{};
        if(!encodePolicy(protocol_,policy,bytes))return fail(Error::invalid);
        uint8_t sequence=0;result.policyAttempted=true;
        if(!io_.submitH2c(policyCommand,true,bytes,sizeof(bytes),sequence))return fail(Error::firmwareCommand);
        if(!check())return false;
        const uint32_t bit=1U<<(sequence&31);
        if(usedSequences_[sequence>>5]&bit)return fail(Error::sequenceReuse);
        usedSequences_[sequence>>5]|=bit;pendingSequence_=sequence;
        pending_=true;result.stage=next;return true;
    }
    bool restore(){
        // RFK engines have completed before finish(); on any failure they must
        // be reset by the device owner. We never release BT ownership on error.
        if(!verifyTxStopped()||!setGrants(true)||!writeScoreboard(savedShadow_))return false;
        result.hardwareRestored=true;result.ownershipHeld=false;
        result.oneshot=false;result.stage=Stage::complete;return true;
    }
public:
    Result result{};
    RfkCoordination(Io &io,Scoreboard &shadow,Protocol protocol,const PolicySnapshot &baseline):
        io_(io),shadow_(shadow),protocol_(protocol),baseline_(baseline){}
    RfkCoordination(const RfkCoordination &)=delete;
    RfkCoordination &operator=(const RfkCoordination &)=delete;
    bool ready(){
        // Consuming readiness must revalidate time/gate/TX even when a queued
        // timer/service callback has not run. A cached stage is not a lease.
        if(result.stage!=Stage::calibrating||!result.ownershipHeld)return false;
        if(!enter())return false;Guard guard{busy_};return verifyTxStopped();
    }
    bool begin(Request request){
        if(!enter())return false;Guard guard{busy_};
        if(result.stage!=Stage::idle||!validRequest(request)||!protocol_.valid()||!baseline_.valid||
           !shadow_.valid||(shadow_.value&0xff000000)||(shadow_.value&wlRfkBit))return fail(Error::invalid);
        uint32_t fw=0;if(!read32(firmwareControl,fw))return false;
        if(fw==0xffffffff||fw==0xdeadbeef||(fw&0xe0)!=0xe0)return fail(Error::firmwareUnavailable,firmwareControl,fw);
        uint32_t cmac=0;if(!read32(cmacFunctionRegister,cmac))return false;
        if(cmac==0xffffffff||cmac==0xdeadbeef||!(cmac&0x40000000))return fail(Error::cmacUnavailable,cmacFunctionRegister,cmac);
        if(!verifyTxStopped()||!awaitBt())return false;
        request_=request;savedShadow_=shadow_.value;
        if(!read8(controlPathRegister,savedControl_)||!read16(priorityRegister,savedPriority_)||
           !readGrant(savedGrant_))return false;
        acquiredAt_=lastTime_;expectedShadow_=savedShadow_;result.ownershipHeld=true;
        if(!writeScoreboard((savedShadow_|wlRfkBit)&~tdmaBit)||!readBt())return false;
        // Close the arbitration window after publishing WLRFK. Never override
        // a BT calibration that started while our request was being published.
        if(result.btScoreboard&(btRfkRun|btRfkRequest))return fail(Error::btBusy,scoreboardRegister,result.btScoreboard);
        return setGrants(false)&&submit(calibrationPolicy(),Stage::waitingStartAck);
    }
    bool service(){
        if(!enter())return false;Guard guard{busy_};
        if(result.ownershipHeld&&!verifyTxStopped())return false;
        return true;
    }
    EventResult acceptEvent(const network::FirmwareEvent &event){
        network::FirmwareAck ack{};
        if(!network::decodeAck(event,ack)||!ack.done||!pending_||
           !network::sameCommand(ack.command,policyCommand)||ack.sequence!=pendingSequence_)
            return EventResult::unrelated;
        if(!enter())return EventResult::fault;Guard guard{busy_};
        pending_=false;
        if(ack.returnCode){fail(Error::firmwareRejected,0,ack.returnCode);return EventResult::fault;}
        if(!verifyTxStopped())return EventResult::fault;
        if(result.stage==Stage::waitingStartAck){result.startAcknowledged=true;result.stage=Stage::calibrating;}
        else if(result.stage==Stage::waitingStopAck){
            result.stopAcknowledged=true;if(!restore())return EventResult::fault;
        }else{fail(Error::ownership);return EventResult::fault;}
        return EventResult::consumed;
    }
    bool oneshot(uint8_t phyPath,bool start){
        if(!enter())return false;Guard guard{busy_};
        const uint8_t paths=phyPath&15,phy=(phyPath>>4)&3,band=phyPath>>6;
        if(result.stage!=Stage::calibrating||!result.ownershipHeld||
           phy!=request_.phy||band!=request_.band||!paths||
           (paths&~request_.paths)||result.oneshot==start||(!start&&phyPath!=oneshotMap_))return fail(Error::invalid);
        if(!verifyTxStopped())return false;result.oneshot=start;oneshotMap_=start?phyPath:0;return true;
    }
    bool finish(){
        if(!enter())return false;Guard guard{busy_};
        if(result.stage!=Stage::calibrating||!result.ownershipHeld||result.oneshot)return fail(Error::ownership);
        if(!verifyTxStopped())return false;
        return submit(baseline_,Stage::waitingStopAck);
    }
    bool calibrationFailed(){
        if(!enter())return false;Guard guard{busy_};
        // Deliberately retains WLRFK and hardware grants until the real device
        // recovery path verifies RFK/TX quiescence and starts a fresh epoch.
        return fail(Error::calibrationFailure);
    }
};
} }
