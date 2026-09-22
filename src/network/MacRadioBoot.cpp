// SPDX-License-Identifier: BSD-3-Clause
#include "MacRadioBoot.hpp"
#include "MacRadioIo.hpp"
#include "MacRfkIo.hpp"
#include "MacBtRfkIo.hpp"
#include "MacBtInitializationIo.hpp"
#include "MacPhyInitialization.hpp"
#include "BasebandGain.hpp"
#include <IOKit/IOLib.h>
namespace rtl8852be { namespace network {
using radioboot::Action;using radioboot::Step;using radioboot::Stage;using radioboot::Progress;
namespace {
uint32_t btVersion(uint32_t raw){return (raw<<24)|((raw&0xff00)<<8)|((raw>>8)&0xff00)|(raw>>24);}
rfk::Kind calibrationKind(Step step){
    switch(step){case Step::rck:return rfk::Kind::rck;case Step::dack:return rfk::Kind::dack;
    case Step::initialRxDc:case Step::rxDc:return rfk::Kind::rxDc;case Step::channel:return rfk::Kind::channel;
    case Step::iqk:return rfk::Kind::iqk;case Step::tssi:return rfk::Kind::tssi;case Step::dpk:return rfk::Kind::dpk;
    default:return rfk::Kind::scan;}
}
bt::RfkType coexistenceKind(rfk::Kind kind){
    switch(kind){case rfk::Kind::rck:return bt::RfkType::lck;case rfk::Kind::dack:return bt::RfkType::dack;
    case rfk::Kind::rxDc:return bt::RfkType::rxdck;case rfk::Kind::iqk:return bt::RfkType::iqk;
    case rfk::Kind::dpk:return bt::RfkType::dpk;case rfk::Kind::channel:return bt::RfkType::channel;
    default:return bt::RfkType::tssi;}
}
}
struct MacRadioBoot::State {
    IOWorkLoop &loop;const CalibrationSnapshot &calibration;const firmware::CapabilitySnapshot &caps;
    const firmware::Plan &plan;NativeFirmwareCommands &commands;firmware::Mailbox<firmware::MacMailboxIo> &mailbox;
    bool stopped{},failedState{},closureProven{},schedulerKnown{},leaseConsumed{},leaseEnded{},failureLogged{};uint16_t schedulerMask{};
    const char *phase{"allocated"};
    uint64_t commandToken{1},accessDeadline{};rfk::Kind expectedKind{};
    MacRadioIo radioIo;RadioAccess<MacRadioIo> access;RadioInitialization<MacRadioIo> radio;
    BasebandGain gain{};uint8_t pagesStorage[2][6000]{},packet[2008]{};
    RadioFirmwarePages pages[2]{};unsigned uploadPath{},uploadPage{};
    bt::Scoreboard scoreboard{};bt::PolicySnapshot baseline{};
    bt::initialization::MacBtInitializationIo btInitIo;
    bt::initialization::Initialization<bt::initialization::MacBtInitializationIo> btInit;
    bt::MacBtRfkIo btIo;bt::RfkCoordination<bt::MacBtRfkIo> *lease{};
    rfk::MacRfkIo rfkIo;rfk::Initialization<rfk::MacRfkIo> rfk;
    channel::ChannelProgramming<rfk::MacRfkIo> channel;channel::Channel target{};
    power::Policy policy{};MacPhyInitialization phy;
    radioboot::Sequence<State> sequence;

