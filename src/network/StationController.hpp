// SPDX-License-Identifier: BSD-3-Clause
// Station orchestration for AX/8852B. Role/join ordering follows pinned rtw89
// d1fced1b8a741dc9f92b47c69489c24385945f6e core.c/mac.c/fw.c.
#pragma once
#include "FirmwareProtocol.hpp"
#include "ChannelGeometry.hpp"
namespace rtl8852be { namespace station {
using network::CommandId;
using network::FirmwareEvent;
struct Token { uint64_t epoch{}, operation{}; };
inline bool same(Token a,Token b){return a.epoch==b.epoch&&a.operation==b.operation;}
inline bool sameChannel(channel::Channel a,channel::Channel b){
    return a.band==b.band&&a.width==b.width&&a.center==b.center&&a.primary==b.primary;
}
struct Address {uint8_t bytes[6]{};};
inline bool same(Address a,Address b){for(unsigned i=0;i<6;++i)if(a.bytes[i]!=b.bytes[i])return false;return true;}
inline bool unicast(Address a){uint8_t any=0;for(auto b:a.bytes)any|=b;return any&&!(a.bytes[0]&1);}
struct Interface {uint8_t macid{},macIndex{},wmm{},port{};Address address{};channel::Channel home{};};
struct Peer {Address bssid{};channel::Channel channel{};uint8_t ssid[32]{},ssidLength{};bool protectedNetwork{};};
struct Association {Address bssid{};channel::Channel channel{};uint16_t aid{};};
struct ScanChannel {channel::Channel channel{};uint16_t dwellMs{};bool active{};};
enum class Traffic {none,management,scanProbe,controlledPort,authorized};
enum class Action {prepareInterface,idleTables,scanBegin,scanTune,scanRestore,scanEnd,
    prepareAuthentication,associationCmac,associationCam,disconnectCmac,disconnectCam,removeCam};
enum class State {stopped,preparing,creatingRole,initialNoLink,installingIdle,idle,
    scanningBegin,scanningTune,scanningDwell,scanningRestore,scanningEnd,
    preparingAuthentication,authenticating,installingAssociationCmac,joining,
    installingAssociationCam,associated,authorized,disconnectingCmac,disconnecting,
    disconnectingCam,removingRole,removingCam,faulted};
enum class Error {none,backend,clock,timeout,firmwareRejected,sequenceExhausted,
    authentication,association,cancelled,invalidProtocol};
struct ActionRequest {
    Action action{};Token token{},association{};Interface interface{};Peer peer{};
    channel::Channel channel{};uint16_t aid{};bool activeScan{};
};
// All entry points execute under ONE controller gate, with monotonic microsecond
// timestamps. Backend calls MUST NOT reenter this object. Asynchronous callbacks
// retain the ORIGINAL token, never stamp an old callback with the current epoch.
//
// Required Backend operations (no optional/no-op success callbacks):
// bool inGate(); bool setTraffic(Traffic); bool reserveH2cSequence(uint8_t&);
// bool publishH2c(const uint8_t*,size_t,Token);
// bool beginAction(const ActionRequest&); bool beginAuthentication(Token,const Peer&);
// bool sendProbe(Token,const Peer*,const ScanChannel&); bool cancelProtocol(Token);
// void scanFinished(Token,bool cancelled); void recoveryRequired(Token,Error);
// bool firmwareRestartVerified(uint64_t newEpoch);
//
// beginAction accepts work; actionComplete(success) means its real register/DMA/
// firmware-ACK prerequisites completed. See docs/station-controller.md for exact
// action requirements. publishH2c must copy the stack buffer before returning,
// retain the DMA buffer until the EXISTING CH12 ownership ledger permits release,
// and ring/sync actual DMA. Neither DMA retirement nor receive ACK is DONE ACK.
// The single global H2C allocator MUST NOT reuse a sequence in a firmware epoch.
// Wire ACK has NO epoch. Exhaustion requires firmware restart + RX drain; a host
// operation counter or timer expiry cannot make a reused wire sequence safe.
template<class Backend> class Controller {
    Backend &io_;
    State state_{State::stopped},scanResume_{State::idle};Error error_{Error::none};
    Interface interface_{};Peer peer_{};Association association_{};
    ScanChannel channels_[64]{};size_t channelCount_{},channelIndex_{};
    Token epoch_{},associationToken_{},scanToken_{},pendingToken_{};
    Action pendingAction_{};CommandId pendingCommand_{};uint8_t pendingSequence_{};
    uint64_t serial_{},lastNow_{},deadline_{},scanDeadline_{};
    bool clockStarted_{},waitingAction_{},waitingCommand_{},roleCreated_{},
         authenticated_{},scanCancelled_{},disconnectRequested_{};
    // Hardware actions include TX drain plus a bounded multi-phase radio tune.
    // Individual firmware DONE ACKs retain the tighter independent deadline.
    static constexpr uint64_t actionTimeout=12000000,commandTimeout=2000000,authenticationTimeout=10000000;
    bool fail(Error error){
        if(state_==State::faulted)return false;
        error_=error;state_=State::faulted;waitingAction_=waitingCommand_=false;
        // Retain all external resources. Only a verified stopped/reset epoch
        // permits reclamation; a failed command may already be device-visible.
        io_.setTraffic(Traffic::none);io_.recoveryRequired(epoch_,error);return false;
    }
    bool time(uint64_t now){
        if(!io_.inGate()||state_==State::faulted)return false;
        if(clockStarted_&&now<lastNow_)return fail(Error::clock);
        lastNow_=now;clockStarted_=true;return true;
    }
    bool arm(uint64_t duration){
        if(UINT64_MAX-lastNow_<duration)return fail(Error::clock);
        deadline_=lastNow_+duration;return true;
    }
    Token token(){
        if(serial_==UINT64_MAX){fail(Error::sequenceExhausted);return {};}
        return {epoch_.epoch,++serial_};
    }
    bool traffic(Traffic value){return io_.setTraffic(value)||fail(Error::backend);}
    bool scanning()const{return state_>=State::scanningBegin&&state_<=State::scanningEnd;}
    bool linked()const{return state_==State::associated||state_==State::authorized;}
    bool action(Action kind,State state,channel::Channel target={}){
        if(waitingAction_||waitingCommand_)return fail(Error::backend);
        pendingToken_=token();if(state_==State::faulted)return false;
        pendingAction_=kind;waitingAction_=true;state_=state;
        if(!arm(actionTimeout))return false;
        ActionRequest request{};request.action=kind;request.token=pendingToken_;
        request.association=associationToken_;request.interface=interface_;request.peer=peer_;
        request.channel=target;request.aid=association_.aid;
        request.activeScan=kind==Action::scanTune&&channels_[channelIndex_].active;
        return io_.beginAction(request)||fail(Error::backend);
    }
    bool command(bool role,bool removeOrDisconnect,State state){
        if(waitingAction_||waitingCommand_)return fail(Error::backend);
        uint8_t sequence=0;
        if(!io_.reserveH2cSequence(sequence))return fail(Error::sequenceExhausted);
        // Shared command bus owns pending-slot exclusion and sequence wrap.
        uint8_t bytes[12]{};size_t length=0;bool encoded;
        if(role){
            network::RoleCommand request{};request.macid=interface_.macid;
            request.selfRole=0;request.wifiRole=1;request.updateMode=removeOrDisconnect?1:0;
            encoded=network::encodeRole(request,sequence,bytes,sizeof(bytes),length);
            pendingCommand_={1,8,4};
        }else{
            network::JoinCommand request{};request.macid=interface_.macid;
            request.band=interface_.macIndex;request.wmm=interface_.wmm;request.port=interface_.port;
            request.selfRole=0;request.wifiRole=1;request.netType=removeOrDisconnect?0:2;
            request.disconnect=removeOrDisconnect;
            encoded=network::encodeJoin(request,sequence,bytes,sizeof(bytes),length);
            pendingCommand_={1,8,0};
        }
        if(!encoded)return fail(Error::backend);
        pendingToken_=token();if(state_==State::faulted)return false;
        pendingSequence_=sequence;waitingCommand_=true;state_=state;
        if(!arm(commandTimeout))return false;
        return io_.publishH2c(bytes,length,pendingToken_)||fail(Error::backend);
    }
    bool beginDisconnect(){
        if(!traffic(Traffic::none))return false;
        if(!io_.cancelProtocol(associationToken_))return fail(Error::backend);
        return action(Action::disconnectCmac,State::disconnectingCmac);
    }
    bool tuneScan(){
        if(!traffic(Traffic::none))return false;
        return action(Action::scanTune,State::scanningTune,channels_[channelIndex_].channel);
    }
    bool restoreScan(){
        if(!traffic(Traffic::none))return false;
        const auto home=scanResume_==State::idle?interface_.home:peer_.channel;
        return action(Action::scanRestore,State::scanningRestore,home);
    }
    bool completeScan(){
        state_=scanResume_;
        const auto mode=state_==State::authorized?Traffic::authorized:
            (state_==State::associated?Traffic::controlledPort:Traffic::management);
        // A pending disconnect must not briefly reopen the controlled port.
        if(!traffic(disconnectRequested_?Traffic::none:mode))return false;
        io_.scanFinished(scanToken_,scanCancelled_);scanToken_={};
        if(disconnectRequested_)return beginDisconnect();
        return true;
    }
    bool expire(){
        if(scanning()&&lastNow_>=scanDeadline_)return fail(Error::timeout);
        if(waitingAction_||waitingCommand_){if(lastNow_>=deadline_)return fail(Error::timeout);}
        else if(state_==State::authenticating&&lastNow_>=deadline_){
            error_=Error::timeout;disconnectRequested_=true;return beginDisconnect();
        }
        return true;
    }
public:
    explicit Controller(Backend &io):io_(io){}
    Controller(const Controller&)=delete;Controller&operator=(const Controller&)=delete;
    State state()const{return state_;}Error error()const{return error_;}
    bool requiresRecovery()const{return state_==State::faulted;}
    bool associated()const{return linked()||(scanning()&&scanResume_!=State::idle);}
    bool portAuthorized()const{return state_==State::authorized;}
    bool roleCreated()const{return roleCreated_;}
    Token associationToken()const{return associationToken_;}
    Token scanToken()const{return scanToken_;}
    // Read-only TX admission; unlike tick(), safe inside a driver's TX pump.
    // Recheck immediately before publication after any allocation/preparation.
    bool canSendScanProbe(Token original,uint64_t now)const{
        return state_==State::scanningDwell&&!scanCancelled_&&!disconnectRequested_&&
            same(original,scanToken_)&&original.operation&&channelIndex_<channelCount_&&
            channels_[channelIndex_].active&&now>=lastNow_&&now<deadline_&&now<scanDeadline_;
    }
    Token pendingToken()const{return pendingToken_;}
    bool commandPending()const{return waitingCommand_;}
    // Legal only after DMA stopped, firmware restarted, C2H/interrupt callbacks
    // drained and queues reset by the owner. FirmwareResetVerified checks THAT
    // owner's proof. A logical reconnect/sequence wrap is not a firmware epoch.
    bool restart(uint64_t newEpoch,uint64_t now){
        if(!io_.inGate()||!newEpoch||newEpoch<=epoch_.epoch||!io_.firmwareRestartVerified(newEpoch))return false;
        if(!io_.setTraffic(Traffic::none))return false;
        state_=State::stopped;error_=Error::none;epoch_={newEpoch,0};serial_=0;
        associationToken_=scanToken_=pendingToken_={};waitingAction_=waitingCommand_=roleCreated_=false;
        authenticated_=scanCancelled_=disconnectRequested_=false;channelCount_=channelIndex_=0;
        interface_={};peer_={};association_={};
        lastNow_=now;clockStarted_=true;return true;
    }
    bool start(Interface config,uint64_t now){
        if(!time(now)||state_!=State::stopped||!epoch_.epoch||!unicast(config.address)||
           config.macIndex!=0||config.macid>=128||config.wmm>3||config.port>4||!channel::validChannel(config.home))return false;
        interface_=config;error_=Error::none;
        return traffic(Traffic::none)&&action(Action::prepareInterface,State::preparing,config.home);
    }
    bool scan(const ScanChannel *channels,size_t count,uint64_t now){
        // Do not interrupt the RSN four-way handshake with a background scan.
        if(!time(now)||(state_!=State::idle&&state_!=State::authorized)||!channels||!count||count>64)return false;
        uint64_t dwell=0;
        for(size_t i=0;i<count;++i){
            if(!channel::validChannel(channels[i].channel)||channels[i].channel.width!=0||
               channels[i].dwellMs<10||channels[i].dwellMs>1000)return false;
            dwell+=uint64_t(channels[i].dwellMs)*1000;
        }
        const uint64_t budget=dwell+(count+4)*actionTimeout;
        if(UINT64_MAX-now<budget)return fail(Error::clock);
        for(size_t i=0;i<count;++i)channels_[i]=channels[i];
        channelCount_=count;channelIndex_=0;scanResume_=state_;scanCancelled_=false;
        scanToken_=token();if(state_==State::faulted)return false;scanDeadline_=now+budget;
        return traffic(Traffic::none)&&action(Action::scanBegin,State::scanningBegin);
    }
    bool cancelScan(Token original,uint64_t now){
        if(!time(now)||!scanning()||!same(original,scanToken_)||!expire())return false;
        scanCancelled_=true;
        if(state_==State::scanningDwell)return restoreScan();
        return true; // finish the already submitted tune before restoring home
    }
    bool connect(const Peer &peer,uint64_t now){
        if(!time(now)||state_!=State::idle||!unicast(peer.bssid)||peer.ssidLength>32||
           !channel::validChannel(peer.channel))return false;
        peer_=peer;association_={};authenticated_=false;error_=Error::none;
        associationToken_=token();if(state_==State::faulted)return false;
        return traffic(Traffic::none)&&action(Action::prepareAuthentication,State::preparingAuthentication,peer.channel);
    }
    // Called ONLY from the real protocol authentication receive/state path.
    // An H2C ACK, TX report, timer, or our own transmitted frame is not evidence.
    bool authenticated(Token original,Address bssid,bool success,uint64_t now){
        if(!time(now)||state_!=State::authenticating||!same(original,associationToken_)||
           !same(bssid,peer_.bssid)||!expire())return false;
        if(!success){error_=Error::authentication;disconnectRequested_=true;return beginDisconnect();}
        authenticated_=true;return true;
    }
    // Supply the validated AP association response and negotiated AID from
    // net80211; not a synthetic RUN request issued by this controller.
    bool associationReceived(Token original,const Association &peer,uint64_t now){
        if(!time(now)||state_!=State::authenticating||!authenticated_||!same(original,associationToken_)||
           !same(peer.bssid,peer_.bssid)||!sameChannel(peer.channel,peer_.channel)||
           !peer.aid||peer.aid>2007||!expire())return false;
        association_=peer;return action(Action::associationCmac,State::installingAssociationCmac);
    }
    bool associationRejected(Token original,Address bssid,uint16_t status,uint64_t now){
        if(!time(now)||state_!=State::authenticating||!authenticated_||!same(original,associationToken_)||
           !same(bssid,peer_.bssid)||!status||!expire())return false;
        error_=Error::association;disconnectRequested_=true;return beginDisconnect();
    }
    // IEEE80211_NODE_PORT_VALID after RSN/EAPOL/key installation (or the real
    // open-network association path). Join DONE alone never opens this gate.
    bool authorizePort(Token original,uint64_t now){
        if(!time(now)||state_!=State::associated||!same(original,associationToken_))return false;
        if(!traffic(Traffic::authorized))return false;
        state_=State::authorized;return true;
    }
    // RSN rekey/key-removal can invalidate the controlled port while a scan is
    // away from home. Remember that change instead of reopening data on return.
    bool revokePort(Token original,uint64_t now){
        if(!time(now)||!same(original,associationToken_)||!expire())return false;
        if(scanning()&&scanResume_!=State::idle){scanResume_=State::associated;return true;}
        if(!linked())return false;
        if(!traffic(Traffic::controlledPort))return false;
        state_=State::associated;return true;
    }
    bool disconnect(uint64_t now){
        if(!time(now)||!expire())return false;
        if(state_==State::idle)return true;
        if(scanning()){
            if(scanResume_!=State::idle)disconnectRequested_=true;
            return cancelScan(scanToken_,now);
        }
        if(state_<State::preparingAuthentication||state_>State::disconnectingCam)return false;
        disconnectRequested_=true;if(!traffic(Traffic::none))return false;
        if(waitingAction_||waitingCommand_)return true;
        return beginDisconnect();
    }
    bool stop(uint64_t now){
        if(!time(now)||state_!=State::idle)return false;
        return traffic(Traffic::none)&&command(true,true,State::removingRole);
    }
    bool tick(uint64_t now){
        if(!time(now)||!expire())return false;
        if(state_==State::scanningDwell&&now>=deadline_){
            if(scanCancelled_||++channelIndex_==channelCount_)return restoreScan();
            return tuneScan();
        }
        return true;
    }
    bool actionComplete(Token original,Action kind,bool success,uint64_t now){
        if(!time(now)||!waitingAction_||!same(original,pendingToken_)||kind!=pendingAction_||!expire())return false;
        waitingAction_=false;if(!success)return fail(Error::backend);
        switch(kind){
        case Action::prepareInterface:return command(true,false,State::creatingRole);
        case Action::idleTables:state_=State::idle;return traffic(Traffic::management);
        case Action::scanBegin:return scanCancelled_?restoreScan():tuneScan();
        case Action::scanTune:
            if(scanCancelled_)return restoreScan();
            state_=State::scanningDwell;if(!arm(uint64_t(channels_[channelIndex_].dwellMs)*1000))return false;
            if(!traffic(channels_[channelIndex_].active?Traffic::scanProbe:Traffic::none))return false;
            if(channels_[channelIndex_].active&&!io_.sendProbe(scanToken_,scanResume_==State::idle?nullptr:&peer_,channels_[channelIndex_]))return fail(Error::backend);
            return true;
        case Action::scanRestore:return action(Action::scanEnd,State::scanningEnd);
        case Action::scanEnd:return completeScan();
        case Action::prepareAuthentication:
            interface_.home=peer_.channel;
            if(disconnectRequested_)return beginDisconnect();
            state_=State::authenticating;if(!arm(authenticationTimeout)||!traffic(Traffic::management))return false;
            return io_.beginAuthentication(associationToken_,peer_)||fail(Error::backend);
        case Action::associationCmac:
            if(disconnectRequested_)return beginDisconnect();
            return command(false,false,State::joining);
        case Action::associationCam:
            if(disconnectRequested_)return beginDisconnect();
            state_=State::associated;return traffic(Traffic::controlledPort);
        case Action::disconnectCmac:return command(false,true,State::disconnecting);
        case Action::disconnectCam:
            state_=State::idle;associationToken_={};association_={};peer_={};authenticated_=disconnectRequested_=false;
            return traffic(Traffic::management);
        case Action::removeCam:roleCreated_=false;state_=State::stopped;return true;
        }
        return fail(Error::backend);
    }
    // receiveEpoch identifies the RX queue incarnation captured when it was
    // initialized. It is NOT read from C2H and must not be assigned on receipt.
    bool firmwareEvent(const FirmwareEvent &event,uint64_t receiveEpoch,uint64_t now){
        if(!time(now)||!waitingCommand_||receiveEpoch!=epoch_.epoch||!expire())return false;
        network::FirmwareAck ack{};
        if(!network::decodeAck(event,ack)||!ack.done||ack.sequence!=pendingSequence_||
           !network::sameCommand(ack.command,pendingCommand_))return false;
        waitingCommand_=false;if(ack.returnCode)return fail(Error::firmwareRejected);
        switch(state_){
        case State::creatingRole:roleCreated_=true;return command(false,true,State::initialNoLink);
        case State::initialNoLink:return action(Action::idleTables,State::installingIdle);
        case State::joining:
            if(disconnectRequested_)return beginDisconnect();
            return action(Action::associationCam,State::installingAssociationCam);
        case State::disconnecting:return action(Action::disconnectCam,State::disconnectingCam);
        case State::removingRole:return action(Action::removeCam,State::removingCam);
        default:return fail(Error::backend);
        }
    }
};
} }
