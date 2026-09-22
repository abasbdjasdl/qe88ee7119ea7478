// SPDX-License-Identifier: GPL-2.0-or-later
#include "MacNetworkController.hpp"
#include "Net80211Runtime.hpp"
#include "MacPciRingIo.hpp"
#include "MacNetworkPhyWait.hpp"
#include "RxTrace.hpp"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/_if_ether.h>
#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ioctl.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IONetworkMedium.h>
#include <IOKit/network/IONetworkData.h>
#include <IOKit/network/IOOutputQueue.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSData.h>
#include <IOKit/IOLib.h>
using namespace rtl8852be;
using namespace rtl8852be::network;
OSDefineMetaClassAndStructors(R16NetworkController,IOEthernetController)
struct R16NetworkController::State final : MacNetworkBootSink {
    R16NetworkController &owner; MacNetworkBootService *boot{};
    MacBootIdentity identity{}; ieee80211com ic{}; Net80211Runtime protocol;
    MacPciRuntimeIo runtimeIo; MacPciRingIo ringIo;
    R16PciInterrupts::Runtime runtime; R16PciInterrupts *interrupt{};
    MacTxDmaQueue tx[6]; MacTxDmaQueue *txPointers[6]{};
    MacFirmwareDmaQueue firmware; RxDmaQueue<MacDmaBuffer> rxq,rpq;
    NativeQueueService *queues{}; MacCommandTransport *transport{};
    NativeFirmwareCommands *commands{}; MacFirmwareEventBinding *events{};
    Net80211PciQueue *completion[13]{}; PciRxAssembly rxAssembly,rpAssembly;MacNetworkPhyWait phyWait;
    station::Controller<State> station;
    station::Traffic traffic{station::Traffic::none};station::Token auth{};
    int (*savedState)(ieee80211com*,enum ieee80211_state,int){};
    bool bound{},attached{},visible{},runtimeAttempted{},prepared{},bootStarted{},stationStarted{},interruptAttached{},credentials{};
    bool enabled{},faulted{},stopping{},scanDone{},authPending{},probePending{},runPending{},resetPending{};
    bool stateDeferred{};int deferredState{},deferredArgument{};
    selection::Pending pendingSelection;
    uint64_t stateRequests[5]{},runCommitted{},portAuthorizations{},rxBridgeOk{},rxBridgeError{},txPrepareErrors{};
    uint64_t lastStateRequest{},lastRxBridgeError{},lastTxPrepareError{};
    uint64_t rxTypes[4]{},rxHardwareCrypto{},rxSoftwareFallback{},rxEapol{},rxEapolGated{},rxEapolBridge{},lastDeauthReason{};
    uint64_t eapolBridgeOk{},eapolBridgeFailed{},eapolStage{},eapolError{},eapolLengths{},eapolHeader{},eapolEnvelope{},eapolDropMask{};
    void deliver(const uint8_t *data,size_t length,uint8_t channel,int rssi){
        receivingProtocol=true;++rxDeliveryAttempts;
        RxPacket packet;
        RxTrace metadata;
        if(decodeRx(data,length,4,packet)==DescriptorStatus::ok)metadata=inspectRx(packet,identity.interface.address.bytes);
        const auto before=ic.ic_stats;RxDeliveryTrace trace;
        if(metadata.eapol){++rxEapolBridge;
            eapolHeader=uint64_t(packet.payload[0])|(uint64_t(packet.payload[1])<<8)|(uint64_t(packet.payload[22]&15)<<16)|(uint64_t(unsigned(ic.ic_state))<<24);
            eapolEnvelope=uint64_t(metadata.eapolType)|(uint64_t(metadata.bodyLength)<<16)|(uint64_t(packet.length)<<32);}
        const int error=deliverRealtekRx(&ic,data,length,4,channel,rssi,metadata.eapol?&trace:nullptr);
        if(metadata.eapol){
            if(error)++eapolBridgeFailed;else ++eapolBridgeOk;
            eapolStage=trace.stage;eapolError=unsigned(error);eapolLengths=uint64_t(trace.packetLength)|(uint64_t(trace.firstLength)<<32);
            const auto &after=ic.ic_stats;
            eapolDropMask=uint64_t(after.is_rx_tooshort!=before.is_rx_tooshort)|
                (uint64_t(after.is_rx_wrongdir!=before.is_rx_wrongdir)<<1)|
                (uint64_t(after.is_rx_wrongbss!=before.is_rx_wrongbss)<<2)|
                (uint64_t(after.is_rx_dup!=before.is_rx_dup)<<3)|
                (uint64_t(after.is_rx_nowep!=before.is_rx_nowep)<<4)|
                (uint64_t(after.is_rx_unencrypted!=before.is_rx_unencrypted)<<5)|
                (uint64_t(after.is_rx_decap!=before.is_rx_decap)<<6)|
                (uint64_t(after.is_rx_unauth!=before.is_rx_unauth)<<7)|
                (uint64_t(after.is_rx_eapol_key!=before.is_rx_eapol_key)<<8)|
                (uint64_t(after.is_rx_nombuf!=before.is_rx_nombuf)<<9);
        }
        receivingProtocol=false;
        if(error){++rxBridgeError;lastRxBridgeError=unsigned(error);}else ++rxBridgeOk;
    }
    bool outputPumping{},receivingProtocol{},actionInFlight{},actionDeferred{};TxLease pendingTx{};
    station::ActionRequest activeAction{};MacProtocolPeer activePeer{};uint64_t lastWatchdog{};
    struct ActionCompletion {station::Token token;station::Action action;bool success;};
    ActionCompletion completed[16]{};unsigned completedCount{};
    State(R16NetworkController &o,MacNetworkBootService *b):owner(o),boot(b),
        runtimeIo(o.pci_,o.bar_),ringIo(o.pci_,o.bar_),runtime(runtimeIo),station(*this){
        for(unsigned i=0;i<6;++i)txPointers[i]=&tx[i];
    }
    ~State(){delete events;delete boot;delete commands;delete transport;delete queues;}
    uint64_t now(){return runtimeIo.nowUs();}
    bool inGate(){return owner.loop_->inGate();}
    void fail(const char *reason){
        if(faulted)return;faulted=true;traffic=station::Traffic::none;enabled=false;
        pendingSelection.clear();
        ic.ic_if.if_flags&=~IFF_RUNNING;
        owner.IOEthernetController::setLinkStatus(kIONetworkLinkValid);
        publishStatus();owner.setProperty("R16Failure",reason);IOLog("RTL8852BE network: %s\n",reason);
        // No reclamation from a receive/ACK callback. stop() later proves idle.
        if(interrupt)interrupt->stop();else if(runtimeAttempted)runtime.stop();
    }
    void bootFailed(const char *reason)override{fail(reason?reason:"boot backend failed");}
    void stationActionComplete(station::Token t,station::Action a,bool ok)override{
        if(!inGate()||stopping||completedCount==16||!actionInFlight||actionDeferred||
           !station::same(t,activeAction.token)||a!=activeAction.action){fail("invalid station completion context");return;}
        completed[completedCount++]={t,a,ok};
    }
    bool setTraffic(station::Traffic t){
        if(faulted||stopping)return false;
        if(t==station::Traffic::none){
            // Stop host admission first; published frames retain the OLD channel
            // and scheduler until their real TXBD + RPQ completions arrive.
            traffic=t;phyWait.clear();if(pendingTx.frame)releaseTx(&ic,pendingTx);
            owner.IOEthernetController::setLinkStatus(kIONetworkLinkValid);
            if(actionInFlight||!dataDrained())return true;
            return boot->setTraffic(t);
        }
        if(actionInFlight||!boot->setTraffic(t))return false;
        if(t!=station::Traffic::authorized&&pendingTx.frame)releaseTx(&ic,pendingTx);
        traffic=t;
        if(t!=station::Traffic::authorized)owner.IOEthernetController::setLinkStatus(kIONetworkLinkValid);
        return true;
    }
    static bool stationAck(void *p,const FirmwareEvent &event,uint64_t epoch,uint64_t){
        auto &s=*static_cast<State*>(p);
        return !s.stopping&&!s.faulted&&s.station.firmwareEvent(event,epoch,s.now());
    }
    bool reserveH2cSequence(uint8_t &sequence){
        return commands&&commands->reserve({this,stationAck},0,2000000,sequence);
    }
    bool publishH2c(const uint8_t *p,size_t n,station::Token){return commands&&commands->publish(p,n);}
    bool beginAction(const station::ActionRequest &r){
        MacProtocolPeer peer{};
        auto mode=ic.ic_curmode;
        if(mode!=IEEE80211_MODE_11A&&mode!=IEEE80211_MODE_11B&&mode!=IEEE80211_MODE_11G)
            mode=identity.interface.home.band?IEEE80211_MODE_11A:IEEE80211_MODE_11G;
        const auto &edca=ieee80211_edca_table[mode][EDCA_AC_BE];
        peer.aifsn=edca.ac_aifsn;peer.ecwMin=edca.ac_ecwmin;peer.ecwMax=edca.ac_ecwmax;peer.txop=edca.ac_txoplimit;
        if(ic.ic_bss&&station::unicast(bssid())){
            peer.valid=true;peer.beaconInterval=ic.ic_bss->ni_intval;
            // Match pinned net80211 initial DTIM policy when a probe has no TIM.
            // A nonzero selected-node period always takes precedence.
            peer.dtimPeriod=ic.ic_bss->ni_dtimperiod?ic.ic_bss->ni_dtimperiod:1;
            peer.shortSlot=(ic.ic_flags&IEEE80211_F_SHSLOT)!=0;
            peer.shortPreamble=(ic.ic_flags&IEEE80211_F_SHPREAMBLE)!=0;
            peer.useProtection=(ic.ic_flags&IEEE80211_F_USEPROT)!=0;
            const auto &rates=ic.ic_bss->ni_rates;
            for(unsigned i=0;i<rates.rs_nrates;++i)if(rates.rs_rates[i]&IEEE80211_RATE_BASIC){
                const int rate=stationio::hardwareRate(rates.rs_rates[i]&IEEE80211_RATE_VAL);
                if(rate>=0)peer.basicRates|=uint16_t(1u<<rate);
            }
        }
        if(faulted||stopping||actionInFlight)return false;
        traffic=station::Traffic::none;phyWait.clear();if(pendingTx.frame)releaseTx(&ic,pendingTx);
        activeAction=r;activePeer=peer;actionInFlight=actionDeferred=true;return true;
    }
    bool beginAuthentication(station::Token t,const station::Peer&){auth=t;authPending=true;return true;}
    bool sendProbe(station::Token,const station::Peer*,const station::ScanChannel&){probePending=true;return true;}
    bool cancelProtocol(station::Token){authPending=probePending=runPending=false;resetPending=true;return true;}
    void scanFinished(station::Token,bool cancelled){if(!cancelled)scanDone=true;}
    void recoveryRequired(station::Token,station::Error){fail("station operation failed; physical firmware reset required");}
    bool firmwareRestartVerified(uint64_t epoch){return prepared&&identity.epoch==epoch&&commands&&commands->epoch()==epoch;}
    static State *from(ieee80211com *ic){return static_cast<State*>(ic->ic_if.if_softc);}
    static void output(_ifnet *ifp){auto *s=static_cast<State*>(ifp->if_softc);if(s)s->pumpTx();}
    static int ioctl(_ifnet *ifp,u_long command,caddr_t data){return ieee80211_ioctl(ifp,command,data);}
    static channel::Channel channelOf(ieee80211com *ic,const ieee80211_channel *c){
        const auto n=ieee80211_chan2ieee(ic,c);
        if(n>255||c==IEEE80211_CHAN_ANYC)return {};
        return {uint8_t(IEEE80211_IS_CHAN_5GHZ(c)?1:0),0,uint8_t(n),uint8_t(n)};
    }
    station::Address bssid(){station::Address a;memcpy(a.bytes,ic.ic_bss->ni_bssid,6);return a;}
    static int newState(ieee80211com *ic,enum ieee80211_state next,int arg){
        auto &s=*from(ic);if(s.stopping||s.faulted)return ENETDOWN;
        if(!s.inGate()){s.fail("net80211 state outside gate");return EIO;}
        if(unsigned(next)<5)++s.stateRequests[unsigned(next)];
        // State/argument only, no frame contents, network names or key material.
        s.lastStateRequest=(uint64_t(unsigned(ic->ic_state))<<40)|(uint64_t(unsigned(next))<<32)|uint32_t(arg);
        if(next==IEEE80211_S_INIT){
            s.stateDeferred=true;s.deferredState=next;s.deferredArgument=arg;
            if(s.stationStarted)s.station.disconnect(s.now());return 0;
        }
        if(!s.enabled||!s.stationStarted)return ENETDOWN;
        if(s.pendingSelection.waiting())return EBUSY;
        if(next==IEEE80211_S_SCAN){
            if(s.station.state()!=station::State::idle){
                s.stateDeferred=true;s.deferredState=next;s.deferredArgument=arg;
                if(!s.station.disconnect(s.now()))return EIO;return 0;
            }
            if(ic->ic_state!=IEEE80211_S_SCAN)return s.savedState(ic,next,arg);
            station::ScanChannel channel{};channel.channel=channelOf(ic,ic->ic_bss->ni_chan);
            channel.dwellMs=120;channel.active=(ic->ic_flags&IEEE80211_F_ASCAN)&&
                !(ic->ic_bss->ni_chan->ic_flags&IEEE80211_CHAN_PASSIVE);
            return s.station.scan(&channel,1,s.now())?0:EIO;
        }
        if(next==IEEE80211_S_AUTH){
            if(s.station.state()!=station::State::idle){
                s.stateDeferred=true;s.deferredState=IEEE80211_S_SCAN;s.deferredArgument=-1;
                s.station.disconnect(s.now());return 0;
            }
            station::Peer peer{};peer.bssid=s.bssid();peer.channel=channelOf(ic,ic->ic_bss->ni_chan);
            peer.ssidLength=ic->ic_bss->ni_esslen;if(peer.ssidLength>32)return EINVAL;
            memcpy(peer.ssid,ic->ic_bss->ni_essid,peer.ssidLength);
            peer.protectedNetwork=(ic->ic_flags&IEEE80211_F_RSNON)!=0;
            return s.station.connect(peer,s.now())?0:EIO;
        }
        if(next==IEEE80211_S_ASSOC){
            // This hook is entered by net80211 only after a valid AP auth response.
            if(ic->ic_state==IEEE80211_S_RUN){
                s.stateDeferred=true;s.deferredState=IEEE80211_S_SCAN;s.deferredArgument=-1;
                return s.station.disconnect(s.now())?0:EIO;
            }
            if(!s.receivingProtocol||arg!=IEEE80211_FC0_SUBTYPE_AUTH||ic->ic_state!=IEEE80211_S_AUTH||
               !s.station.authenticated(s.auth,s.bssid(),true,s.now()))return EIO;
            return s.savedState(ic,next,arg);
        }
        if(next==IEEE80211_S_RUN){
            if(!s.receivingProtocol||arg!=IEEE80211_FC0_SUBTYPE_ASSOC_RESP||ic->ic_state!=IEEE80211_S_ASSOC)return EINVAL;
            station::Association a{s.bssid(),channelOf(ic,ic->ic_bss->ni_chan),uint16_t(ic->ic_bss->ni_associd&0x3fff)};
            if(!s.station.associationReceived(s.auth,a,s.now()))return EIO;
            s.runPending=true;return 0; // Defer protocol RUN until real CAM/Join DONE.
        }
        return EOPNOTSUPP;
    }
    void pumpTx(){
        if(outputPumping||stopping||faulted||actionInFlight||!enabled||!runtime.running()||traffic==station::Traffic::none)return;
        outputPumping=true;
        for(unsigned budget=0;budget<32;++budget){
            if(!pendingTx.frame){
                // Preserve EAPOL in RUN; the protocol itself enforces port validity.
                const int error=prepareNextTx(&ic,pendingTx);
                if(error==EAGAIN)break;
                if(error){++txPrepareErrors;lastTxPrepareError=unsigned(error);continue;}
            }
            TxInfo info{};unsigned ring=9;
            if(!boot->txInfo(pendingTx,info,ring)||ring>=6){releaseTx(&ic,pendingTx);fail("TX descriptor policy unavailable");break;}
            if(tx[ring].completions().full())break;
            const int error=submitNativeData(runtime,tx[ring],ring,pendingTx,info);
            if(error){if(pendingTx.frame)releaseTx(&ic,pendingTx);fail("native TX publication failed");break;}
            ++txSubmitted;
        }
        outputPumping=false;
    }
    static int notification(void *p,const FirmwareEvent &event){return static_cast<State*>(p)->boot->notification(event);}
    static bool receiveData(void *p,const uint8_t *data,size_t length){
        auto &s=*static_cast<State*>(p);if(s.faulted||s.stopping)return false;
        AssembledRx frame;const auto status=s.rxAssembly.feed(data,length,frame);
        if(status!=AssemblyStatus::complete)return true;
        ++s.rxComplete;
        if(frame.packet.info.pkt_type==0){
            const auto trace=inspectRx(frame.packet,s.identity.interface.address.bytes);
            ++s.rxTypes[trace.type];
            if(hardwareDecrypted(frame.packet.info))++s.rxHardwareCrypto;
            if(frame.packet.info.hw_dec&&frame.packet.info.sw_dec)++s.rxSoftwareFallback;
            if(trace.deauth)s.lastDeauthReason=trace.deauthReason;
            if(trace.eapol){++s.rxEapol;
                if(!s.attached||!s.owner.interface_||!s.enabled||s.actionInFlight)++s.rxEapolGated;}
        }
        if(frame.packet.info.pkt_type==0){++s.rxWireless;if(!s.attached||!s.owner.interface_||!s.enabled||s.actionInFlight)++s.rxGated;}
        if(frame.packet.info.pkt_type==1)++s.rxPhy;
        if(frame.packet.info.pkt_type==10){
            FirmwareEvent event;if(!decodeC2h(frame.packet.payload,frame.packet.length,event))return true;
            return MacFirmwareEventBinding::receive(s.events,event)==0;
        }
        if(frame.packet.info.pkt_type==1){
            stationio::PhySample report{};const bool validReport=stationio::phySample(frame.packet,report);
            s.boot->phyReport(frame.packet);
            if(validReport&&s.attached&&s.owner.interface_&&s.enabled&&!s.actionInFlight){
                uint8_t channel=0;int rssi=0;
                if(s.boot->rxInfo(channel,rssi)&&(!report.channelKnown||report.channel==channel)){
                    const auto *pending=s.phyWait.take(frame.packet.info.ppdu_cnt,frame.packet.info.data_rate,s.now());
                    if(pending)s.deliver(pending->bytes,pending->length,channel,rssi);
                }
            }
            return !s.faulted;
        }
        if(frame.packet.info.pkt_type==0&&s.attached&&s.owner.interface_&&s.enabled&&!s.actionInFlight){
            uint8_t channel=0;int rssi=0;
            if(s.boot->rxInfo(channel,rssi)){
                s.deliver(frame.data,frame.bytes,channel,rssi);
            }else s.phyWait.store(frame.packet.info.ppdu_cnt,frame.packet.info.data_rate,frame.data,frame.bytes,s.now());
        }
        return !s.faulted;
    }
    static bool receiveReports(void *p,const uint8_t *data,size_t length){
        auto &s=*static_cast<State*>(p);return receiveRpq(s.rpAssembly,data,length,s.completion)==0;
    }
    static R16PciInterrupts::ServiceResult interruptService(void *p,const InterruptStatus &causes){
        auto &s=*static_cast<State*>(p);auto result=serviceNativeQueues(s.queues,causes);
        if(result!=R16PciInterrupts::ServiceResult::fault)s.pumpTx();return result;
    }
    static void interruptFault(void *p,const InterruptStatus&){static_cast<State*>(p)->fail("PCI interrupt or DMA service fault");}
    bool attachProtocol(){
        if(protocol.bind(owner.loop_,owner.gate_)!=kIOReturnSuccess)return false;bound=true;
        if(!station::unicast(identity.interface.address)||!identity.epoch||!identity.channelCount||identity.channelCount>64)return false;
        auto &ifp=ic.ic_if;ifp.if_softc=this;ifp.if_flags=IFF_BROADCAST|IFF_SIMPLEX|IFF_MULTICAST;
        ifp.if_start=output;ifp.if_ioctl=ioctl;strlcpy(ifp.if_xname,"r16wifi",sizeof(ifp.if_xname));
        memcpy(ic.ic_myaddr,identity.interface.address.bytes,6);
        ic.ic_opmode=IEEE80211_M_STA;ic.ic_phytype=IEEE80211_T_OFDM;
        ic.ic_caps=IEEE80211_C_RSN; // Legacy 20 MHz, software crypto, no aggregation/QoS offload.
        ic.ic_sup_rates[IEEE80211_MODE_11B]=ieee80211_std_rateset_11b;
        ic.ic_sup_rates[IEEE80211_MODE_11G]=ieee80211_std_rateset_11g;
        ic.ic_sup_rates[IEEE80211_MODE_11A]=ieee80211_std_rateset_11a;
        for(size_t i=0;i<identity.channelCount;++i){const auto &c=identity.channels[i];
            if(!c.number||ic.ic_channels[c.number].ic_flags||!channel::validChannel({uint8_t(c.fiveGhz),0,c.number,c.number}))return false;
            auto &out=ic.ic_channels[c.number];out.ic_flags=c.fiveGhz?IEEE80211_CHAN_A:IEEE80211_CHAN_G;
            if(c.passive)out.ic_flags|=IEEE80211_CHAN_PASSIVE;
            out.ic_freq=ieee80211_ieee2mhz(c.number,out.ic_flags);
        }
        if(!ic.ic_channels[identity.interface.home.primary].ic_flags)return false;
        ic.ic_ibss_chan=&ic.ic_channels[identity.interface.home.primary];ic.ic_max_rssi=100;
        if_attach(&ifp);ieee80211_ifattach(&ifp,&owner);attached=true;
        savedState=ic.ic_newstate;ic.ic_newstate=newState;ieee80211_media_init(&ifp);
        return savedState&&ic.ic_bss&&ifp.if_snd.queue;
    }
    bool loadCredentials(){
        // Pre-derived PSK only: no passphrase PBKDF2 under a kernel command gate.
        auto *ssid=OSDynamicCast(OSData,owner.getProperty("R16SSID"));
        if(!ssid)return true;
        if(!ssid->getLength()||ssid->getLength()>32)return false;
        ieee80211_join join{};join.i_len=ssid->getLength();memcpy(join.i_nwid,ssid->getBytesNoCopy(),join.i_len);
        auto *psk=OSDynamicCast(OSData,owner.getProperty("R16PSK"));
        if(psk){
            if(psk->getLength()!=32)return false;
            join.i_flags=IEEE80211_JOIN_WPAPSK|IEEE80211_JOIN_WPA;
            join.i_wpaparams.i_enabled=1;join.i_wpaparams.i_protos=IEEE80211_WPA_PROTO_WPA2;
            join.i_wpaparams.i_akms=IEEE80211_WPA_AKM_PSK;
            join.i_wpaparams.i_ciphers=IEEE80211_WPA_CIPHER_CCMP;
            join.i_wpaparams.i_groupcipher=IEEE80211_WPA_CIPHER_CCMP;
            join.i_wpapsk.i_enabled=1;memcpy(join.i_wpapsk.i_psk,psk->getBytesNoCopy(),32);
        }else{join.i_flags=IEEE80211_JOIN_NWKEY;join.i_nwkey.i_wepon=IEEE80211_NWKEY_OPEN;}
        if(ieee80211_add_ess(&ic,&join))return false;ic.ic_flags|=IEEE80211_F_AUTO_JOIN;credentials=true;return true;
    }
    IOReturn queueSelection(const selection::Join *requested){
        if(!attached||!stationStarted||faulted||stopping||!enabled)return kIOReturnNotReady;
        if(requested){
            if(!selection::valid(*requested))return kIOReturnBadArgument;
            if(!pendingSelection.submit(*requested))return kIOReturnBusy;
        }else pendingSelection.clear();
        // Keep the old protocol keys until all old-channel TX and hardware work
        // has drained. Never install the new credentials from this call stack.
        if(!station.disconnect(now())){pendingSelection.clear();return kIOReturnBusy;}
        credentials=false;ic.ic_flags&=~IEEE80211_F_AUTO_JOIN;
        stateDeferred=true;deferredState=IEEE80211_S_INIT;deferredArgument=-1;
        authPending=probePending=runPending=scanDone=false;
        owner.IOEthernetController::setLinkStatus(kIONetworkLinkValid);
        return kIOReturnSuccess;
    }
    bool applyPendingSelection(){
        selection::Join selected{};
        if(!pendingSelection.take(station.state()==station::State::idle&&ic.ic_state==IEEE80211_S_INIT,
                                  dataDrained(),actionInFlight||stateDeferred||resetPending,selected))return true;
        ieee80211_del_ess(&ic,nullptr,0,1);ieee80211_deselect_ess(&ic);
        ieee80211_disable_rsn(&ic);ieee80211_disable_wep(&ic);
        selection::wipe(ic.ic_psk,sizeof(ic.ic_psk));
        ieee80211_join join{};join.i_len=selected.ssidLength;
        memcpy(join.i_nwid,selected.ssid,selected.ssidLength);
        if(selected.security==selection::Security::wpa2Psk){
            join.i_flags=IEEE80211_JOIN_WPAPSK|IEEE80211_JOIN_WPA;
            join.i_wpaparams.i_enabled=1;join.i_wpaparams.i_protos=IEEE80211_WPA_PROTO_WPA2;
            join.i_wpaparams.i_akms=IEEE80211_WPA_AKM_PSK;
            join.i_wpaparams.i_ciphers=IEEE80211_WPA_CIPHER_CCMP;
            join.i_wpaparams.i_groupcipher=IEEE80211_WPA_CIPHER_CCMP;
            join.i_wpapsk.i_enabled=1;memcpy(join.i_wpapsk.i_psk,selected.pmk,32);
        }else{join.i_flags=IEEE80211_JOIN_NWKEY;join.i_nwkey.i_wepon=IEEE80211_NWKEY_OPEN;}
        const int error=ieee80211_add_ess(&ic,&join);
        if(selected.specificBssid){memcpy(ic.ic_des_bssid,selected.bssid,6);ic.ic_flags|=IEEE80211_F_DESBSSID;}
        else{memset(ic.ic_des_bssid,0,6);ic.ic_flags&=~IEEE80211_F_DESBSSID;}
        selection::wipe(&selected,sizeof(selected));selection::wipe(&join,sizeof(join));
        if(error)return false;
        ic.ic_flags|=IEEE80211_F_AUTO_JOIN;credentials=true;return true;
    }
    bool startHardware(){
        owner.recordStartup(owner.pci_,30);
        if(!boot->prepare(identity))return false;prepared=true;
        owner.recordStartup(owner.pci_,31);if(!attachProtocol())return false;
        owner.recordStartup(owner.pci_,32);if(!loadCredentials())return false;
        owner.recordStartup(owner.pci_,33);
        static const uint8_t channels[6]={0,1,2,3,8,9};
        RingMemory memory[9]{};
        for(unsigned i=0;i<6;++i){if(tx[i].attach(&ic,channels[i]))return false;
            memory[i]={tx[i].ringMapping().physical,64};completion[channels[i]]=&tx[i].completions();}
        owner.recordStartup(owner.pci_,34);
        if(firmware.attach())return false;
        memory[6]={firmware.ringMapping().physical,64};memory[7]={rxq.ringMapping().physical,64};memory[8]={rpq.ringMapping().physical,64};
        owner.recordStartup(owner.pci_,35);
        queues=new NativeQueueService(runtime,txPointers,firmware,rxq,rpq,{this,receiveData,receiveReports});
        transport=new MacCommandTransport(*owner.loop_,runtime,firmware);
        if(!queues||!transport)return false;
        owner.recordStartup(owner.pci_,36);
        commands=new NativeFirmwareCommands(*transport,identity.epoch);if(!commands)return false;
        events=new MacFirmwareEventBinding(*commands,identity.epoch,this,notification);if(!events)return false;
        // From the first address publication onward any failure retains DMA until stop proves idle.
        owner.recordStartup(owner.pci_,37);
        visible=true;for(auto &q:tx)if(!q.markDeviceVisible())return false;
        if(!firmware.markDeviceVisible()||!rxq.markDeviceVisible()||!rpq.markDeviceVisible())return false;
        owner.recordStartup(owner.pci_,38);
        PciRingSetup<MacPciRingIo> setup(ringIo);if(!setup.configure(memory))return false;
        owner.recordStartup(owner.pci_,39);
        runtimeAttempted=true;if(!runtime.start(memory)||!interrupt->start(&runtime,interruptService,interruptFault,this))return false;
        owner.recordStartup(owner.pci_,40);
        bootStarted=true;if(!boot->start(*commands,*this))return false;
        owner.recordStartup(owner.pci_,41);
        return owner.timer_->setTimeoutMS(10)==kIOReturnSuccess;
    }
    bool dataDrained()const{
        if(pendingTx.frame)return false;
        for(const auto &q:tx)if(const_cast<MacTxDmaQueue&>(q).completions().outstanding())return false;
        return true;
    }
    uint64_t txSubmitted{},rxComplete{},rxWireless{},rxPhy{},rxDeliveryAttempts{},rxGated{};
    uint64_t lastStatus{};
    void publishStatus(){
        if(!owner.pci_)return;
        owner.pci_->setProperty("R16TraceEapolBridgeOk",uint64_t(eapolBridgeOk),64);
        owner.pci_->setProperty("R16TraceEapolBridgeFailed",uint64_t(eapolBridgeFailed),64);
        owner.pci_->setProperty("R16TraceEapolStage",uint64_t(eapolStage),64);
        owner.pci_->setProperty("R16TraceEapolError",uint64_t(eapolError),64);
        owner.pci_->setProperty("R16TraceEapolLengths",uint64_t(eapolLengths),64);
        owner.pci_->setProperty("R16TraceEapolHeader",uint64_t(eapolHeader),64);
        owner.pci_->setProperty("R16TraceEapolEnvelope",uint64_t(eapolEnvelope),64);
        owner.pci_->setProperty("R16TraceEapolDropMask",uint64_t(eapolDropMask),64);
        owner.pci_->setProperty("R16TraceRxManagement",uint64_t(rxTypes[0]),64);
        owner.pci_->setProperty("R16TraceRxControl",uint64_t(rxTypes[1]),64);
        owner.pci_->setProperty("R16TraceRxData",uint64_t(rxTypes[2]),64);
        owner.pci_->setProperty("R16TraceRxOther",uint64_t(rxTypes[3]),64);
        owner.pci_->setProperty("R16TraceRxHardwareCrypto",uint64_t(rxHardwareCrypto),64);
        owner.pci_->setProperty("R16TraceRxSoftwareFallback",uint64_t(rxSoftwareFallback),64);
        owner.pci_->setProperty("R16TraceEapolSeen",uint64_t(rxEapol),64);
        owner.pci_->setProperty("R16TraceEapolGated",uint64_t(rxEapolGated),64);
        owner.pci_->setProperty("R16TraceEapolBridge",uint64_t(rxEapolBridge),64);
        owner.pci_->setProperty("R16TraceDeauthReason",uint64_t(lastDeauthReason),64);
        owner.pci_->setProperty("R16TraceRequestInit",uint64_t(stateRequests[0]),64);
        owner.pci_->setProperty("R16TraceRequestScan",uint64_t(stateRequests[1]),64);
        owner.pci_->setProperty("R16TraceRequestAuth",uint64_t(stateRequests[2]),64);
        owner.pci_->setProperty("R16TraceRequestAssoc",uint64_t(stateRequests[3]),64);
        owner.pci_->setProperty("R16TraceRequestRun",uint64_t(stateRequests[4]),64);
        owner.pci_->setProperty("R16TraceLastRequest",uint64_t(lastStateRequest),64);
        owner.pci_->setProperty("R16TraceRunCommitted",uint64_t(runCommitted),64);
        owner.pci_->setProperty("R16TracePortAuthorizations",uint64_t(portAuthorizations),64);
        owner.pci_->setProperty("R16TraceRxBridgeOk",uint64_t(rxBridgeOk),64);
        owner.pci_->setProperty("R16TraceRxBridgeError",uint64_t(rxBridgeError),64);
        owner.pci_->setProperty("R16TraceRxLastError",uint64_t(lastRxBridgeError),64);
        owner.pci_->setProperty("R16TraceTxPrepareErrors",uint64_t(txPrepareErrors),64);
        owner.pci_->setProperty("R16TraceTxLastError",uint64_t(lastTxPrepareError),64);
        owner.pci_->setProperty("R16TraceStationError",uint64_t(unsigned(station.error())),64);
        owner.pci_->setProperty("R16TraceSupplicant",uint64_t(ic.ic_bss?unsigned(ic.ic_bss->ni_rsn_supp_state):0),64);
        owner.pci_->setProperty("R16TracePortValid",uint64_t(ic.ic_bss?unsigned(ic.ic_bss->ni_port_valid):0),64);
        owner.pci_->setProperty("R16TraceRsnEnabled",uint64_t((ic.ic_flags&IEEE80211_F_RSNON)!=0),64);
        owner.pci_->setProperty("R16TraceActionDeferred",uint64_t(actionDeferred),64);
        owner.pci_->setProperty("R16TraceDataDrained",uint64_t(dataDrained()),64);
        owner.pci_->setProperty("R16TracePendingTx",uint64_t(pendingTx.frame!=nullptr),64);
        owner.pci_->setProperty("R16TraceEapolKey",uint64_t(ic.ic_stats.is_rx_eapol_key),64);
        owner.pci_->setProperty("R16TraceEapolReplay",uint64_t(ic.ic_stats.is_rx_eapol_replay),64);
        owner.pci_->setProperty("R16TraceEapolBadMic",uint64_t(ic.ic_stats.is_rx_eapol_badmic),64);
        owner.pci_->setProperty("R16TraceDeauth",uint64_t(ic.ic_stats.is_rx_deauth),64);
        owner.pci_->setProperty("R16TraceDisassoc",uint64_t(ic.ic_stats.is_rx_disassoc),64);
        owner.pci_->setProperty("R16TraceRxNotAssoc",uint64_t(ic.ic_stats.is_rx_notassoc),64);
        owner.pci_->setProperty("R16TraceRxWrongBss",uint64_t(ic.ic_stats.is_rx_wrongbss),64);
        owner.pci_->setProperty("R16TraceRxDuplicate",uint64_t(ic.ic_stats.is_rx_dup),64);
        owner.pci_->setProperty("R16TraceRxUnauth",uint64_t(ic.ic_stats.is_rx_unauth),64);
        owner.pci_->setProperty("R16TraceTxNoAuth",uint64_t(ic.ic_stats.is_tx_noauth),64);
        owner.pci_->setProperty("R16TraceRxUnencrypted",uint64_t(ic.ic_stats.is_rx_unencrypted),64);
        owner.pci_->setProperty("R16TraceRxDecap",uint64_t(ic.ic_stats.is_rx_decap),64);
        owner.pci_->setProperty("R16TraceCcmpDecrypt",uint64_t(ic.ic_stats.is_ccmp_dec_errs),64);
        owner.pci_->setProperty("R16TraceCcmpReplay",uint64_t(ic.ic_stats.is_ccmp_replays),64);
        owner.pci_->setProperty("R16TraceRxMgmtDiscard",uint64_t(ic.ic_stats.is_rx_mgtdiscard),64);
        owner.pci_->setProperty("R16LiveTxSubmitted",uint64_t(txSubmitted),64);
        owner.pci_->setProperty("R16LiveRxComplete",uint64_t(rxComplete),64);
        owner.pci_->setProperty("R16LiveRxWireless",uint64_t(rxWireless),64);
        owner.pci_->setProperty("R16LiveRxPhy",uint64_t(rxPhy),64);
        owner.pci_->setProperty("R16LiveRxDeliveryAttempts",uint64_t(rxDeliveryAttempts),64);
        owner.pci_->setProperty("R16LiveRxGated",uint64_t(rxGated),64);
        owner.pci_->setProperty("R16LiveCommands",uint64_t(commands?commands->allocated():0),64);
        owner.pci_->setProperty("R16LiveCommandError",uint64_t(commands?unsigned(commands->error()):0),64);
        owner.pci_->setProperty("R16LiveStation",uint64_t(unsigned(station.state())),64);
        owner.pci_->setProperty("R16LiveProtocol",uint64_t(unsigned(ic.ic_state)),64);
        owner.pci_->setProperty("R16LiveEnabled",uint64_t(enabled),64);
        owner.pci_->setProperty("R16LiveFaulted",uint64_t(faulted),64);
        owner.pci_->setProperty("R16LiveBootReady",uint64_t(boot&&boot->ready()),64);
        owner.pci_->setProperty("R16LiveTraffic",uint64_t(unsigned(traffic)),64);
        owner.pci_->setProperty("R16LiveAction",uint64_t(unsigned(activeAction.action)),64);
        owner.pci_->setProperty("R16LiveActionPending",uint64_t(actionInFlight),64);
        owner.pci_->setProperty("R16LiveBsdNamed",uint64_t(owner.interface_&&owner.interface_->getProperty("BSD Name")),64);
    }
    void poll(){
        if(stopping||faulted)return;
        const auto timestamp=now();
        if(!lastStatus||timestamp-lastStatus>=1000000){lastStatus=timestamp;publishStatus();}
        if(!commands->service()){fail("firmware service failed");return;}
        if(stationStarted&&!station.tick(timestamp)){fail("station deadline expired");return;}
        if(actionDeferred&&dataDrained()){
            if(!boot->setTraffic(station::Traffic::none)){fail("scheduler pause after TX drain failed");return;}
            actionDeferred=false;
            if(!boot->beginAction(activeAction,activePeer)){fail("hardware station action rejected");return;}
        }
        if(!boot->service(timestamp)){fail("firmware service failed");return;}
        if(boot->ready()&&!stationStarted){
            stationStarted=true;
            if(!station.restart(identity.epoch,timestamp)||!station.start(identity.interface,timestamp)){fail("station role start failed");return;}
        }
        unsigned budget=16;
        while(completedCount&&budget--){const auto item=completed[0];
            for(unsigned i=1;i<completedCount;++i)completed[i-1]=completed[i];--completedCount;
            actionInFlight=false;
            if(!station.actionComplete(item.token,item.action,item.success,now())){fail("station action completion rejected");return;}
        }
        if(stationStarted&&!station.tick(now())){fail("station timeout");return;}
        if(resetPending&&dataDrained()){resetPending=false;savedState(&ic,IEEE80211_S_INIT,-1);}
        if(stateDeferred&&!resetPending&&dataDrained()&&station.state()==station::State::idle){
            stateDeferred=false;const auto next=static_cast<enum ieee80211_state>(deferredState);
            if(next==IEEE80211_S_INIT)savedState(&ic,next,deferredArgument);
            else newState(&ic,next,deferredArgument);
        }
        if(authPending){authPending=false;savedState(&ic,IEEE80211_S_AUTH,-1);}
        if(probePending){probePending=false;savedState(&ic,IEEE80211_S_SCAN,-1);}
        if(scanDone){scanDone=false;ieee80211_next_scan(&ic.ic_if);}
        if(!applyPendingSelection()){fail("network selection failed");return;}
        if(runPending&&station.state()==station::State::associated){
            runPending=false;if(!savedState(&ic,IEEE80211_S_RUN,-1)&&ic.ic_state==IEEE80211_S_RUN)++runCommitted;
        }
        if(enabled&&credentials&&station.state()==station::State::idle&&ic.ic_state==IEEE80211_S_INIT&&!stateDeferred)
            newState(&ic,IEEE80211_S_SCAN,-1);
        if(ic.ic_state==IEEE80211_S_RUN&&station.state()==station::State::associated&&
           (!(ic.ic_flags&IEEE80211_F_RSNON)||ic.ic_bss->ni_port_valid)){
            if(!station.authorizePort(auth,now())){fail("controlled port authorization failed");return;}
            ++portAuthorizations;owner.IOEthernetController::setLinkStatus(kIONetworkLinkValid|kIONetworkLinkActive);
        }
        if(station.state()==station::State::authorized&&(ic.ic_flags&IEEE80211_F_RSNON)&&!ic.ic_bss->ni_port_valid)
            station.revokePort(auth,now());
        if(attached&&timestamp-lastWatchdog>=1000000){lastWatchdog=timestamp;ieee80211_watchdog(&ic.ic_if);}
        pumpTx();
        if(!faulted&&owner.timer_->setTimeoutMS(10)!=kIOReturnSuccess)fail("controller timer failed");
    }
    bool shutdown(){
        stopping=true;enabled=false;traffic=station::Traffic::none;owner.timer_->cancelTimeout();
        pendingSelection.clear();
        owner.IOEthernetController::setLinkStatus(kIONetworkLinkValid);
        if(commands)commands->invalidate();
        // Disable the source first. No memory is released while a callback borrows it.
        bool idle=interruptAttached?interrupt->stop():true;
        if(visible&&(!runtime.result.stopped))idle=runtime.stop()&&idle;
        const bool backendStopped=boot->stop();
        if(!idle||!backendStopped||(queues&&queues->serving())||(interrupt&&interrupt->servicing()))return false;
        if(interruptAttached&&!interrupt->detach())return false;interruptAttached=false;
        if(interrupt){interrupt->release();interrupt=nullptr;}
        if(pendingTx.frame)releaseTx(&ic,pendingTx);
        bool released=true;
        for(auto &q:tx)released=q.releaseAfterDmaStopped()&&released;
        released=firmware.releaseAfterDmaStopped()&&released;
        released=rxq.releaseAfterDmaStopped()&&rpq.releaseAfterDmaStopped()&&released;
        if(!released)return false;
        if(attached){ic.ic_if.if_flags&=~IFF_RUNNING;ic.ic_newstate=savedState;
            savedState(&ic,IEEE80211_S_INIT,-1);ieee80211_ifdetach(&ic.ic_if);if_detach(&ic.ic_if);attached=false;}
        if(bound){if(protocol.unbindAfterProtocolDetached()!=kIOReturnSuccess)return false;bound=false;}
        return true;
    }
};
bool R16NetworkController::createWorkLoop(){if(!loop_)loop_=IOWorkLoop::workLoop();return loop_!=nullptr;}
IOWorkLoop *R16NetworkController::getWorkLoop()const{return loop_;}
IOReturn R16NetworkController::startGated(OSObject *o,void*,void*,void*,void*){
    auto *s=static_cast<R16NetworkController*>(o)->state_;return s->startHardware()?kIOReturnSuccess:kIOReturnError;
}
IOReturn R16NetworkController::stopGated(OSObject *o,void*,void*,void*,void*){
    auto *s=static_cast<R16NetworkController*>(o)->state_;return !s||s->shutdown()?kIOReturnSuccess:kIOReturnBusy;
}
void R16NetworkController::timer(OSObject *o,IOTimerEventSource*){auto *s=static_cast<R16NetworkController*>(o)->state_;if(s)s->poll();}
void R16NetworkController::recordStartup(IOService *provider,unsigned stage,bool failed){
    startupStage_=stage;
    // The PCI provider outlives a failed/detached controller. Only constant
    // identifiers and numeric progress are persisted, never SSID or key data.
    if(provider){
        provider->setProperty("R16NetworkTestId","NETWORK-START-02");
        provider->setProperty("R16NetworkStage",uint64_t(stage),32);
        provider->setProperty("R16NetworkStartFailed",failed);
    }
    IOLog("RTL8852BE startup stage=%u failed=%u\n",stage,unsigned(failed));
}
bool R16NetworkController::start(IOService *provider){
    recordStartup(provider,1);
    if(!IOEthernetController::start(provider)){recordStartup(provider,1,true);return false;}superStarted_=true;
    recordStartup(provider,2);
    pci_=OSDynamicCast(IOPCIDevice,provider);
    if(!pci_||pci_->configRead16(kIOPCIConfigVendorID)!=0x10ec||pci_->configRead16(kIOPCIConfigDeviceID)!=0xb852)goto failed;
    pci_->retain();providerRetained_=true;
    recordStartup(provider,3);
    if(!pci_->open(this))goto failed;providerOpened_=true;
    pci_->setMemoryEnable(true);pci_->setBusMasterEnable(false);
    recordStartup(provider,4);
    bar_=pci_->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2);
    if(!bar_||bar_->getLength()<0x10000||!createWorkLoop())goto failed;
    recordStartup(provider,5);
    gate_=IOCommandGate::commandGate(this);timer_=IOTimerEventSource::timerEventSource(this,timer);
    if(!gate_||!timer_)goto failed;
    recordStartup(provider,6);
    if(loop_->addEventSource(gate_)!=kIOReturnSuccess||loop_->addEventSource(timer_)!=kIOReturnSuccess)goto failed;
    {
        recordStartup(provider,7);
        auto *boot=createBootService();if(!boot){setProperty("R16Failure","concrete boot service missing");goto failed;}
        recordStartup(provider,8);
        state_=new State(*this,boot);if(!state_){delete boot;goto failed;}
        recordStartup(provider,9);
        if(!boot->allocate(*pci_,*bar_,*loop_))goto failed;
        for(unsigned i=0;i<6;++i){recordStartup(provider,10+i);if(!state_->tx[i].allocate(pci_,loop_))goto failed;}
        recordStartup(provider,16);if(!state_->firmware.allocate(pci_,loop_))goto failed;
        recordStartup(provider,17);if(!state_->rxq.allocate(pci_,loop_))goto failed;
        recordStartup(provider,18);if(!state_->rpq.allocate(pci_,loop_))goto failed;
        recordStartup(provider,19);
        state_->interrupt=new R16PciInterrupts;if(!state_->interrupt)goto failed;
        int msi=-1;for(int index=0;index<32;++index){int kind=0;
            if(pci_->getInterruptType(index,&kind)!=kIOReturnSuccess)break;
            if(kind&kIOInterruptTypePCIMessaged){msi=index;break;}}
        recordStartup(provider,20);
        if(msi<0||!state_->interrupt->attach(pci_,loop_,msi))goto failed;state_->interruptAttached=true;
        recordStartup(provider,21);
        if(gate_->runAction(startGated)!=kIOReturnSuccess)goto failed;
        recordStartup(provider,50);
        auto *dict=OSDictionary::withCapacity(1);auto *medium=IONetworkMedium::medium(kIOMediumEthernetAuto,0);
        bool published=dict&&medium&&IONetworkMedium::addMedium(dict,medium)&&publishMediumDictionary(dict)&&setCurrentMedium(medium)&&setSelectedMedium(medium);
        if(medium)medium->release();if(dict)dict->release();if(!published)goto failed;
        recordStartup(provider,51);
        if(!attachInterface(reinterpret_cast<IONetworkInterface**>(&interface_),true))goto failed;
    }
    IOEthernetController::setLinkStatus(kIONetworkLinkValid);registerService();interface_->registerService();
    recordStartup(provider,52);return true;
