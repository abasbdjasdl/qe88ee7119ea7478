// SPDX-License-Identifier: GPL-2.0-or-later
#include "MacRTL8852BE.hpp"
#include "MacProbeAndFirmware.hpp"
#include "MacRadioBoot.hpp"
#include "MacStationIo.hpp"
#include "InitialChannelPolicy.hpp"
#include "EmbeddedFirmware.hpp"
#include <IOKit/IOLib.h>
namespace rtl8852be { namespace network {
namespace {
class BootService final : public MacNetworkBootService {
    enum class Stage {idle,allocated,prepared,firmwareConfiguration,radio,ready,failed,stopped};
    IOPCIDevice *device_{};IOMemoryMap *map_{};IOWorkLoop *loop_{};
    NativeFirmwareCommands *commands_{};MacNetworkBootSink *sink_{};
    MacProbeAndFirmware firmware_;MacRadioBoot radio_;InitialChannelPolicy policy_;
    MacStationIo *stationIo_{};station::tables::NativeProgrammer *tables_{};
    Stage stage_{Stage::idle};MacBootIdentity identity_{};channel::Channel channel_{};
    station::ActionRequest action_{};MacProtocolPeer peer_{};
    stationio::PhySample phy_{};bool sampleValid_{},channelValid_{};
    bool pending_{},tableDone_{},tableSuccess_{},scanning_{},stopped_{};unsigned phase_{};
    bool edcaDone_{};uint8_t edcaSequence_{};
    uint64_t previous_{};bool clockStarted_{};
    uint16_t basicRates_=0x000f;
    bool inGate()const{return loop_&&loop_->inGate();}
    bool fail(const char *reason){
        if(stage_!=Stage::failed){stage_=Stage::failed;pending_=false;channelValid_=sampleValid_=false;
            if(commands_)commands_->invalidate();if(sink_)sink_->bootFailed(reason);}
        return false;
    }
    static bool paused(void *p){return static_cast<BootService*>(p)->radio_.schedulerPaused();}
    static bool window(void *p){auto &s=*static_cast<BootService*>(p);return s.inGate()&&s.pending_&&s.radio_.stationWindowOwned();}
    static bool tableCompletion(void *p,station::Token token,bool ok){
        auto &s=*static_cast<BootService*>(p);
        if(!s.inGate()||!s.pending_||!station::same(token,s.action_.token)||s.tableDone_)return false;
        s.tableDone_=true;s.tableSuccess_=ok;return true;
    }
    static bool edcaCompletion(void *p,const FirmwareEvent &event,uint64_t epoch,uint64_t operation){
        auto &s=*static_cast<BootService*>(p);FirmwareAck ack{};
        if(!s.inGate()||!s.pending_||s.action_.action!=station::Action::associationCmac||s.edcaDone_||
           epoch!=s.action_.token.epoch||operation!=s.action_.token.operation||!decodeAck(event,ack)||
           !ack.done||ack.sequence!=s.edcaSequence_||!sameCommand(ack.command,{1,9,0x0f})||ack.returnCode)return false;
        s.edcaDone_=true;return true;
    }
    station::tables::Cam cam(bool connected,bool valid=true)const{
        station::tables::Cam c{};c.macid=identity_.interface.macid;c.port=identity_.interface.port;
        c.local=identity_.interface.address;c.bssid=action_.peer.bssid;c.connected=connected;c.valid=valid;
        c.aid=connected?action_.aid:0;return c;
    }
    bool submit(const station::tables::Plan &p){
        tableDone_=tableSuccess_=false;
        return tables_&&tables_->begin(p,action_.token,{this,tableCompletion});
    }
    bool submitCam(bool connected,bool valid=true){station::tables::Plan p{};
        return station::tables::camPlan(cam(connected,valid),p)&&submit(p);}
    bool tableFinished(){return tableDone_&&tableSuccess_;}
    bool finish(){
        if(!pending_||!sink_)return false;
        const auto request=action_;pending_=false;phase_=0;
        sink_->stationActionComplete(request.token,request.action,true);return true;
    }
    bool launchTune(bool scanning){
        if(!policy_.allows(action_.channel))return false;
        sampleValid_=channelValid_=false;
        return radio_.tune(action_.channel,policy_.powerPolicy(),scanning);
    }
    bool finishTune(){
        if(radio_.busy())return false;
        if(!radio_.ready()||!radio_.tuned())return false;
        channel_=action_.channel;channelValid_=true;return true;
    }
    bool serviceAction(){
        using A=station::Action;
        if(!pending_)return true;
        if(tableDone_&&!tableSuccess_)return fail("station table firmware rejected");
        if(stationIo_->faulted())return fail("station register operation failed");
        switch(action_.action){
        case A::prepareInterface:
            if(phase_==0){
                if(!stationIo_->seed(identity_.interface.macid).complete||
                   !stationIo_->beginPort({identity_.interface.port,false,0,0}))return fail("initial MAC port/table setup failed");
                phase_=1;
            }
            if(phase_==1){const auto p=stationIo_->servicePort();if(p==MacPortState::fault)return fail("initial MAC port failed");
                if(p!=MacPortState::complete)return true;
                if(!launchTune(false))return fail("initial home channel failed");phase_=2;return true;}
            if(radio_.busy())return true;
            if(!finishTune())return fail("initial RF calibration incomplete");return finish();
        case A::idleTables:
            if(phase_==0){station::tables::Plan p{};station::tables::DefaultCmac c{};
                c.macid=identity_.interface.macid;c.antennaTx=firmware_.capabilities()->antennaTx;
                if(!station::tables::idlePlan(cam(false),c,p)||!submit(p))return fail("idle table submission failed");phase_=1;return true;}
            return tableFinished()?finish():true;
        case A::scanBegin:
            if(phase_==0){if(scanning_||!stationIo_->scanFilter(true)||!radio_.beginScan())return fail("scan preparation failed");phase_=1;return true;}
            if(radio_.busy())return true;if(!radio_.ready())return fail("scan calibration failed");scanning_=true;return finish();
        case A::scanTune:
        case A::scanRestore:
            if(phase_==0){if(!scanning_||!launchTune(true))return fail("scan channel rejected");phase_=1;return true;}
            if(radio_.busy())return true;if(!finishTune())return fail("scan channel calibration failed");return finish();
        case A::scanEnd:
            if(phase_==0){if(!scanning_||!radio_.endScan())return fail("scan restoration failed");phase_=1;return true;}
            if(radio_.busy())return true;if(!radio_.ready()||!radio_.tuned())return fail("home restoration incomplete");
            scanning_=false;if(!stationIo_->scanFilter(false))return fail("scan RX policy restoration failed");return finish();
        case A::prepareAuthentication:
            if(phase_==0){if(!peer_.valid||!peer_.basicRates||!launchTune(false))return fail("AP channel/capabilities invalid");phase_=1;return true;}
            if(phase_==1){if(radio_.busy())return true;if(!finishTune())return fail("AP RF calibration incomplete");
                basicRates_=peer_.basicRates;
                if(!submitCam(false))return fail("authentication CAM submission failed");phase_=2;return true;}
            return tableFinished()?finish():true;
        case A::associationCmac:
            if(phase_==0){if(!peer_.valid||!peer_.beaconInterval||
                !stationIo_->beginPort({identity_.interface.port,true,peer_.beaconInterval,peer_.dtimPeriod}))return fail("associated port parameters invalid");phase_=1;}
            if(phase_==1){const auto p=stationIo_->servicePort();if(p==MacPortState::fault)return fail("associated port programming failed");
                if(p!=MacPortState::complete)return true;
                uint8_t payload[12]{};
                if(!stationio::edcaPayload(channel_,peer_,0,payload)||
                   !commands_->submit({1,9,0x0f},false,true,payload,sizeof(payload),{this,edcaCompletion},action_.token.operation,500000,edcaSequence_))return fail("EDCA configuration submission failed");
                phase_=2;return true;}
            if(phase_==2){if(!edcaDone_)return true;
                station::tables::AssociationCmac c{};c.macid=identity_.interface.macid;c.port=identity_.interface.port;c.band=channel_.band;c.stationPresent=true;
                station::tables::Plan plan{};if(!station::tables::cmacPlan(c,plan)||!submit(plan))return fail("associated CMAC submission failed");phase_=3;return true;}
            return tableFinished()?finish():true;
        case A::associationCam:
        case A::disconnectCam:
        case A::removeCam:
            if(phase_==0){if(!submitCam(action_.action==A::associationCam,action_.action!=A::removeCam))return fail("CAM update submission failed");phase_=1;return true;}
            return tableFinished()?finish():true;
        case A::disconnectCmac:
            if(phase_==0){if(!stationIo_->beginPort({identity_.interface.port,false,0,0}))return fail("disconnected port setup failed");phase_=1;}
            if(phase_==1){const auto p=stationIo_->servicePort();if(p==MacPortState::fault)return fail("disconnected port failed");if(p!=MacPortState::complete)return true;
                station::tables::AssociationCmac c{};c.macid=identity_.interface.macid;c.port=identity_.interface.port;c.band=channel_.band;
                station::tables::Plan plan{};if(!station::tables::cmacPlan(c,plan)||!submit(plan))return fail("disconnected CMAC submission failed");phase_=2;return true;}
            return tableFinished()?finish():true;
        }
        return fail("unknown station action");
    }
public:
    ~BootService()override{
        // Controller calls stop while commands still exist, then deletes us only
        // after DMA/IRQ shutdown succeeded and outside the command gate.
        delete stationIo_;delete tables_;radio_.release();firmware_.release();
    }
    bool allocate(IOPCIDevice &d,IOMemoryMap &m,IOWorkLoop &w)override{
        if(stage_!=Stage::idle||w.inGate())return false;device_=&d;map_=&m;loop_=&w;
        if(!firmware_.allocate(d,m,w,image::bytes,image::length))return fail("firmware DMA allocation/identity failed");
        stage_=Stage::allocated;return true;
    }
    bool prepare(MacBootIdentity &out)override{
        if(!inGate()||stage_!=Stage::allocated||!firmware_.prepare())return fail("power/firmware/MAC startup failed");
        const auto *c=firmware_.calibration();const auto *caps=firmware_.capabilities();
        if(!c||!caps||!policy_.initialize(firmware_.epoch()))return fail("calibration identity missing");
        identity_.epoch=firmware_.epoch();identity_.interface.home={0,0,1,1};
        for(unsigned i=0;i<6;++i)identity_.interface.address.bytes[i]=c->board.mac[i];
        for(uint8_t n=1;n<=11;++n)identity_.channels[identity_.channelCount++]={n,false,false};
        out=identity_;stage_=Stage::prepared;return true;
    }
    bool start(NativeFirmwareCommands &commands,MacNetworkBootSink &sink)override{
        if(!inGate()||stage_!=Stage::prepared||commands.epoch()!=identity_.epoch)return false;
        commands_=&commands;sink_=&sink;
        tables_=new station::tables::NativeProgrammer(commands);
        stationIo_=new MacStationIo(*device_,*map_,*loop_,{this,paused,window});
        if(!tables_||!stationIo_||!firmware_.beginFirmwareConfiguration(commands))return fail("firmware runtime configuration failed");
        stage_=Stage::firmwareConfiguration;return true;
    }
    bool service(uint64_t now)override{
        if(!inGate()||stage_==Stage::failed||stage_==Stage::stopped)return false;
        if(clockStarted_&&now<previous_)return fail("controller clock reversed");previous_=now;clockStarted_=true;
        if(!commands_||!commands_->service())return fail("firmware command service failed");
        if(stage_==Stage::firmwareConfiguration){
            if(firmware_.result().error!=ProbeFirmwareError::none)return fail("firmware configuration rejected");
            if(firmware_.result().offloadConfigurationPending)return true;
            if(!radio_.allocate(*device_,*map_,*loop_,*firmware_.calibration(),*firmware_.capabilities(),
                *firmware_.firmwarePlan(),*commands_,*firmware_.mailbox())||!radio_.begin())return fail("radio initialization failed");
            // begin() samples a newer hardware clock and can execute BB/RF
            // setup. The pre-begin timestamp must not be replayed into service.
            stage_=Stage::radio;return true;
        }
        if(stage_==Stage::radio||stage_==Stage::ready){
            if(!radio_.service(now))return fail("radio/BT calibration failed");
            if(stage_==Stage::radio&&radio_.ready())stage_=Stage::ready;
            if(stage_==Stage::ready&&!serviceAction())return false;
        }
        return true;
    }
    bool ready()const override{return stage_==Stage::ready;}
    bool beginAction(const station::ActionRequest &request,const MacProtocolPeer &peer)override{
        if(!inGate()||!ready()||pending_||!request.token.operation||request.token.epoch!=identity_.epoch||
           request.interface.macid!=identity_.interface.macid||request.interface.port!=identity_.interface.port||
           !station::same(request.interface.address,identity_.interface.address))return false;
        if(!radio_.setTraffic(station::Traffic::none))return false;
        action_=request;peer_=peer;pending_=true;phase_=0;tableDone_=tableSuccess_=edcaDone_=false;return true;
    }
    bool setTraffic(station::Traffic traffic)override{
        if(!inGate()||!ready()||radio_.busy())return false;
        if(traffic!=station::Traffic::none&&(!channelValid_||!policy_.allows(channel_)))return false;
        return radio_.setTraffic(traffic);
    }
    bool txInfo(const TxLease &lease,TxInfo &info,unsigned &ring)override{
        return inGate()&&ready()&&channelValid_&&policy_.allows(channel_)&&
            stationIo_->txInfo(lease,identity_.interface.macid,channel_,basicRates_,info,ring);
    }
    bool rxInfo(uint8_t &channel,int &rssi)override{
        if(!inGate()||!ready()||!channelValid_||!sampleValid_||radio_.busy())return false;
        channel=channel_.primary;rssi=phy_.normalized;return true;
    }
    int phyReport(const RxPacket &packet)override{
        if(!inGate()||!ready()||!channelValid_||radio_.busy())return 0;
        stationio::PhySample sample{};if(!MacStationIo::phyReport(packet,sample))return 0;
        if(sample.channelKnown&&(sample.channel!=channel_.primary||sample.band!=channel_.band))return 0;
        phy_=sample;sampleValid_=true;return 0;
    }
    int notification(const FirmwareEvent &)override{return 0;} // Unsubscribed C2H is not completion evidence.
    bool stop()override{
        if(stopped_)return true;if(!loop_)return true;if(!inGate())return false;
        pending_=false;sink_=nullptr;channelValid_=sampleValid_=false;policy_.invalidate();
        if(commands_)commands_->invalidate();const bool radioStopped=radio_.stop();
        const bool powerStopped=firmware_.stop();const bool ok=radioStopped&&powerStopped;
        if(ok){stopped_=true;stage_=Stage::stopped;}return ok;
    }
};
}
} }
OSDefineMetaClassAndStructors(R16RTL8852BE,R16NetworkController)
rtl8852be::network::MacNetworkBootService *R16RTL8852BE::createBootService(){return new rtl8852be::network::BootService;}
