// SPDX-License-Identifier: GPL-2.0-or-later
#include "MacNetworkController.hpp"
#include "Net80211Runtime.hpp"
#include "MacPciRingIo.hpp"
#include "MacNetworkPhyWait.hpp"
#include "RxTrace.hpp"
#include "WirelessControl.hpp"
#include "AuthenticationBinding.hpp"
#include "NativeScanObservation.hpp"
#include "NativeScanProbeTx.hpp"
#include "NativeWclScanPlan.hpp"
#include "NativeWclScanResults.hpp"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/_if_ether.h>
#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_node.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IONetworkMedium.h>
#include <IOKit/network/IONetworkData.h>
#include <IOKit/network/IOOutputQueue.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSData.h>
#include <IOKit/IOLib.h>
using namespace rtl8852be;
using namespace rtl8852be::network;
// Pinned net80211 output.c helper: consumes mbuf; caller supplies node reference.
int ieee80211_mgmt_output(struct _ifnet*,struct ieee80211_node*,mbuf_t,int);
OSDefineMetaClassAndStructors(R16NetworkController,IOEthernetController)
struct R16NetworkController::State final : MacNetworkBootSink {
    struct ProbeOps {
        using Node=ieee80211_node;State &s;
        Node *createNode(uint8_t number,const uint8_t *rates,size_t count){
            if(!s.ic.ic_node_alloc||!s.ic.ic_node_free||count>IEEE80211_RATE_MAXSIZE)return nullptr;
            auto *node=s.ic.ic_node_alloc(&s.ic);if(!node)return nullptr;
            memset(node,0,sizeof(*node));node->ni_ic=&s.ic;node->ni_chan=&s.ic.ic_channels[number];
            node->ni_state=IEEE80211_STA_CACHE;node->ni_rates.rs_nrates=uint8_t(count);
            memcpy(node->ni_rates.rs_rates,rates,count);
            memset(node->ni_macaddr,0xff,6);memset(node->ni_bssid,0xff,6);
            // Detached, never inserted into ic_tree and never marked COLLECT.
            return ieee80211_ref_node(node);
        }
        mbuf_t createBody(const uint8_t *body,size_t bytes){
            mbuf_t frame=nullptr;unsigned chunks=1;
            if(mbuf_allocpacket(MBUF_DONTWAIT,bytes,&chunks,&frame))return nullptr;
            if(mbuf_copyback(frame,0,bytes,body,MBUF_DONTWAIT)||chunks!=1||
               mbuf_len(frame)!=bytes||mbuf_pkthdr_len(frame)!=bytes){mbuf_freem(frame);return nullptr;}
            return frame;
        }
        void freeBody(mbuf_t frame){mbuf_freem(frame);}
        void retainNode(Node *node){ieee80211_ref_node(node);}
        void releaseNode(Node *node){ieee80211_release_node(&s.ic,node);}
        unsigned references(Node *node){
            return node!=s.ic.ic_bss&&node->ni_state==IEEE80211_STA_CACHE&&!node->ni_unref_cb?
                node->ni_refcnt:UINT_MAX;
        }
        void freeDetachedNode(Node *node){s.ic.ic_node_free(&s.ic,node);}
        void lockQueue(){IORecursiveLockLock(s.ic.ic_mgtq.mq_mtx);}
        void unlockQueue(){IORecursiveLockUnlock(s.ic.ic_mgtq.mq_mtx);}
        bool queueReady(){return !s.outputPumping&&mq_empty(&s.ic.ic_mgtq)&&!mq_full(&s.ic.ic_mgtq);}
        unsigned queueDrops(){return mq_drops(&s.ic.ic_mgtq);}
        void suppressPump(bool value){s.outputPumping=value;}
        int managementOutput(Node *node,mbuf_t frame){
            return ieee80211_mgmt_output(&s.ic.ic_if,node,frame,IEEE80211_FC0_SUBTYPE_PROBE_REQ);
        }
        bool queueContainsOnly(Node *node){
            const auto head=MBUF_LIST_FIRST(&s.ic.ic_mgtq.mq_list);
            return mq_len(&s.ic.ic_mgtq)==1&&head&&
                reinterpret_cast<Node*>(mbuf_pkthdr_rcvif(head))==node;
        }
    };
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
    void (*savedEvent)(ieee80211com*,int,void*){};
    nativescan::Observer *scanObservations{}; // ~300 KiB, heap only; optional.
    foregroundscan::Controller foregroundScan;
    nativewclresults::Bridge *wclResults{}; // Optional gate-owned draft state.
    nativewclscan::Request wclRequest{};
    foregroundscan::Token wclRequestToken{};
    bool wclRequestValid{};
    scanprobe::FrameOwner<ProbeOps> foregroundProbe;
    foregroundscan::Dwell foregroundProbeOperation{};
    TxCounters foregroundProbeBefore{};unsigned foregroundProbeRing{6};
    bool foregroundCompletedThisPoll{};
    bool bound{},attached{},visible{},runtimeAttempted{},prepared{},bootStarted{},stationStarted{},interruptAttached{},credentials{};
    bool enabled{},faulted{},stopping{},scanDone{},authPending{},probePending{},runPending{},resetPending{};
    bool stateDeferred{};int deferredState{},deferredArgument{};
    selection::Pending pendingSelection;
    MacLinkPublication linkPublication;
    bool linkPublicationValid{};
    authevents::Queue authenticationEvents;
    uint64_t stateRequests[5]{},runCommitted{},portAuthorizations{},rxBridgeOk{},rxBridgeError{},txPrepareErrors{};
    uint64_t lastStateRequest{},lastRxBridgeError{},lastTxPrepareError{};
    uint64_t rxTypes[4]{},rxHardwareCrypto{},rxSoftwareFallback{},rxEapol{},rxEapolGated{},rxEapolBridge{},lastDeauthReason{};
    uint64_t eapolBridgeOk{},eapolBridgeFailed{},eapolStage{},eapolError{},eapolLengths{},eapolHeader{},eapolEnvelope{},eapolDropMask{};
    void deliver(const uint8_t *data,size_t length,uint8_t channel,int rssi){
        receivingProtocol=true;++rxDeliveryAttempts;
        RxPacket packet;
        RxTrace metadata;
        if(decodeRx(data,length,4,packet)==DescriptorStatus::ok){
            metadata=inspectRx(packet,identity.interface.address.bytes);
            if(packet.info.pkt_type==0&&!hardwareDecrypted(packet.info)&&packet.length>=28)
                authenticationEvents.observe(packet.payload,packet.length-4,channel,now());
            if(scanObservations&&packet.info.pkt_type==0&&!hardwareDecrypted(packet.info)&&
               packet.length>=40&&
               ((ic.ic_state==IEEE80211_S_SCAN&&!(ic.ic_flags&IEEE80211_F_BGSCAN))||
                (ic.ic_state==IEEE80211_S_INIT&&foregroundScan.observing(
                    {station.scanToken().epoch,station.scanToken().operation})))&&
               station.state()==station::State::scanningDwell&&channel&&ic.ic_channels[channel].ic_freq){
                const nativescan::Channel observedChannel{
                    IEEE80211_IS_CHAN_5GHZ(&ic.ic_channels[channel])?nativescan::Band::ghz5:nativescan::Band::ghz2,channel};
                // The boot backend supplies the most recent real PHY sample;
                // this does not claim exact per-MPDU/PPDU RSSI attribution.
                nativescan::Signal signal{};
                if(rssi>=0&&rssi<=100)signal={nativescan::SignalUnit::percent,int16_t(rssi)};
                int dbm=0;if(boot->rxSignalDbm(dbm)&&dbm>=-127&&dbm<=0)
                    signal={nativescan::SignalUnit::dbm,int16_t(dbm)};
                const auto token=station.scanToken();
                // RX includes a four-byte FCS. Copy complete raw IEs before
                // protocol delivery can request another state/channel.
                scanObservations->observe({token.epoch,token.operation},packet.payload,packet.length-4,
                                           observedChannel,signal,now());
            }
        }
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
        // Observation allocation failure must not alter existing connectivity.
        scanObservations=new nativescan::Observer;
    }
    ~State(){delete wclResults;delete scanObservations;delete events;delete boot;delete commands;delete transport;delete queues;}
    uint64_t now(){return runtimeIo.nowUs();}
    bool inGate(){return owner.loop_->inGate();}
    void abortWclDraft(){
        if(wclResults)wclResults->abort();
        bzero(&wclRequest,sizeof(wclRequest));wclRequestValid=false;
        // Keep wclRequestToken until the frontend retires an aborted draft.
    }
    void supersedeWclDraft(){
        abortWclDraft();
        if(wclResults)wclResults->retire();
        wclRequestToken={};
    }
    void fail(const char *reason){
        if(faulted)return;faulted=true;traffic=station::Traffic::none;enabled=false;
        // The station failure path never calls scanFinished. Do not reenter it
        // from this callback; a later proven shutdown releases the drain debt.
        foregroundScan.fail(foregroundscan::Reason::backend,now());
        if(inGate())abortWclDraft();
        if(scanObservations&&inGate())scanObservations->cancel();
        authenticationEvents.disable();
        pendingSelection.clear();
        ic.ic_if.if_flags&=~IFF_RUNNING;
        owner.setLinkStatus(kIONetworkLinkValid);
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
            traffic=t;phyWait.clear();dropForegroundHostProbe();if(pendingTx.frame)releaseTx(&ic,pendingTx);
            owner.setLinkStatus(kIONetworkLinkValid);
            if(actionInFlight||!dataDrained())return true;
            return boot->setTraffic(t);
        }
        if(actionInFlight||!boot->setTraffic(t))return false;
        if(t!=station::Traffic::authorized&&pendingTx.frame)releaseTx(&ic,pendingTx);
        traffic=t;
        if(t!=station::Traffic::authorized)owner.setLinkStatus(kIONetworkLinkValid);
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
        traffic=station::Traffic::none;phyWait.clear();dropForegroundHostProbe();if(pendingTx.frame)releaseTx(&ic,pendingTx);
        activeAction=r;activePeer=peer;actionInFlight=actionDeferred=true;return true;
    }
    bool beginAuthentication(station::Token t,const station::Peer &peer){
        auth=t;authenticationEvents.bind(t.epoch,t.operation,identity.interface.address.bytes,
            peer.bssid.bytes,peer.channel.primary);authPending=true;return true;
    }
    bool sendProbe(station::Token token,const station::Peer *peer,const station::ScanChannel &channel){
        if(foregroundScan.owns({token.epoch,token.operation})){
            // Only schedule here: StationController is still in its completion
            // callback. The poll checks the original dwell again before TX.
            return !peer&&channel.active&&activeProbeChannel(channel.channel.primary)&&
                channel.channel.band==0&&channel.channel.width==0&&
                foregroundScan.requestProbe({token.epoch,token.operation},
                    {nativescan::Band::ghz2,channel.channel.primary});
        }
        probePending=true;return true;
    }
    bool cancelProtocol(station::Token){
        cancelForeground(foregroundscan::Reason::protocol,false);
        if(scanObservations)scanObservations->cancel();
        authenticationEvents.invalidate();authPending=probePending=runPending=false;resetPending=true;return true;
    }
    void scanFinished(station::Token token,bool cancelled){
        if(foregroundScan.owns({token.epoch,token.operation})){
            const auto timestamp=now();
            const bool cancelling=foregroundScan.draining()||timestamp>=foregroundScan.status().deadlineUs;
            const bool probeOk=finishForegroundProbe({token.epoch,token.operation},!cancelled&&!cancelling);
            bool observed=false;
            if(scanObservations){
                if(cancelled||cancelling||!probeOk)scanObservations->cancel();
                else observed=scanObservations->channelFinished({token.epoch,token.operation},false);
            }
            foregroundScan.channelFinished({token.epoch,token.operation},cancelled,timestamp);
            if(!cancelled&&!cancelling&&!observed){
                foregroundScan.fail(foregroundscan::Reason::observation,timestamp);
                if(scanObservations)scanObservations->cancel();
            }
            if(foregroundScan.status().phase==foregroundscan::Phase::failed||
               foregroundScan.status().phase==foregroundscan::Phase::cancelled||
               foregroundScan.status().phase==foregroundscan::Phase::timedOut||
               foregroundScan.draining())abortWclDraft();
            // StationController clears its scanToken only AFTER this callback.
            // Starting the next scan here would reenter it and lose that token.
            foregroundCompletedThisPoll=true;return;
        }
        if(scanObservations)scanObservations->channelFinished({token.epoch,token.operation},cancelled);
        if(!cancelled&&ic.ic_state==IEEE80211_S_SCAN&&!foregroundScan.active())scanDone=true;
    }
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
    void observeScanPlan(){
        if(!scanObservations||scanObservations->open())return;
        nativescan::Channel planned[nativescan::maxChannels]{};size_t count=0;
        const bool active=(ic.ic_flags&IEEE80211_F_ASCAN)!=0;
        const auto current=channelOf(&ic,ic.ic_bss->ni_chan);
        bool valid=channel::validChannel(current)&&!(ic.ic_flags&IEEE80211_F_BGSCAN);
        // next_scan has already cleared the selected channel's bitmap bit.
        // Reinsert only that channel, then apply its actual passive-scan rule.
        for(unsigned i=1;valid&&i<=IEEE80211_CHAN_MAX;++i){
            const auto &candidate=ic.ic_channels[i];
            if(i!=current.primary&&!isset(ic.ic_chan_scan,i))continue;
            if(active&&(candidate.ic_flags&IEEE80211_CHAN_PASSIVE))continue;
            if(!isset(ic.ic_chan_active,i)||!candidate.ic_freq||!candidate.ic_flags||
               i>255||count==nativescan::maxChannels){valid=false;break;}
            planned[count++]={IEEE80211_IS_CHAN_5GHZ(&candidate)?nativescan::Band::ghz5:nativescan::Band::ghz2,uint8_t(i)};
        }
        // A bad/oversize plan blocks observations for this pass only. Do not
        // change channel policy, ask for another scan, or fail the radio.
        scanObservations->begin(identity.epoch,int(ic.ic_curmode),active,valid?planned:nullptr,valid?count:0);
    }
    static void protocolEvent(ieee80211com *ic,int event,void *data){
        auto *s=from(ic);if(!s)return;
        if(event==IEEE80211_EVT_SCAN_DONE&&s->scanObservations&&!s->foregroundScan.active()){
            if(s->inGate()&&!s->stopping&&!s->faulted&&ic->ic_state==IEEE80211_S_SCAN&&
               !(ic->ic_flags&IEEE80211_F_BGSCAN))
                s->scanObservations->finish(s->identity.epoch,int(ic->ic_curmode),
                    (ic->ic_flags&IEEE80211_F_ASCAN)!=0,s->now());
            // An unexpected caller outside the gate cannot mutate the cache.
            else if(s->inGate())s->scanObservations->cancel();
        }
        if(s->savedEvent&&s->savedEvent!=protocolEvent)s->savedEvent(ic,event,data);
    }
    static int newState(ieee80211com *ic,enum ieee80211_state next,int arg){
        auto &s=*from(ic);if(s.stopping||s.faulted)return ENETDOWN;
        if(!s.inGate()){s.fail("net80211 state outside gate");return EIO;}
        if(s.foregroundScan.active()){
            if(next!=IEEE80211_S_INIT)return EBUSY;
            s.cancelForeground(foregroundscan::Reason::protocol,false);
        }
        if(next!=IEEE80211_S_SCAN&&s.scanObservations)s.scanObservations->cancel();
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
            s.observeScanPlan();
            const bool accepted=s.station.scan(&channel,1,s.now());
            if(s.scanObservations){
                if(accepted){
                    const auto token=s.station.scanToken();
                    s.scanObservations->channelAccepted(s.identity.epoch,int(ic->ic_curmode),
                        (ic->ic_flags&IEEE80211_F_ASCAN)!=0,
                        {channel.channel.band?nativescan::Band::ghz5:nativescan::Band::ghz2,channel.channel.primary},
                        {token.epoch,token.operation});
                }else s.scanObservations->rejectedChannel();
            }
            return accepted?0:EIO;
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
                if(error){++txPrepareErrors;lastTxPrepareError=unsigned(error);
                    if(foregroundScan.active()&&foregroundScan.status().activeScan){fail("foreground probe preparation failed");break;}
                    continue;}
            }
            const bool probe=foregroundProbe.node()&&pendingTx.node==foregroundProbe.node();
            if(foregroundScan.active()&&foregroundScan.status().activeScan&&!probe){
                releaseTx(&ic,pendingTx);fail("unexpected management TX during foreground scan");break;
            }
            nativescan::Channel probeChannel;
            if(probe&&(!foregroundScan.pendingProbe(foregroundProbeOperation,probeChannel)||
               !foregroundProbeContext(foregroundProbeOperation,probeChannel))){
                releaseTx(&ic,pendingTx);cancelForeground(foregroundscan::Reason::probe,true);break;
            }
            TxInfo info{};unsigned ring=9;
            if(!boot->txInfo(pendingTx,info,ring)||ring>=6){releaseTx(&ic,pendingTx);fail("TX descriptor policy unavailable");break;}
            if(tx[ring].completions().full())break;
            if(probe){
                if(ring!=4||info.ch_dma!=8||tx[ring].completions().outstanding()){
                    releaseTx(&ic,pendingTx);fail("foreground probe TX ownership conflict");break;
                }
                foregroundProbeRing=ring;foregroundProbeBefore=tx[ring].completions().counters();
                if(!foregroundProbeContext(foregroundProbeOperation,probeChannel)){
                    releaseTx(&ic,pendingTx);cancelForeground(foregroundscan::Reason::probe,true);break;
                }
            }
            const int error=submitNativeData(runtime,tx[ring],ring,pendingTx,info);
            if(error){if(pendingTx.frame)releaseTx(&ic,pendingTx);fail("native TX publication failed");break;}
            if(probe&&!foregroundScan.submittedProbe(foregroundProbeOperation)){
                fail("foreground probe publication identity changed");break;
            }
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
        savedEvent=ic.ic_event_handler;ic.ic_event_handler=protocolEvent;
        return savedState&&ic.ic_bss&&ifp.if_snd.queue;
    }
    static void fillJoin(const selection::Join &selected,ieee80211_join &join){
        memset(&join,0,sizeof(join));join.i_len=selected.ssidLength;
        memcpy(join.i_nwid,selected.ssid,selected.ssidLength);
        if(selected.security==selection::Security::wpa2Psk||selected.security==selection::Security::wpaPsk){
            join.i_flags=IEEE80211_JOIN_WPAPSK|IEEE80211_JOIN_WPA;
            join.i_wpaparams.i_enabled=1;
            join.i_wpaparams.i_protos=selected.security==selection::Security::wpaPsk?
                IEEE80211_WPA_PROTO_WPA1:IEEE80211_WPA_PROTO_WPA2;
            join.i_wpaparams.i_akms=IEEE80211_WPA_AKM_PSK;
            join.i_wpaparams.i_ciphers=selected.pairwise==selection::Cipher::tkip?
                IEEE80211_WPA_CIPHER_TKIP:IEEE80211_WPA_CIPHER_CCMP;
            join.i_wpaparams.i_groupcipher=selected.group==selection::Cipher::tkip?
                IEEE80211_WPA_CIPHER_TKIP:IEEE80211_WPA_CIPHER_CCMP;
            join.i_wpapsk.i_enabled=1;memcpy(join.i_wpapsk.i_psk,selected.pmk,32);
        }else{join.i_flags=IEEE80211_JOIN_NWKEY;join.i_nwkey.i_wepon=IEEE80211_NWKEY_OPEN;}
    }
    bool loadCredentials(){
        // Existing WPA2-CCMP startup remains the default; optional settings never
        // widen the negotiated protocol/cipher set or silently fall back.
        auto *ssid=OSDynamicCast(OSData,owner.getProperty("R16SSID"));
        if(!ssid)return true;
        if(!ssid->getLength()||ssid->getLength()>32)return false;
        selection::Join selected;selected.ssidLength=ssid->getLength();
        memcpy(selected.ssid,ssid->getBytesNoCopy(),selected.ssidLength);
        auto *psk=OSDynamicCast(OSData,owner.getProperty("R16PSK"));
        selected.security=psk?selection::Security::wpa2Psk:selection::Security::open;
        auto *modeObject=owner.getProperty("R16Security");
        auto *mode=OSDynamicCast(OSString,modeObject);
        if(modeObject&&!mode)return false;
        if(mode){
            if(mode->isEqualTo("open"))selected.security=selection::Security::open;
            else if(mode->isEqualTo("wpa-psk"))selected.security=selection::Security::wpaPsk;
            else if(mode->isEqualTo("wpa2-psk"))selected.security=selection::Security::wpa2Psk;
            else return false;
        }
        if((selected.security==selection::Security::open&&psk)||
           (selected.security!=selection::Security::open&&(!psk||psk->getLength()!=32)))return false;
        auto parseCipher=[&](const char *key,selection::Cipher &out){
            auto *object=owner.getProperty(key);if(!object)return true;
            auto *text=OSDynamicCast(OSString,object);if(!text)return false;
            if(text->isEqualTo("ccmp")){out=selection::Cipher::ccmp;return true;}
            if(text->isEqualTo("tkip")){out=selection::Cipher::tkip;return true;}
            return false;
        };
        if(!parseCipher("R16PairwiseCipher",selected.pairwise)||
           !parseCipher("R16GroupCipher",selected.group))return false;
        if(psk){selected.pmkLength=32;memcpy(selected.pmk,psk->getBytesNoCopy(),32);}
        if(!selection::valid(selected)){selection::wipe(&selected,sizeof(selected));return false;}
        ieee80211_join join;fillJoin(selected,join);selection::wipe(&selected,sizeof(selected));
        const int error=ieee80211_add_ess(&ic,&join);selection::wipe(&join,sizeof(join));
        if(error)return false;ic.ic_flags|=IEEE80211_F_AUTO_JOIN;credentials=true;return true;
    }
    bool copyNode(const ieee80211_node *node,wireless::Network &out){
        if(!node||node->ni_esslen>sizeof(out.ssid))return false;
        memset(&out,0,sizeof(out));out.ssidLength=node->ni_esslen;
        memcpy(out.ssid,node->ni_essid,out.ssidLength);memcpy(out.bssid,node->ni_bssid,6);
        if(node->ni_chan&&node->ni_chan!=IEEE80211_CHAN_ANYC){
            const auto channel=channelOf(&ic,node->ni_chan);
            out.channel=channel.primary;out.fiveGhz=channel.band==1;
        }
        // Rtw8852bHardware feeds PHY normalized 0..100 RSSI to net80211.
        out.signalPercent=node->ni_rssi>100?100:node->ni_rssi;
        out.privacy=(node->ni_capinfo&IEEE80211_CAPINFO_PRIVACY)!=0;
        out.protocols=node->ni_rsnprotos;out.akms=node->ni_rsnakms;
        out.ciphers=node->ni_rsnciphers;out.rsnCapabilities=node->ni_rsncaps;
        return true;
    }
    IOReturn copyWirelessStatus(wireless::Snapshot &out){
        memset(&out,0,sizeof(out));out.sampledAtUs=now();
        out.selectionGeneration=pendingSelection.generation();
        out.selectionPending=pendingSelection.waiting();
        out.scanInProgress=attached&&(ic.ic_state==IEEE80211_S_SCAN||foregroundScan.active());
        const bool run=attached&&ic.ic_state==IEEE80211_S_RUN;
        const bool authorized=station.state()==station::State::authorized&&
            (!(ic.ic_flags&IEEE80211_F_RSNON)||(ic.ic_bss&&ic.ic_bss->ni_port_valid));
        out.link=wireless::linkState(enabled,faulted,stopping,out.selectionPending,
            out.scanInProgress,run,authorized,attached&&
            (ic.ic_state==IEEE80211_S_AUTH||ic.ic_state==IEEE80211_S_ASSOC));
        if(!attached)return kIOReturnNotReady;
        if(run&&!out.selectionPending&&enabled&&!faulted&&!stopping)
            out.currentValid=copyNode(ic.ic_bss,out.current);
        // Iterate under the same gate as RX and cache expiry; never retain nodes.
        unsigned visited=0;ieee80211_node *node;
        RB_FOREACH(node,ieee80211_tree,&ic.ic_tree){
            if(++visited>256){out.cacheTruncated=true;break;}
            wireless::Network entry;
            if(copyNode(node,entry)&&!wireless::append(out,entry))break;
        }
        return kIOReturnSuccess;
    }
    void bindAuthenticationEventsIfReady(){
        if(authenticationEvents.needsBinding()&&ic.ic_bss&&ic.ic_bss->ni_chan&&
           ic.ic_bss->ni_chan!=IEEE80211_CHAN_ANYC&&
           authevents::currentAssociation(station.state(),station.associationToken(),auth,pendingSelection.waiting())){
            const auto channel=channelOf(&ic,ic.ic_bss->ni_chan);
            authenticationEvents.bind(auth.epoch,auth.operation,identity.interface.address.bytes,
                ic.ic_bss->ni_bssid,channel.primary);
        }
    }
    IOReturn authenticationControl(void *client,uint32_t operation,authevents::Event *out){
        if(!client)return kIOReturnBadArgument;
        if(operation==control::captureEnd)
            return authenticationEvents.disable(client)?kIOReturnSuccess:kIOReturnNotOpen;
        if(!attached||faulted||stopping||!enabled)return kIOReturnNotReady;
        if(operation==control::captureRead)
            return out&&authenticationEvents.read(client,*out)?kIOReturnSuccess:kIOReturnNotOpen;
        if(operation!=control::captureBegin)return kIOReturnUnsupported;
        if(authenticationEvents.owned(client))return kIOReturnSuccess;
        if(!authenticationEvents.enable(client))return kIOReturnExclusiveAccess;
        bindAuthenticationEventsIfReady();
        return kIOReturnSuccess;
    }
    bool activeProbeChannel(uint8_t number)const{
        if(!number||number>11||!isset(ic.ic_chan_active,number))return false;
        const auto &channel=ic.ic_channels[number];
        if(!channel.ic_freq||!IEEE80211_IS_CHAN_2GHZ(&channel)||
           (channel.ic_flags&IEEE80211_CHAN_PASSIVE))return false;
        for(size_t i=0;i<identity.channelCount&&i<sizeof(identity.channels)/sizeof(identity.channels[0]);++i)
            if(identity.channels[i].number==number&&!identity.channels[i].fiveGhz&&
               !identity.channels[i].passive)return true;
        return false;
    }
    bool foregroundProbeContext(foregroundscan::Dwell operation,nativescan::Channel channel){
        return inGate()&&!faulted&&!stopping&&enabled&&!actionInFlight&&
            ic.ic_state==IEEE80211_S_INIT&&station.state()==station::State::scanningDwell&&
            traffic==station::Traffic::scanProbe&&foregroundScan.observing(operation)&&
            station.canSendScanProbe({operation.epoch,operation.operation},now())&&
            foregroundscan::same(operation,{station.scanToken().epoch,station.scanToken().operation})&&
            channel.band==nativescan::Band::ghz2&&activeProbeChannel(channel.number)&&
            activeAction.action==station::Action::scanTune&&activeAction.channel.band==0&&
            activeAction.channel.width==0&&activeAction.channel.primary==channel.number&&
            activeAction.channel.center==channel.number&&now()<foregroundScan.status().deadlineUs;
    }
    void dropForegroundHostProbe(){
        auto *node=foregroundProbe.node();if(!node)return;
        if(ic.ic_mgtq.mq_mtx){
            IORecursiveLockLock(ic.ic_mgtq.mq_mtx);
            const auto head=MBUF_LIST_FIRST(&ic.ic_mgtq.mq_list);
            if(head&&reinterpret_cast<ieee80211_node*>(mbuf_pkthdr_rcvif(head))==node){
                TxLease lease{mq_dequeue(&ic.ic_mgtq),node,0};releaseTx(&ic,lease);
            }
            IORecursiveLockUnlock(ic.ic_mgtq.mq_mtx);
        }
        if(pendingTx.node==node)releaseTx(&ic,pendingTx);
    }
    bool finishForegroundProbe(foregroundscan::Dwell operation,bool successRequired){
        if(!foregroundScan.status().activeScan)return !foregroundProbe.node();
        bool success=false;
        if(foregroundProbe.node()&&foregroundProbeRing<6&&
           foregroundscan::same(operation,foregroundProbeOperation)&&dataDrained()){
            const auto &after=tx[foregroundProbeRing].completions().counters();
            const auto &before=foregroundProbeBefore;
            // This scan admits no other management lease. Status 0 is a
            // successful broadcast TX report; it does not prove an AP ACK.
            success=scanprobe::exactlyOneSuccessful(before,after);
        }
        dropForegroundHostProbe();ProbeOps ops{*this};
        const bool released=foregroundProbe.releaseOwner(ops);
        if(released){foregroundProbeOperation={};foregroundProbeRing=6;foregroundProbeBefore={};}
        if(successRequired&&success&&released)return foregroundScan.completedProbe(operation);
        return !successRequired&&released;
    }
    void serviceForegroundProbe(){
        const auto operation=foregroundScan.operation();nativescan::Channel channel;
        if(!foregroundScan.pendingProbe(operation,channel)||foregroundProbe.node())return;
        if(!foregroundProbeContext(operation,channel)){
            cancelForeground(foregroundscan::Reason::probe,true);return;
        }
        const auto &current=ic.ic_channels[channel.number];
        const auto mode=(IEEE80211_IS_CHAN_G(&current)||IEEE80211_IS_CHAN_PUREG(&current))?
            IEEE80211_MODE_11G:IEEE80211_MODE_11B;
        const auto &rates=ic.ic_sup_rates[mode];ProbeOps ops{*this};
        foregroundProbeOperation=operation;foregroundProbeRing=6;
        if(!foregroundProbe.queueFrame(ops,channel.number,rates.rs_rates,rates.rs_nrates)){
            dropForegroundHostProbe();foregroundProbe.releaseOwner(ops);
            cancelForeground(foregroundscan::Reason::probe,true);return;
        }
        pumpTx(); // Real preparation/doorbell, checked against this exact dwell.
    }
    void cancelForeground(foregroundscan::Reason reason,bool cancelStation){
        abortWclDraft();
        if(!foregroundScan.active())return;
        const auto timestamp=now();
        foregroundScan.cancel(foregroundScan.token(),reason,timestamp);
        dropForegroundHostProbe();
        if(scanObservations)scanObservations->cancel();
        if(cancelStation&&foregroundScan.draining()){
            const auto operation=foregroundScan.operation();
            if(!station.cancelScan({operation.epoch,operation.operation},timestamp))
                fail("foreground scan cancellation rejected");
        }
    }
    bool submitForegroundChannel(){
        nativescan::Channel target;
        if(!foregroundScan.next(target)||station.state()!=station::State::idle||
           actionInFlight||!dataDrained())return false;
        station::ScanChannel request{};
        request.channel={uint8_t(target.band==nativescan::Band::ghz5),0,target.number,target.number};
        request.dwellMs=foregroundScan.status().dwellMs;
        request.active=foregroundScan.status().activeScan;
        if(request.active&&!activeProbeChannel(target.number))return false;
        if(!station.scan(&request,1,now())){
            foregroundScan.fail(foregroundscan::Reason::backend,now());
            abortWclDraft();
            if(scanObservations)scanObservations->cancel();return false;
        }
        const auto operation=station.scanToken();
        if(!foregroundScan.accepted({operation.epoch,operation.operation})){
            fail("foreground scan ownership mismatch");return false;
        }
        if(!scanObservations||!scanObservations->channelAccepted(identity.epoch,
                foregroundscan::observerMode,request.active,target,{operation.epoch,operation.operation})){
            cancelForeground(foregroundscan::Reason::observation,true);return false;
        }
        return true;
    }
    IOReturn beginForeground(bool active,foregroundscan::Status &out,
                            const foregroundscan::RequestedPlan *requested=nullptr){
        memset(&out,0,sizeof(out));
        nativescan::Channel plan[nativescan::maxChannels]{};size_t count=0;
        foregroundscan::Readiness readiness{
            attached&&stationStarted&&enabled&&!faulted&&!stopping&&scanObservations,
            station.state()==station::State::idle,ic.ic_state==IEEE80211_S_INIT,
            station.associated()||ic.ic_state==IEEE80211_S_RUN,pendingSelection.waiting(),
            credentials||(ic.ic_flags&IEEE80211_F_AUTO_JOIN),
            actionInFlight||actionDeferred||completedCount||stateDeferred||resetPending||
                authPending||probePending||runPending||scanDone||!dataDrained()||
                (scanObservations&&scanObservations->open())||foregroundProbe.node()||
                (attached&&!mq_empty(&ic.ic_mgtq))};
        // Snapshot the intersection of actual boot policy and current net80211
        // allowed channels. No capability-derived channels, mode changes,
        // power changes or credential changes are introduced.
        for(size_t i=0;i<identity.channelCount&&i<nativescan::maxChannels;++i){
            const auto &allowed=identity.channels[i];
            if(!allowed.number||!isset(ic.ic_chan_active,allowed.number))continue;
            const auto &channel=ic.ic_channels[allowed.number];
            if(!channel.ic_freq||!channel.ic_flags||
               bool(IEEE80211_IS_CHAN_5GHZ(&channel))!=allowed.fiveGhz)continue;
            plan[count++]={allowed.fiveGhz?nativescan::Band::ghz5:nativescan::Band::ghz2,allowed.number};
        }
        uint16_t requestedDwell=foregroundscan::dwellMs;
        if(requested){
            if(active!=requested->active||!requested->channelCount||
               requested->channelCount>sizeof(requested->channels)||
               requested->dwellMs<10||requested->dwellMs>1000)return kIOReturnBadArgument;
            nativescan::Channel selected[sizeof(requested->channels)]{};
            for(size_t i=0;i<requested->channelCount;++i){
                const uint8_t number=requested->channels[i];
                if(number<1||number>11)return kIOReturnBadArgument;
                for(size_t j=0;j<i;++j)
                    if(requested->channels[j]==number)return kIOReturnBadArgument;
                bool found=false;
                for(size_t j=0;j<count;++j)
                    if(plan[j].band==nativescan::Band::ghz2&&plan[j].number==number){
                        selected[i]=plan[j];found=true;break;
                    }
                if(!found)return kIOReturnUnsupported;
            }
            count=requested->channelCount;
            for(size_t i=0;i<count;++i)plan[i]=selected[i];
            requestedDwell=requested->dwellMs;
        }
        readiness.activeAllowed=true;
        for(size_t i=0;i<count;++i)
            if(plan[i].band!=nativescan::Band::ghz2||!activeProbeChannel(plan[i].number))
                readiness.activeAllowed=false;
        foregroundscan::Status accepted;
        const auto admitted=foregroundScan.begin(readiness,active,identity.epoch,now(),
                                                  plan,count,requestedDwell,accepted);
        switch(admitted){
        case foregroundscan::Admission::busy:return kIOReturnBusy;
        case foregroundscan::Admission::notReady:return kIOReturnNotReady;
        case foregroundscan::Admission::unsupported:return kIOReturnUnsupported;
        case foregroundscan::Admission::invalid:return kIOReturnBadArgument;
        case foregroundscan::Admission::accepted:break;
        }
        // Admission replaces the scan-cache generation, even when later
        // hardware startup fails. No old draft may survive this boundary.
        supersedeWclDraft();
        if(!scanObservations->begin(identity.epoch,foregroundscan::observerMode,active,plan,count)){
            scanObservations->cancel();foregroundScan.fail(foregroundscan::Reason::observation,now());
            return kIOReturnError;
        }
        // Do not return success for a request that never reached station.scan.
        if(!submitForegroundChannel()){
            if(foregroundScan.active()&&!foregroundScan.draining())
                cancelForeground(foregroundscan::Reason::backend,true);
            return kIOReturnError;
        }
        out=foregroundScan.status();return kIOReturnSuccess;
    }
    IOReturn beginWclScan(const void *ownedMessage,size_t length,bool profileVerified,
                         foregroundscan::Status &out){
        memset(&out,0,sizeof(out));
        if(!profileVerified)return kIOReturnUnsupported;
        if(!ownedMessage||length!=nativewclscan::messageBytes)return kIOReturnBadArgument;
        nativewclscan::Policy policy{};
        policy.profile=nativewclscan::TargetProfile::darwin24_4_0_d8b50fc2;
        // This driver has no private scan-MAC generation facility. Apple has
        // an explicit capability-disabled no-op for request byte +4.
        policy.privateMac=nativewclscan::PrivateMacPolicy::unsupported;
        for(size_t i=0;i<identity.channelCount&&i<sizeof(identity.channels)/sizeof(identity.channels[0]);++i){
            const auto &allowed=identity.channels[i];
            if(allowed.fiveGhz||allowed.number<1||allowed.number>11||
               !isset(ic.ic_chan_active,allowed.number))continue;
            const auto &channel=ic.ic_channels[allowed.number];
            if(!channel.ic_freq||!IEEE80211_IS_CHAN_2GHZ(&channel))continue;
            const auto bit=uint16_t(1u<<(allowed.number-1));
            policy.permittedPassiveChannels|=bit;
            if(activeProbeChannel(allowed.number))policy.permittedActiveChannels|=bit;
        }
        nativewclscan::Request decoded{};
        const auto parsed=nativewclscan::decode(ownedMessage,length,policy,decoded);
        if(parsed.status!=nativewclscan::Status::knownSubset)return kIOReturnUnsupported;
        foregroundscan::RequestedPlan plan{};
        const auto mapped=nativewclscanplan::map(parsed,decoded,plan);
        if(mapped.status!=nativewclscanplan::Status::planned){
            bzero(&decoded,sizeof(decoded));return kIOReturnUnsupported;
        }
        // Optional native drafting must not consume boot memory or change the
        // existing WPA2/Ethernet path when no WCL request was admitted.
        if(!wclResults)wclResults=new nativewclresults::Bridge;
        if(!wclResults){bzero(&decoded,sizeof(decoded));return kIOReturnNoMemory;}
        const auto result=beginForeground(plan.active,out,&plan);
        if(result==kIOReturnSuccess){
            wclRequest=decoded;wclRequestToken=out.token;wclRequestValid=true;
        }
        bzero(&decoded,sizeof(decoded));
        return result;
    }
    void advanceForeground(){
        if(!foregroundScan.active()||foregroundScan.draining()||foregroundCompletedThisPoll)return;
        const auto timestamp=now();
        if(!foregroundScan.clockValid(timestamp)){fail("foreground scan clock regressed");return;}
        if(foregroundScan.expired(timestamp)){
            expireForegroundHardware();return;
        }
        if(foregroundScan.readyToFinish()){
            nativescan::Summary snapshot;
            const bool complete=scanObservations&&scanObservations->finish(identity.epoch,
                foregroundscan::observerMode,foregroundScan.status().activeScan,timestamp)&&scanObservations->copySummary(snapshot);
            if(!complete||!foregroundScan.complete(snapshot.token,timestamp)){
                foregroundScan.fail(foregroundscan::Reason::observation,timestamp);
                abortWclDraft();
                if(scanObservations)scanObservations->cancel();
            }
            return;
        }
        nativescan::Channel next;
        if(foregroundScan.next(next)&&!submitForegroundChannel()&&!faulted)
            cancelForeground(foregroundscan::Reason::backend,true);
    }
    void expireForegroundHardware(){
        // expired() may already be terminal between channels, so cleanup cannot
        // depend on active()==true. Preserve the previous completed snapshot.
        abortWclDraft();
        if(scanObservations)scanObservations->cancel();
        if(foregroundScan.draining()){
            const auto operation=foregroundScan.operation();
            if(!station.cancelScan({operation.epoch,operation.operation},now()))
                fail("foreground scan timeout cancellation rejected");
        }
    }
    IOReturn queueSelection(const selection::Join *requested){
        if(!attached||!stationStarted||faulted||stopping||!enabled)return kIOReturnNotReady;
        if(requested){
            if(!selection::valid(*requested))return kIOReturnBadArgument;
            if(!pendingSelection.submit(*requested))return kIOReturnBusy;
        }else pendingSelection.clear();
        cancelForeground(foregroundscan::Reason::selection,false);
        if(scanObservations)scanObservations->cancel();
        // Keep the old protocol keys until all old-channel TX and hardware work
        // has drained. Never install the new credentials from this call stack.
        if(!station.disconnect(now())){pendingSelection.clear();return kIOReturnBusy;}
        credentials=false;ic.ic_flags&=~IEEE80211_F_AUTO_JOIN;
        stateDeferred=true;deferredState=IEEE80211_S_INIT;deferredArgument=-1;
        authPending=probePending=runPending=scanDone=false;
        owner.setLinkStatus(kIONetworkLinkValid);
        return kIOReturnSuccess;
    }
    bool applyPendingSelection(){
        selection::Join selected{};
        if(!pendingSelection.take(station.state()==station::State::idle&&ic.ic_state==IEEE80211_S_INIT,
                                  dataDrained(),actionInFlight||stateDeferred||resetPending,selected))return true;
        ieee80211_del_ess(&ic,nullptr,0,1);ieee80211_deselect_ess(&ic);
        ieee80211_disable_rsn(&ic);ieee80211_disable_wep(&ic);
        selection::wipe(ic.ic_psk,sizeof(ic.ic_psk));
        ieee80211_join join;fillJoin(selected,join);
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
        // Count-only diagnostics for the unattended recovery collector. Never
        // publish network names, addresses, raw IEs or credentials here.
        nativescan::Summary nativeScan{};
        const bool nativeScanComplete=scanObservations&&scanObservations->copySummary(nativeScan);
        owner.pci_->setProperty("R16NativeScanObserver",uint64_t(scanObservations!=nullptr),64);
        owner.pci_->setProperty("R16NativeScanPassOpen",uint64_t(scanObservations&&scanObservations->open()),64);
        owner.pci_->setProperty("R16NativeScanComplete",uint64_t(nativeScanComplete),64);
        owner.pci_->setProperty("R16NativeScanGeneration",nativeScan.token.generation,64);
        owner.pci_->setProperty("R16NativeScanEpoch",nativeScan.token.epoch,64);
        owner.pci_->setProperty("R16NativeScanCount",uint64_t(nativeScan.count),64);
        owner.pci_->setProperty("R16NativeScanChannels",uint64_t(nativeScan.channelCount),64);
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
        foregroundCompletedThisPoll=false;
        const auto timestamp=now();
        if(!foregroundScan.clockValid(timestamp)){fail("foreground scan clock regressed");return;}
        if(foregroundScan.expired(timestamp)){expireForegroundHardware();if(faulted)return;}
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
        serviceForegroundProbe();if(faulted)return;
        if(resetPending&&dataDrained()){resetPending=false;savedState(&ic,IEEE80211_S_INIT,-1);}
        if(stateDeferred&&!resetPending&&dataDrained()&&station.state()==station::State::idle){
            stateDeferred=false;const auto next=static_cast<enum ieee80211_state>(deferredState);
            if(next==IEEE80211_S_INIT)savedState(&ic,next,deferredArgument);
            else newState(&ic,next,deferredArgument);
        }
        if(authPending){authPending=false;savedState(&ic,IEEE80211_S_AUTH,-1);}
        if(probePending){probePending=false;savedState(&ic,IEEE80211_S_SCAN,-1);}
        if(scanDone){scanDone=false;if(!foregroundScan.active())ieee80211_next_scan(&ic.ic_if);}
        if(!applyPendingSelection()){fail("network selection failed");return;}
        if(runPending&&station.state()==station::State::associated){
            runPending=false;if(!savedState(&ic,IEEE80211_S_RUN,-1)&&ic.ic_state==IEEE80211_S_RUN)++runCommitted;
        }
        if(enabled&&credentials&&!foregroundScan.active()&&!foregroundCompletedThisPoll&&
           station.state()==station::State::idle&&ic.ic_state==IEEE80211_S_INIT&&!stateDeferred)
            newState(&ic,IEEE80211_S_SCAN,-1);
        if(ic.ic_state==IEEE80211_S_RUN&&station.state()==station::State::associated&&
           (!(ic.ic_flags&IEEE80211_F_RSNON)||ic.ic_bss->ni_port_valid)){
            if(!station.authorizePort(auth,now())){fail("controlled port authorization failed");return;}
            ++portAuthorizations;owner.setLinkStatus(kIONetworkLinkValid|kIONetworkLinkActive);
        }
        if(station.state()==station::State::authorized&&(ic.ic_flags&IEEE80211_F_RSNON)&&!ic.ic_bss->ni_port_valid)
            station.revokePort(auth,now());
        advanceForeground();if(faulted)return;
        if(attached&&timestamp-lastWatchdog>=1000000){lastWatchdog=timestamp;ieee80211_watchdog(&ic.ic_if);}
        bindAuthenticationEventsIfReady();pumpTx();
        if(!faulted&&owner.timer_->setTimeoutMS(10)!=kIOReturnSuccess)fail("controller timer failed");
    }
    bool shutdown(){
        abortWclDraft();
        cancelForeground(foregroundscan::Reason::shutdown,false);
        dropForegroundHostProbe();
        if(scanObservations)scanObservations->cancel();
        authenticationEvents.disable();
        stopping=true;enabled=false;traffic=station::Traffic::none;owner.timer_->cancelTimeout();
        pendingSelection.clear();
        owner.setLinkStatus(kIONetworkLinkValid);
        if(commands)commands->invalidate();
        // Disable the source first. No memory is released while a callback borrows it.
        bool idle=interruptAttached?interrupt->stop():true;
        if(visible&&(!runtime.result.stopped))idle=runtime.stop()&&idle;
        const bool backendStopped=boot->stop();
        if(!idle||!backendStopped||(queues&&queues->serving())||(interrupt&&interrupt->servicing()))return false;
        foregroundScan.hardwareStopped(now());
        if(interruptAttached&&!interrupt->detach())return false;interruptAttached=false;
        if(interrupt){interrupt->release();interrupt=nullptr;}
        if(pendingTx.frame)releaseTx(&ic,pendingTx);
        bool released=true;
        for(auto &q:tx)released=q.releaseAfterDmaStopped()&&released;
        released=firmware.releaseAfterDmaStopped()&&released;
        released=rxq.releaseAfterDmaStopped()&&rpq.releaseAfterDmaStopped()&&released;
        if(!released)return false;
        ProbeOps probeOps{*this};if(!foregroundProbe.releaseOwner(probeOps))return false;
        if(attached){ic.ic_if.if_flags&=~IFF_RUNNING;ic.ic_newstate=savedState;
            ic.ic_event_handler=savedEvent;
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
    controlLock_=IOLockAlloc();if(!controlLock_)goto failed;
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
    IOLockLock(controlLock_);controlStopping_=false;IOLockUnlock(controlLock_);
    setLinkStatus(kIONetworkLinkValid);registerService();interface_->registerService();
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
    else{s->cancelForeground(foregroundscan::Reason::disabled,false);
        if(s->scanObservations)s->scanObservations->cancel();
        s->authenticationEvents.disable();s->pendingSelection.clear();s->ic.ic_if.if_flags&=~(IFF_UP|IFF_RUNNING);s->stateDeferred=true;s->deferredState=IEEE80211_S_INIT;s->deferredArgument=-1;
        if(s->stationStarted)s->station.disconnect(s->now());
        owner.setLinkStatus(kIONetworkLinkValid);}
    return kIOReturnSuccess;
}
IOReturn R16NetworkController::enable(IONetworkInterface*){return gate_?gate_->runAction(enableGated,this):kIOReturnNotReady;}
IOReturn R16NetworkController::disable(IONetworkInterface*){return gate_?gate_->runAction(enableGated):kIOReturnNotReady;}
IOReturn R16NetworkController::selectionGated(OSObject *o,void *request,void*,void*,void*){
    auto *s=static_cast<R16NetworkController*>(o)->state_;
    return s?s->queueSelection(static_cast<const selection::Join*>(request)):kIOReturnNotReady;
}
IOReturn R16NetworkController::selectWirelessNetwork(const selection::Join &request){
    return runControlAction(selectionGated,const_cast<selection::Join*>(&request));
}
IOReturn R16NetworkController::disconnectWirelessNetwork(){return runControlAction(selectionGated);}
IOReturn R16NetworkController::wirelessStatusGated(OSObject *o,void *output,void*,void*,void*){
    auto *self=static_cast<R16NetworkController*>(o);
    if(!output)return kIOReturnBadArgument;
    return self->state_?self->state_->copyWirelessStatus(*static_cast<wireless::Snapshot*>(output)):kIOReturnNotReady;
}
IOReturn R16NetworkController::copyWirelessStatus(wireless::Snapshot &out){
    memset(&out,0,sizeof(out));
    return runControlAction(wirelessStatusGated,&out);
}
IOReturn R16NetworkController::linkPublicationGated(OSObject *o,void *output,void*,void*,void*){
    if(!output)return kIOReturnBadArgument;
    auto *state=static_cast<R16NetworkController*>(o)->state_;
    if(!state||!state->linkPublicationValid||!state->linkPublication.revision)return kIOReturnNotReady;
    *static_cast<MacLinkPublication*>(output)=state->linkPublication;
    return kIOReturnSuccess;
}
IOReturn R16NetworkController::copyLinkPublication(MacLinkPublication &out){
    out={};return runControlAction(linkPublicationGated,&out);
}
namespace {
struct AuthenticationRequest {void *client;uint32_t operation;authevents::Event *output;};
struct NativeScanRequest {unsigned operation; nativescan::Token token;size_t index;void *output;};
struct ForegroundScanRequest {
    unsigned operation;bool active;foregroundscan::Token token;
    const foregroundscan::RequestedPlan *plan;foregroundscan::Status *output;
};
struct WclScanRequest {
    const void *message;size_t length;bool profileVerified;
    foregroundscan::Status *output;
};
struct WclResultsRequest {
    unsigned operation;foregroundscan::Token token{};
    bool profileVerified{},accepted{};
    void *buffer{};size_t capacity{};
    nativewclresults::Frame frame{};
    nativewclresults::Frame *output{};
};
IOReturn wclResultsCode(nativewclresults::Status status){
    using nativewclresults::Status;
    switch(status){
    case Status::ready:case Status::frameReady:return kIOReturnSuccess;
    case Status::busy:return kIOReturnBusy;
    case Status::unsupportedProfile:return kIOReturnUnsupported;
    case Status::stale:return kIOReturnNotFound;
    case Status::invalidSnapshot:case Status::aborted:case Status::finished:
        return kIOReturnNotReady;
    case Status::invalidRequest:case Status::invalidEntry:
    case Status::invalidBuffer:case Status::bufferTooSmall:return kIOReturnBadArgument;
    }
    return kIOReturnError;
}
struct LinkStatusRequest {
    UInt32 status;const IONetworkMedium *medium;UInt64 speed;OSData *data;bool applied{};
};
}
IOReturn R16NetworkController::nativeScanGated(OSObject *owner,void *arg,void*,void*,void*){
    if(!arg)return kIOReturnBadArgument;
    const auto &request=*static_cast<NativeScanRequest*>(arg);
    auto *state=static_cast<R16NetworkController*>(owner)->state_;
    if(!state||state->stopping||!state->scanObservations||!request.output)return kIOReturnNotReady;
    auto &observer=*state->scanObservations;
    switch(request.operation){
    case 0:return observer.copySummary(*static_cast<nativescan::Summary*>(request.output))?kIOReturnSuccess:kIOReturnNotReady;
    case 1:return observer.copyEntry(request.token,request.index,*static_cast<nativescan::Entry*>(request.output))?kIOReturnSuccess:kIOReturnNotFound;
    case 2:return observer.copyChannel(request.token,request.index,*static_cast<nativescan::Channel*>(request.output))?kIOReturnSuccess:kIOReturnNotFound;
    default:return kIOReturnBadArgument;
    }
}
IOReturn R16NetworkController::copyNativeScanSummary(nativescan::Summary &output){
    memset(&output,0,sizeof(output));NativeScanRequest request{0,{},0,&output};
    return runControlAction(nativeScanGated,&request);
}
IOReturn R16NetworkController::copyNativeScanEntry(nativescan::Token token,size_t index,nativescan::Entry &output){
    memset(&output,0,sizeof(output));NativeScanRequest request{1,token,index,&output};
    return runControlAction(nativeScanGated,&request);
}
IOReturn R16NetworkController::copyNativeScanChannel(nativescan::Token token,size_t index,nativescan::Channel &output){
    memset(&output,0,sizeof(output));NativeScanRequest request{2,token,index,&output};
    return runControlAction(nativeScanGated,&request);
}
IOReturn R16NetworkController::foregroundScanGated(OSObject *owner,void *argument,void*,void*,void*){
    if(!argument)return kIOReturnBadArgument;
    const auto &request=*static_cast<ForegroundScanRequest*>(argument);
    if(!request.output)return kIOReturnBadArgument;
    auto *state=static_cast<R16NetworkController*>(owner)->state_;
    if(!state||state->stopping)return kIOReturnNotReady;
    if(request.operation==0)return state->beginForeground(request.active,*request.output,request.plan);
    if(request.operation==1)return state->foregroundScan.copy(request.token,*request.output)?
        kIOReturnSuccess:kIOReturnNotFound;
    if(request.operation!=2)return kIOReturnBadArgument;
    if(!foregroundscan::same(request.token,state->foregroundScan.token())||
       !state->foregroundScan.active())return kIOReturnNotFound;
    state->cancelForeground(foregroundscan::Reason::caller,true);
    if(state->faulted)return kIOReturnError;
    return state->foregroundScan.copy(request.token,*request.output)?kIOReturnSuccess:kIOReturnNotFound;
}
IOReturn R16NetworkController::wclScanGated(OSObject *owner,void *argument,void*,void*,void*){
    if(!argument)return kIOReturnBadArgument;
    const auto &request=*static_cast<WclScanRequest*>(argument);
    if(!request.output)return kIOReturnBadArgument;
    auto *state=static_cast<R16NetworkController*>(owner)->state_;
    if(!state||state->stopping)return kIOReturnNotReady;
    return state->beginWclScan(request.message,request.length,
                               request.profileVerified,*request.output);
}
IOReturn R16NetworkController::wclResultsGated(OSObject *owner,void *argument,void*,void*,void*){
    if(!argument)return kIOReturnBadArgument;
    auto &request=*static_cast<WclResultsRequest*>(argument);
    auto *state=static_cast<R16NetworkController*>(owner)->state_;
    if(!state||state->stopping||state->faulted||!state->scanObservations||!state->wclResults)
        return kIOReturnNotReady;
    if(!request.token.request||!foregroundscan::same(request.token,state->wclRequestToken))
        return kIOReturnNotFound;
    auto &bridge=*state->wclResults;
    const auto &scan=state->foregroundScan.status();
    const auto &store=state->scanObservations->completedStoreUnderGate();
    switch(request.operation){
    case 0:{
        if(!request.profileVerified||!state->wclRequestValid)return kIOReturnUnsupported;
        const auto status=bridge.begin(nativewclbeacon::TargetProfile::darwin24_4_0_d8b50fc2,
            true,state->wclRequest,scan,store,request.buffer,request.capacity);
        return wclResultsCode(status);
    }
    case 1:{
        if(!state->wclRequestValid||!request.output)return kIOReturnNotReady;
        // Frame and payload must not alias any mutable controller object. The
        // bridge also checks the borrowed Store and foreground Status itself.
        auto *controller=static_cast<R16NetworkController*>(owner);
        const auto aliasesController=[&](const void *p,size_t n){
            return nativewclbeacon::detail::overlaps(p,n,state,sizeof(*state))||
                nativewclbeacon::detail::overlaps(p,n,controller,sizeof(*controller))||
                nativewclbeacon::detail::overlaps(p,n,state->scanObservations,
                                                 sizeof(*state->scanObservations))||
                nativewclbeacon::detail::overlaps(p,n,state->wclResults,
                                                 sizeof(*state->wclResults));
        };
        const auto writable=nativewclbeacon::detail::writableBytes(request.capacity);
        if(aliasesController(request.output,sizeof(*request.output))||
           (request.buffer&&aliasesController(request.buffer,writable)))
            return kIOReturnBadArgument;
        return wclResultsCode(bridge.reserve(scan,store,request.buffer,request.capacity,
                                             *request.output));
    }
    case 2:{
        // No IO80211 event sender has been proven. A caller cannot turn an
        // offline draft into a purported native-menu delivery by asserting true.
        // Check the exact reservation before aborting; an old ACK for the same
        // scan token must not cancel a newer reserved frame.
        const bool reserved=bridge.phase()==nativewclresults::Phase::reserved;
        bridge.commit(request.frame,false,scan,store);
        return reserved&&bridge.phase()==nativewclresults::Phase::aborted?
            (request.accepted?kIOReturnUnsupported:kIOReturnSuccess):kIOReturnNotFound;
    }
    case 3:
        if(bridge.phase()==nativewclresults::Phase::idle){
            // A cancelled request can have no draft to retire. Clearing its
            // orphaned token must still be possible without a later scan.
            if(state->wclRequestValid&&state->foregroundScan.active())return kIOReturnBusy;
        }else{
            // Explicit abandonment of an undelivered draft is safe: no event
            // sender exists, and a late commit remains token/phase rejected.
            bridge.abort();
            if(!bridge.retire())return kIOReturnBusy;
        }
        state->wclRequestToken={};state->wclRequestValid=false;
        bzero(&state->wclRequest,sizeof(state->wclRequest));
        return kIOReturnSuccess;
    default:return kIOReturnBadArgument;
    }
}
IOReturn R16NetworkController::beginNativeForegroundScan(bool active,foregroundscan::Status &output){
    memset(&output,0,sizeof(output));ForegroundScanRequest request{0,active,{},nullptr,&output};
    return runControlAction(foregroundScanGated,&request);
}
IOReturn R16NetworkController::beginNativePlannedForegroundScan(
    const foregroundscan::RequestedPlan &input,foregroundscan::Status &output){
    const foregroundscan::RequestedPlan copied=input;
    memset(&output,0,sizeof(output));
    ForegroundScanRequest request{0,copied.active,{},&copied,&output};
    return runControlAction(foregroundScanGated,&request);
}
IOReturn R16NetworkController::beginNativeWclScanRequest(const void *message,size_t length,
    bool exactKernelProfileVerified,foregroundscan::Status &output){
    if(!exactKernelProfileVerified){memset(&output,0,sizeof(output));return kIOReturnUnsupported;}
    if(!message||length!=nativewclscan::messageBytes){memset(&output,0,sizeof(output));return kIOReturnBadArgument;}
    // The gate may wait while the framework-owned caller buffer disappears.
    // A separate, fixed-size heap copy keeps every decoder read owned here.
    auto *copy=static_cast<uint8_t*>(IOMalloc(nativewclscan::messageBytes));
    if(!copy){memset(&output,0,sizeof(output));return kIOReturnNoMemory;}
    memcpy(copy,message,nativewclscan::messageBytes);
    // A caller may place its output inside the readable request allocation.
    // Clear only after the independent request copy has been made.
    memset(&output,0,sizeof(output));
    WclScanRequest request{copy,nativewclscan::messageBytes,true,&output};
    const auto result=runControlAction(wclScanGated,&request);
    bzero(copy,nativewclscan::messageBytes);
    IOFree(copy,nativewclscan::messageBytes);
    return result;
}
IOReturn R16NetworkController::armNativeWclScanResults(foregroundscan::Token token,
    bool exactKernelProfileVerified){
    if(!exactKernelProfileVerified)return kIOReturnUnsupported;
    // The encoder's 2112-byte scratch belongs to this call, never the kernel
    // stack, a WCL input allocation, or the observer's cache banks.
    auto *scratch=static_cast<uint8_t*>(IOMalloc(nativewclbeacon::maxPayloadBytes));
    if(!scratch)return kIOReturnNoMemory;
    WclResultsRequest request{};request.operation=0;request.token=token;
    request.profileVerified=true;request.buffer=scratch;
    request.capacity=nativewclbeacon::maxPayloadBytes;
    const auto result=runControlAction(wclResultsGated,&request);
    bzero(scratch,nativewclbeacon::maxPayloadBytes);
    IOFree(scratch,nativewclbeacon::maxPayloadBytes);
    return result;
}
IOReturn R16NetworkController::reserveNativeWclScanResult(foregroundscan::Token token,
    void *buffer,size_t capacity,nativewclresults::Frame &output){
    // Preserve the bridge's alias checks: zeroing either caller allocation
    // here would corrupt borrowed state before the gate can reject an alias.
    // On failure the caller must disregard prior buffer/Frame contents.
    if(buffer&&nativewclbeacon::detail::overlaps(
        &output,sizeof(output),buffer,capacity?capacity:1))return kIOReturnBadArgument;
    WclResultsRequest request{};request.operation=1;request.token=token;
    request.buffer=buffer;request.capacity=capacity;request.output=&output;
    return runControlAction(wclResultsGated,&request);
}
IOReturn R16NetworkController::commitNativeWclScanResult(
    const nativewclresults::Frame &frame,bool accepted){
    WclResultsRequest request{};request.operation=2;request.token=frame.request;
    request.frame=frame;request.accepted=accepted;
    return runControlAction(wclResultsGated,&request);
}
IOReturn R16NetworkController::retireNativeWclScanResults(foregroundscan::Token token){
    WclResultsRequest request{};request.operation=3;request.token=token;
    return runControlAction(wclResultsGated,&request);
}
IOReturn R16NetworkController::copyNativeForegroundScanStatus(foregroundscan::Token token,foregroundscan::Status &output){
    memset(&output,0,sizeof(output));ForegroundScanRequest request{1,false,token,nullptr,&output};
    return runControlAction(foregroundScanGated,&request);
}
IOReturn R16NetworkController::cancelNativeForegroundScan(foregroundscan::Token token,foregroundscan::Status &output){
    memset(&output,0,sizeof(output));ForegroundScanRequest request{2,false,token,nullptr,&output};
    return runControlAction(foregroundScanGated,&request);
}
IOReturn R16NetworkController::authenticationGated(OSObject *owner,void *arg,void*,void*,void*){
    const auto &request=*static_cast<AuthenticationRequest*>(arg);
    auto *state=static_cast<R16NetworkController*>(owner)->state_;
    return state?state->authenticationControl(request.client,request.operation,request.output):kIOReturnNotReady;
}
IOReturn R16NetworkController::authenticationEvents(void *client,uint32_t operation,authevents::Event *output){
    if(output)memset(output,0,sizeof(*output));
    AuthenticationRequest request{client,operation,output};return runControlAction(authenticationGated,&request);
}
IOReturn R16NetworkController::runControlAction(IOCommandGate::Action action,void *argument){
    // External control callers must not hold the hardware workloop gate: lock
    // order is controlLock -> command gate, including stop's request drain.
    if(!controlLock_||!loop_||loop_->inGate())return kIOReturnNotReady;
    IOLockLock(controlLock_);
    const auto result=!controlStopping_&&gate_?gate_->runAction(action,argument):kIOReturnNotReady;
    IOLockUnlock(controlLock_);return result;
}
void R16NetworkController::blockControlRequests(){
    if(controlLock_){IOLockLock(controlLock_);controlStopping_=true;IOLockUnlock(controlLock_);}
}
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
bool R16NetworkController::applyLinkStatus(UInt32 status,const IONetworkMedium *medium,UInt64 speed,OSData *data){
    if(!state_||state_->traffic!=station::Traffic::authorized)status&=~kIONetworkLinkActive;
    const bool applied=IOEthernetController::setLinkStatus(status,medium,speed,data);
    if(state_){
        state_->linkPublicationValid=applied;
        state_->linkPublication.record(applied,status,state_->identity.epoch,
            state_->pendingSelection.generation(),state_->now());
    }
    return applied;
}
IOReturn R16NetworkController::linkStatusGated(OSObject *o,void *argument,void*,void*,void*){
    auto *owner=static_cast<R16NetworkController*>(o);
    if(!argument||!owner->state_)return kIOReturnNotReady;
    auto &request=*static_cast<LinkStatusRequest*>(argument);
    request.applied=owner->applyLinkStatus(request.status,request.medium,request.speed,request.data);
    return kIOReturnSuccess;
}
bool R16NetworkController::setLinkStatus(UInt32 status,const IONetworkMedium *medium,UInt64 speed,OSData *data){
    // Preserve superclass initialization/teardown calls before/after State exists.
    // They can only publish an inactive link and have no observer snapshot.
    if(!state_)return applyLinkStatus(status,medium,speed,data);
    // Internal transitions already own the gate (including shutdown after the
    // external-call fence closes). Never invert controlLock -> gate here.
    if(loop_&&loop_->inGate())return applyLinkStatus(status,medium,speed,data);
    LinkStatusRequest request{status,medium,speed,data};
    return runControlAction(linkStatusGated,&request)==kIOReturnSuccess&&request.applied;
}
void R16NetworkController::releaseResources(){
    // Drain in-flight control calls before dismantling gate/state; reject new
    // calls even if a userspace client retains the stopped provider object.
    blockControlRequests();
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
void R16NetworkController::free(){releaseResources();if(retainedFault_)return;if(loop_){loop_->release();loop_=nullptr;}if(controlLock_){IOLockFree(controlLock_);controlLock_=nullptr;}IOEthernetController::free();}