    static bool available(void *p){return static_cast<State *>(p)->commands.service();}
    static bool command(void *p,CommandId id,bool done,const uint8_t *bytes,size_t length,uint8_t &seq){
        auto &s=*static_cast<State *>(p);
        return !s.stopped&&s.commands.submit(id,false,done,bytes,length,
            done?CommandReceiver{&s,receive}:CommandReceiver{},s.commandToken,100000,seq);
    }
    static void invalidate(void *p){static_cast<State *>(p)->commands.invalidate();}
    FirmwareCommandLink link(){return {this,available,command,invalidate};}
    static bool receive(void *p,const FirmwareEvent &event,uint64_t epoch,uint64_t token){
        auto &s=*static_cast<State *>(p);
        if(s.stopped||s.failedState||!s.inGate()||epoch!=s.caps.epoch||token!=s.commandToken)return false;
        if(s.sequence.result.stage==Stage::btInitialization)
            return s.btInit.acceptEvent(event)==bt::EventResult::consumed;
        if(!s.lease)return false;
        return s.lease->acceptEvent(event)==bt::EventResult::consumed;
    }
    static bool tableGuard(void *p){
        auto &s=*static_cast<State *>(p);
        return s.inGate()&&!s.stopped&&!s.failedState&&s.btIo.nowUs()<s.accessDeadline&&s.paused();
    }
    static bool leaseBegin(void *p,rfk::Kind kind){
        auto &s=*static_cast<State *>(p);
        if(s.leaseConsumed||s.leaseEnded||kind!=s.expectedKind||!s.lease||!s.lease->ready())return false;
        s.leaseConsumed=true;return true;
    }
    static bool leaseCheck(void *p,rfk::Kind kind){
        auto &s=*static_cast<State *>(p);
        return !s.stopped&&!s.failedState&&s.inGate()&&kind==s.expectedKind&&s.leaseConsumed&&!s.leaseEnded&&
            s.lease&&s.lease->ready();
    }
    static bool leaseEnd(void *p,rfk::Kind kind,bool success){
        auto &s=*static_cast<State *>(p);
        if(!success||!leaseCheck(p,kind))return false;
        s.leaseEnded=true;return true; // firmware restore is submitted AFTER the RFK stack unwinds
    }
    static bool oneshot(void *p,rfk::Kind kind,uint8_t map,bool start){
        auto &s=*static_cast<State *>(p);
        return leaseCheck(p,kind)&&s.lease->oneshot(map,start);
    }
    static bool recovery(void *p,rfk::Kind){
        auto &s=*static_cast<State *>(p);
        // No local register sequence proves a partially executed KIP/NCTL/RF
        // recovery. Keep the lease and report reset to the real power owner.
        s.failedState=true;return false;
    }
    State(IOPCIDevice &d,IOMemoryMap &m,IOWorkLoop &w,const CalibrationSnapshot &c,
          const firmware::CapabilitySnapshot &cap,const firmware::Plan &f,NativeFirmwareCommands &cmd,
          firmware::Mailbox<firmware::MacMailboxIo> &mail):
        loop(w),calibration(c),caps(cap),plan(f),commands(cmd),mailbox(mail),
        radioIo(&d,&m,{this,tableGuard}),access(radioIo),radio(radioIo),
        btInitIo(&d,&m,&w,link()),btInit(btInitIo,scoreboard,baseline),btIo(&d,&m,&w,link()),
        rfkIo(&d,&m,{this,leaseBegin,leaseEnd,oneshot,recovery,leaseCheck}),rfk(rfkIo,c.cut),channel(rfkIo),
        phy(&d,&m,&w,{this,tableGuard}),sequence(*this){}
    ~State(){delete lease;}
    bool inGate(){return loop.inGate();}
    bool healthy(){return !stopped&&!failedState&&!mailbox.faulted()&&commands.service();}
    bool paused(){uint16_t tx=0;
        return inGate()&&!stopped&&!failedState&&schedulerKnown&&!schedulerMask&&!mailbox.faulted()&&
            btIo.read16(bt::schedulerRegister,tx)&&tx==0;
    }
    bool pause(){
        if(!inGate()||stopped||failedState||!mailbox.setSchedulerMask(0))return false;
        schedulerKnown=true;schedulerMask=0;return paused();
    }
    void failed(){
        // One pre-cleanup snapshot preserves the initiating hardware error.
        // Normal stop also calls failed() to close ownership; do not log it as
        // a fault merely because the root has invalidated the command epoch.
        if(!failureLogged&&(sequence.result.error!=radioboot::Error::none||failedState||
           rfk.result.error!=rfk::Error::none||phy.result().error!=PhyInitError::none||
           btInit.result.error!=bt::initialization::Error::none||channel.result.error!=channel::Error::none||
           (commands.error()!=CommandError::none&&commands.error()!=CommandError::invalidated)||
           (lease&&lease->result.error!=bt::Error::none))){
            failureLogged=true;const auto &p=phy.result();
            IOLog("RTL8852BE radio failure: phase=%s stage=%u step=%u error=%u cmd=%u used=%u "
                  "btinit=%u/%u@%08x btlease=%u/%u@%08x held=%u consumed=%u ended=%u native=%u "
                  "rfk=%u/%u space=%u path=%u@%08x mask=%08x value=%08x ops=%u polls=%u "
                  "phy=%u/%u@%08x expected=%08x actual=%08x channel=%u/%u@%08x radioio=%u\n",
                phase,unsigned(sequence.result.stage),unsigned(sequence.result.step),unsigned(sequence.result.error),
                unsigned(commands.error()),commands.allocated(),unsigned(btInit.result.stage),unsigned(btInit.result.error),btInit.result.address,
                lease?unsigned(lease->result.stage):0,lease?unsigned(lease->result.error):0,lease?lease->result.address:0,
                unsigned(lease&&lease->result.ownershipHeld),unsigned(leaseConsumed),unsigned(leaseEnded),unsigned(rfkIo.leaseActive()),
                unsigned(rfk.result.stage),unsigned(rfk.result.error),unsigned(rfk.result.space),unsigned(rfk.result.path),
                rfk.result.address,rfk.result.mask,rfk.result.value,rfk.result.operations,rfk.result.polls,
                unsigned(p.stage),unsigned(p.error),p.address,p.expected,p.actual,
                unsigned(channel.result.stage),unsigned(channel.result.error),channel.result.address,unsigned(radio.ioStatus()));
        }
        // Attempt real scheduler closure before poisoning shared command state.
        bool closed=false;
        if(inGate()&&!stopped&&!mailbox.faulted()){
            closed=mailbox.setSchedulerMask(0);schedulerKnown=closed;schedulerMask=0;}
        if(lease&&lease->result.ownershipHeld)(void)lease->calibrationFailed();
        const bool pmacStopped=rfkIo.stopCalibrationTx();
        closureProven=closed&&pmacStopped&&!rfkIo.leaseActive();commands.invalidate();failedState=true;
    }
    bool prepare(){
        phase="BB-table";
        accessDeadline=btIo.nowUs()+10000000;
        if(!radioIo.valid()||!btInitIo.valid()||!btIo.valid()||!rfkIo.valid()||!phy.valid()||!paused())return false;
        const auto rfe=calibration.board.rfe,cut=calibration.cut;
        if(radio.baseband(bbTable,rfe,cut).status!=RadioTableStatus::ok)return false;
        phase="power-unit";if(!phy.powerUnit())return false;
        phase="gain-table";if(loadBasebandGain(gainTable,rfe,cut,gain).status!=RadioTableStatus::ok)return false;
        phase="BB-reset";if(!phy.reset())return false;
        phase="RF-A";if(radio.radio(radioATable,rfe,cut,pagesStorage[0],sizeof(pagesStorage[0]),pages[0]).status!=RadioTableStatus::ok)return false;
        phase="RF-B";if(radio.radio(radioBTable,rfe,cut,pagesStorage[1],sizeof(pagesStorage[1]),pages[1]).status!=RadioTableStatus::ok)return false;
        return true;
    }
    Progress upload(){
        phase="RF-firmware-pages";
        if(!healthy()||!paused())return Progress::failed;
        if(uploadPath==2)return Progress::complete;
        auto &p=pages[uploadPath];uint8_t sequenceNumber=0;size_t length=0;
        if(!commands.reserve({},0,100000,sequenceNumber)||
           !p.encode(uploadPage,sequenceNumber,packet,sizeof(packet),length)||!commands.publish(packet,length))return Progress::failed;
        if(++uploadPage==p.pages()){++uploadPath;uploadPage=0;}
        return uploadPath==2?Progress::complete:Progress::pending;
    }
    bool startBt(){phase="BT-initialization";return btInit.begin({btVersion(plan.version),calibration.board.rfe,calibration.cut,true,true});}
    Progress pollBt(){
        if(!btInit.service())return Progress::failed;
        if(btInit.result.stage!=bt::initialization::Stage::complete)return Progress::pending;
        if(!btInit.ready())return Progress::failed;
        // core.c: BB/RF tables -> btc_ntfy_init -> phy_dm_init. NCTL and
        // captured gain bases therefore follow the acknowledged BT baseline.
        accessDeadline=btIo.nowUs()+10000000;
        phase="PHY-before-RFK";if(!phy.beforeRfk(calibration,caps))return Progress::failed;
        phase="NCTL";if(radio.nctl(calibration.board.rfe,calibration.cut)!=RadioIoStatus::ok)return Progress::failed;
        const auto &bases=phy.result();
        return rfk.configureCalibration(calibration.board,calibration.phy,bases.offsetBase,bases.rssiBase,caps.effectiveRxPaths())&&
            channel.configure(gain,calibration.board,calibration.phy,bases.offsetBase,bases.rssiBase,caps.effectiveRxPaths())?
            Progress::complete:Progress::failed;
    }
    bool requestLease(Step step){
        phase="BT-acquire";
        if(!paused()||rfkIo.leaseActive())return false;
        if(lease&&lease->result.stage!=bt::Stage::complete)return false;
        delete lease;lease=nullptr;
        if(commandToken==UINT64_MAX)return false;++commandToken;
        expectedKind=calibrationKind(step);leaseConsumed=leaseEnded=false;
        lease=new bt::RfkCoordination<bt::MacBtRfkIo>(btIo,scoreboard,bt::Protocol::select(btVersion(plan.version)),baseline);
        if(!lease)return false;
        const uint8_t band=sequence.action()==Action::initialize?0:target.band;
        return lease->begin({coexistenceKind(expectedKind),3,1,band});
    }
    Progress pollLease(){
        if(!lease||!lease->service())return Progress::failed;
        if(lease->result.stage!=bt::Stage::calibrating)return Progress::pending;
        return lease->ready()?Progress::complete:Progress::failed;
    }
    bool run(Step step){
        phase="calibration";
        if(!lease||!lease->ready()||calibrationKind(step)!=expectedKind)return false;
        const rfk::Channel c{target.band,target.width,target.center};
        switch(step){
        case Step::rck:return rfk.initializeRck();case Step::dack:return rfk.initializeDack();
        case Step::initialRxDc:return rfk.initializeRxDc();
        case Step::channel:return channel.program(target)&&channel.programPower(policy)&&channel.finish();
        case Step::rxDc:return rfk.calibrateRxDc(c);case Step::iqk:return rfk.calibrateIqOnly(c);
        case Step::tssi:return rfk.calibrateTssi();case Step::dpk:return rfk.calibrateDpk();
        case Step::scanBegin:return rfk.beginScan();case Step::scanTune:return rfk.prepareScanChannel(c);
        case Step::scanEnd:return rfk.finishScan(c);
        }
        return false;
    }
    bool finishLease(){phase="BT-restore";return lease&&leaseConsumed&&leaseEnded&&!rfkIo.leaseActive()&&lease->finish();}
    Progress pollRestore(){
        if(!lease||!lease->service())return Progress::failed;
        if(lease->result.stage!=bt::Stage::complete)return Progress::pending;
        return lease->result.stopAcknowledged&&lease->result.hardwareRestored&&!lease->result.ownershipHeld?
            Progress::complete:Progress::failed;
    }
    bool finishOperation(Action action){
        if(!paused())return false;
        if(action==Action::initialize){
            phase="PHY-power-trim-path";
            accessDeadline=btIo.nowUs()+1000000;
            return rfk.result.rckReady&&rfk.result.dackReady&&rfk.result.rxDcReady&&
                phy.powerReference()&&radio.powerTrim(calibration.phy)==RadioIoStatus::ok&&phy.afterRfk({0,0,1});
        }
        if(action==Action::scanBegin||action==Action::scanTune)return rfk.result.scanActive&&rfk.result.scanReady;
        if(!rfk.result.iqReady||!rfk.result.tssiReady||!rfk.result.dpkReady||rfk.result.scanActive)return false;
        return phy.result().afterRfkDone;
    }
};
MacRadioBoot::~MacRadioBoot(){
    if(state_&&!release())IOLog("RTL8852BE: radio state retained: stop/callback invalidation required\n");
}
bool MacRadioBoot::allocate(IOPCIDevice &d,IOMemoryMap &m,IOWorkLoop &w,const CalibrationSnapshot &c,
                            const firmware::CapabilitySnapshot &cap,const firmware::Plan &f,
                            NativeFirmwareCommands &cmd,firmware::Mailbox<firmware::MacMailboxIo> &mail){
    if(state_||!c.board.identityValid||c.cut>1||!f.valid||f.cut!=c.cut||!cap.epoch||cap.epoch!=cmd.epoch()||
       cap.cut!=c.cut||cap.rfe!=c.board.rfe||!bt::Protocol::select(btVersion(f.version)).valid())return false;
    for(unsigned i=0;i<6;++i)if(cap.mac[i]!=c.board.mac[i])return false;
    if(cap.effectiveRxPaths()<1||cap.effectiveRxPaths()>3||cap.effectiveTxPaths()<1||cap.effectiveTxPaths()>3)return false;
    state_=new State(d,m,w,c,cap,f,cmd,mail);return state_!=nullptr;
}
bool MacRadioBoot::begin(){return state_&&state_->sequence.begin(state_->btIo.nowUs());}
bool MacRadioBoot::service(uint64_t now){return state_&&state_->sequence.service(now);}
bool MacRadioBoot::ready()const{return state_&&!state_->failedState&&!state_->stopped&&!state_->commands.faulted()&&state_->sequence.result.initialized&&state_->sequence.result.stage==Stage::ready;}
bool MacRadioBoot::busy()const{return state_&&state_->sequence.busy();}
bool MacRadioBoot::tuned()const{return ready()&&state_->sequence.result.tuned;}
const radioboot::Result *MacRadioBoot::result()const{return state_?&state_->sequence.result:nullptr;}
bool MacRadioBoot::tune(channel::Channel c,power::Policy policy,bool scanning){
    if(!ready()||!channel::validChannel(c)||!policy.query||!policy.generation||policy.domain>=16)return false;
    auto &s=*state_;if(scanning!=s.sequence.result.scanning)return false;
    // Resolve the actual policy before retaining pointers or touching hardware.
    int16_t ceiling=0;if(!policy.query(policy.context,c.band,c.primary,ceiling)||ceiling<-64||ceiling>63)return false;
    s.target=c;s.policy=policy;return s.sequence.launch(scanning?Action::scanTune:Action::tune,s.btIo.nowUs());
}
bool MacRadioBoot::beginScan(){return tuned()&&state_->sequence.launch(Action::scanBegin,state_->btIo.nowUs());}
bool MacRadioBoot::endScan(){return ready()&&state_->sequence.result.scanning&&state_->sequence.launch(Action::scanEnd,state_->btIo.nowUs());}
bool MacRadioBoot::setTraffic(uint16_t mask){
    if(!state_||!state_->inGate()||(mask&~0x101))return false;auto &s=*state_;
    if(mask&&(!ready()||(s.lease&&s.lease->result.ownershipHeld)||
       (!s.sequence.result.fullCalibration&&!(mask==0x100&&s.rfk.result.scanActive&&s.rfk.result.scanReady))))return false;
    if(!s.healthy()||!s.mailbox.setSchedulerMask(mask))return false;
    s.schedulerKnown=true;s.schedulerMask=mask;return !mask?s.paused():true;
}
bool MacRadioBoot::setTraffic(station::Traffic traffic){
    switch(traffic){case station::Traffic::none:return setTraffic(uint16_t(0));
    case station::Traffic::management:case station::Traffic::scanProbe:return setTraffic(uint16_t(0x100));
    case station::Traffic::controlledPort:case station::Traffic::authorized:return setTraffic(uint16_t(0x101));}
    return false;
}
bool MacRadioBoot::schedulerPaused(){return state_&&state_->paused();}
bool MacRadioBoot::stationWindowOwned(){return ready()&&!busy()&&schedulerPaused();}
bool MacRadioBoot::stop(){
    if(!state_)return true;auto &s=*state_;if(!s.inGate())return false;
    if(s.stopped)return s.closureProven;
    if(!s.sequence.stop())return false;
    s.stopped=true;s.radioIo.cancel();s.rfkIo.cancel();s.btIo.cancel();s.btInitIo.cancel();s.phy.cancel();return s.closureProven;
}
bool MacRadioBoot::release(){
    if(!state_)return true;
    if(!state_->stopped||!state_->commands.faulted())return false;
    delete state_;state_=nullptr;return true;
}
} }