failed:
    recordStartup(provider,startupStage_,true);
    releaseResources();if(superStarted_){IOEthernetController::stop(provider);superStarted_=false;}return false;
}
bool R16NetworkController::configureInterface(IONetworkInterface *netif){
    if(!state_||!IOEthernetController::configureInterface(netif))return false;
    auto *data=netif->getParameter(kIONetworkStatsKey);auto *eth=OSDynamicCast(IOEthernetInterface,netif);
    if(!data||!data->getBuffer()||!eth)return false;
    state_->ic.ic_if.netStat=const_cast<IONetworkStats*>(static_cast<const IONetworkStats*>(data->getBuffer()));state_->ic.ic_if.iface=eth;return true;
}
IOReturn R16NetworkController::selectMedium(const IONetworkMedium *medium){
    if(!medium||medium->getType()!=kIOMediumEthernetAuto)return kIOReturnUnsupported;
    return setSelectedMedium(medium)?kIOReturnSuccess:kIOReturnError;
}
IOReturn R16NetworkController::getPacketFilters(const OSSymbol *group,UInt32 *filters)const{
    if(!filters)return kIOReturnBadArgument;
    // Do not advertise base-class multicast/promiscuous filters whose native
    // setters are still Unsupported. IPv4 unicast/broadcast remain available.
    if(group==gIONetworkFilterGroup){*filters=kIOPacketFilterUnicast|kIOPacketFilterBroadcast;return kIOReturnSuccess;}
    *filters=0;return kIOReturnUnsupported;
}
IOReturn R16NetworkController::getHardwareAddress(IOEthernetAddress *address){
    if(!state_||!state_->prepared||!address)return kIOReturnNotReady;
    memcpy(address->bytes,state_->identity.interface.address.bytes,6);return kIOReturnSuccess;
}
IOReturn R16NetworkController::enableGated(OSObject *o,void *on,void*,void*,void*){
    auto &owner=*static_cast<R16NetworkController*>(o);auto *s=owner.state_;
    if(!s||s->faulted||s->stopping)return kIOReturnNotReady;s->enabled=on!=nullptr;
    if(s->enabled){s->ic.ic_if.if_flags|=IFF_UP|IFF_RUNNING;s->lastWatchdog=s->now();}
    else{s->pendingSelection.clear();s->ic.ic_if.if_flags&=~(IFF_UP|IFF_RUNNING);s->stateDeferred=true;s->deferredState=IEEE80211_S_INIT;s->deferredArgument=-1;
        if(s->stationStarted)s->station.disconnect(s->now());
        owner.IOEthernetController::setLinkStatus(kIONetworkLinkValid);}
    return kIOReturnSuccess;
}
IOReturn R16NetworkController::enable(IONetworkInterface*){return gate_?gate_->runAction(enableGated,this):kIOReturnNotReady;}
IOReturn R16NetworkController::disable(IONetworkInterface*){return gate_?gate_->runAction(enableGated):kIOReturnNotReady;}
IOReturn R16NetworkController::selectionGated(OSObject *o,void *request,void*,void*,void*){
    auto *s=static_cast<R16NetworkController*>(o)->state_;
    return s?s->queueSelection(static_cast<const selection::Join*>(request)):kIOReturnNotReady;
}
IOReturn R16NetworkController::selectWirelessNetwork(const selection::Join &request){
    return gate_?gate_->runAction(selectionGated,const_cast<selection::Join*>(&request)):kIOReturnNotReady;
}
IOReturn R16NetworkController::disconnectWirelessNetwork(){return gate_?gate_->runAction(selectionGated):kIOReturnNotReady;}
IOReturn R16NetworkController::outputGated(OSObject *o,void *p,void *out,void*,void*){
    auto &owner=*static_cast<R16NetworkController*>(o);auto *s=owner.state_;auto m=static_cast<mbuf_t>(p);
    auto &result=*static_cast<UInt32*>(out);result=kIOReturnOutputDropped;
    if(!s||!s->enabled||s->faulted||s->traffic!=station::Traffic::authorized||!s->ic.ic_if.if_snd.queue){mbuf_freem(m);return kIOReturnSuccess;}
    if(!s->ic.ic_if.if_snd.queue->lockEnqueue(m)){mbuf_freem(m);return kIOReturnSuccess;}
    result=kIOReturnOutputSuccess;s->pumpTx();return kIOReturnSuccess;
}
UInt32 R16NetworkController::outputPacket(mbuf_t m,void*){
    if(!m)return kIOReturnOutputDropped;UInt32 result=kIOReturnOutputDropped;
    if(!gate_){mbuf_freem(m);return result;}
    if(gate_->runAction(outputGated,m,&result)!=kIOReturnSuccess)mbuf_freem(m);return result;
}
bool R16NetworkController::setLinkStatus(UInt32 status,const IONetworkMedium *medium,UInt64 speed,OSData *data){
    if(!state_||state_->traffic!=station::Traffic::authorized)status&=~kIONetworkLinkActive;
    return IOEthernetController::setLinkStatus(status,medium,speed,data);
}
void R16NetworkController::releaseResources(){
    if(state_&&gate_&&gate_->runAction(stopGated)!=kIOReturnSuccess){
        // Preserve the entire borrowed object graph, not only physical pages.
        if(!retainedFault_){retainedFault_=true;retain();}
        setProperty("R16Failure","shutdown not proven; owner and DMA retained");return;
    }
    if(interface_){detachInterface(interface_);interface_=nullptr;}
    delete state_;state_=nullptr;
    if(timer_){timer_->cancelTimeout();if(timer_->getWorkLoop())loop_->removeEventSource(timer_);timer_->release();timer_=nullptr;}
    if(gate_){if(gate_->getWorkLoop())loop_->removeEventSource(gate_);gate_->release();gate_=nullptr;}
    if(bar_){bar_->release();bar_=nullptr;}
    if(providerOpened_){pci_->close(this);providerOpened_=false;}
    if(providerRetained_){pci_->release();providerRetained_=false;}pci_=nullptr;
}
void R16NetworkController::stop(IOService *provider){releaseResources();if(superStarted_){IOEthernetController::stop(provider);superStarted_=false;}}
void R16NetworkController::free(){releaseResources();if(retainedFault_)return;if(loop_){loop_->release();loop_=nullptr;}IOEthernetController::free();}
