
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef R16_VM_LIFECYCLE_EXPERIMENT
#error "Disposable VM experiment only; never add to the hardware driver"
#endif
#include <pexpert/pexpert.h>
#include "native_runtime_identity.hpp"

class R16VMHarness final : public IOService {
    OSDeclareDefaultStructors(R16VMHarness);
#ifdef R16_VM_INFRA_PROBE
    // Retained experiment owner storage; never temporary option/context buffers.
    IOSkywalkPacketBufferPool::PoolOptions poolOptions_{32,32,2048,1,4096,0,nullptr};
    IOSkywalkPacketBufferPool *txPool_{},*rxPool_{};
    IOSkywalkTxSubmissionQueue *txSubmission_{};
    IOSkywalkTxCompletionQueue *txCompletion_{};
    IOSkywalkRxCompletionQueue *rxCompletion_{};
    // Factory tests do not enable queues or bind a radio session. Refuse all
    // traffic without consuming packets or writing unowned completion slots.
    // Invocation is unexpected and is diagnostic evidence, not a successful TX.
    static unsigned closedTx(OSObject*,IOSkywalkTxSubmissionQueue*,IOSkywalkPacket *const*,unsigned count,void*) {
        IOLog("R16VM disabled TX callback offered=%u consumed=0\n",count);return 0;
    }
    static unsigned closedCompletion(OSObject*,IOSkywalkTxCompletionQueue*,IOSkywalkPacket**,unsigned count,void*) {
        IOLog("R16VM disabled TX completion callback capacity=%u produced=0\n",count);return 0;
    }
    static unsigned closedRx(OSObject*,IOSkywalkRxCompletionQueue*,IOSkywalkPacket**,unsigned count,void*) {
        IOLog("R16VM disabled RX callback capacity=%u produced=0\n",count);return 0;
    }
#endif
#ifdef R16_VM_START_PROBE
    // Borrowed from the retained startup ledger. The provider and controller
    // must expose the same native queue: its getThread() deliberately returns
    // null in 24.4, so IONetworkController must not apply thread policy to it.
    IOWorkLoop *sharedWorkQueue_{};
    template<class T> static void releaseOwned(T *&object,const char *name) {
        if(!object)return;
        IOLog("R16VM release owned %s begin refs=%u\n",name,unsigned(object->getRetainCount()));
        T *owned=object;
        object=nullptr;
        owned->release();
        // Do not inspect owned: its final release may have destroyed it.
        IOLog("R16VM release owned %s returned\n",name);
    }
    static bool terminateSupport(IOService *service,const char *name) {
        if(!service)return false;
        IOLog("R16VM terminate %s begin inactive=%u attached=%u refs=%u\n",name,
              unsigned(service->isInactive()),unsigned(service->getProvider()!=nullptr),
              unsigned(service->getRetainCount()));
        const bool accepted=service->terminate(kIOServiceSynchronous);
        const bool detached=service->getProvider()==nullptr;
        IOLog("R16VM terminate %s returned=%u inactive=%u detached=%u refs=%u\n",name,
              unsigned(accepted),unsigned(service->isInactive()),unsigned(detached),
              unsigned(service->getRetainCount()));
        return accepted&&service->isInactive()&&detached;
    }
#endif
public:
#ifdef R16_VM_START_PROBE
    IOWorkLoop *getWorkLoop() const override { return sharedWorkQueue_; }
#endif
    bool start(IOService *provider) override {
        unsigned enabled=0;
        if (!PE_parse_boot_argn("r16vmtest", &enabled, sizeof(enabled)))
            return false;
#ifdef R16_VM_START_PROBE
#ifdef R16_VM_INFRA_PROBE
        if(enabled<1||enabled>8)return false;
#else
        if(enabled<1||enabled>4)return false;
#endif
#else
        if(enabled!=1)return false;
#endif
        const auto identity=r16_native_identity::inspectLoadedComponents();
        IOLog("R16VM identity status=%u component=%lu\n",unsigned(identity.status),identity.component);
        if (identity.status!=r16_native_identity::Status::matched) return false;
        if (!IOService::start(provider)) return false;
        // This harness does not claim PCI, attach a controller to a provider or
        // enter IO80211Controller::start. init/free need their own live evidence.
        IOLog("R16VM controller allocate begin\n");
        auto *controller=new R16VMController;
        if (!controller) {
            IOLog("R16VM allocation failed\n");
            IOService::stop(provider); return false;
        }
        IOLog("R16VM controller init begin\n");
        const bool initialized=controller->init(nullptr);
#ifdef R16_VM_START_PROBE
        if(initialized&&enabled>=2){
            if(!controller->attach(this)){
                IOLog("R16VM controller attach failed\n");
                controller->release();IOService::stop(provider);return false;
            }
            auto *ledger=controller->startupLedger();
            IOLog("R16VM support prepare begin\n");
            const bool prepared=r16_startup_prepare(controller,this,ledger,true);
            IOLog("R16VM support prepared=%u phase=%u failure=%u\n",unsigned(prepared),unsigned(ledger->phase),unsigned(ledger->failure));
            bool started=false;
            if(prepared){
                sharedWorkQueue_=ledger->workQueue;
                const bool sameQueue=getWorkLoop()==controller->getWorkLoop();
                IOLog("R16VM provider/controller same workqueue=%u\n",unsigned(sameQueue));
                if(!sameQueue){
                    IOLog("R16VM refusing base start: provider workqueue mismatch\n");
                    return true; // Retained VM containment, never call unsafe base.
                }
                IOLog("R16VM base start begin\n");
                started=r16_startup_enter_base(ledger);
                IOLog("R16VM base start result=%u phase=%u failure=%u\n",unsigned(started),unsigned(ledger->phase),unsigned(ledger->failure));
            }
            if(started){
#ifdef R16_VM_INFRA_PROBE
                if(enabled>=5){
                    IOLog("R16VM station allocate begin\n");
                    auto *station=new R16VMInfra;
                    const bool initializedStation=station&&station->init();
                    IOLog("R16VM station init result=%u\n",unsigned(initializedStation));
                    bool roleReady=initializedStation;
                    if(initializedStation&&enabled>=6){
                        // Observed native station role; use family-owned setters,
                        // never write private expansion offsets. Ignore setters'
                        // legacy return declarations; read the actual role back.
                        station->setInterfaceRole(1);
                        station->setInterfaceId(0);
                        roleReady=station->getInterfaceRole()==1;
                        IOLog("R16VM station role1 readback=%u\n",unsigned(roleReady));
                    }
                    const bool attached=roleReady&&station->attach(controller);
                    IOLog("R16VM station attach result=%u\n",unsigned(attached));
                    if(attached)IOLog("R16VM station start begin\n");
                    const bool stationStarted=attached&&station->start(controller);
                    IOLog("R16VM station start result=%u\n",unsigned(stationStarted));
                    if(stationStarted&&enabled>=7){
                        // Type 1 is the exact-KC ordinary network-packet path.
                        // No DMA spec, offload, queue enable or registration.
                        IOLog("R16VM TX pool factory begin\n");
                        txPool_=IOSkywalkPacketBufferPool::withName("R16VM-TX",this,1,&poolOptions_);
                        IOLog("R16VM TX pool result=%u\n",unsigned(txPool_!=nullptr));
                        if(txPool_){
                            IOLog("R16VM RX pool factory begin\n");
                            rxPool_=IOSkywalkPacketBufferPool::withName("R16VM-RX",this,1,&poolOptions_);
                            IOLog("R16VM RX pool result=%u\n",unsigned(rxPool_!=nullptr));
                        }
                        setProperty("R16VMPoolsCreated",txPool_&&rxPool_);
                        if(txPool_&&rxPool_&&enabled==8){
                            IOLog("R16VM TX submission factory begin\n");
                            txSubmission_=IOSkywalkTxSubmissionQueue::withPool(txPool_,16,0,this,closedTx,this,0);
                            IOLog("R16VM TX submission result=%u\n",unsigned(txSubmission_!=nullptr));
                            if(txSubmission_){
                                IOLog("R16VM TX completion factory begin\n");
                                txCompletion_=IOSkywalkTxCompletionQueue::withPool(txPool_,16,1,this,closedCompletion,this,0);
                                IOLog("R16VM TX completion result=%u\n",unsigned(txCompletion_!=nullptr));
                            }
                            if(txCompletion_){
                                IOLog("R16VM RX completion factory begin\n");
                                rxCompletion_=IOSkywalkRxCompletionQueue::withPool(rxPool_,16,2,this,closedRx,this,0);
                                IOLog("R16VM RX completion result=%u\n",unsigned(rxCompletion_!=nullptr));
                            }
                            setProperty("R16VMQueuesCreated",txSubmission_&&txCompletion_&&rxCompletion_);
                            IOLog("R16VM queue factory experiment finished; queues disabled, no packet transfer\n");
                        }
                    }
                    // Keep even partially initialized station and all providers
                    // alive. No fake MAC, SSID, pools or hardware are supplied.
                    setProperty("R16VMStationStarted",stationStarted);
                    setProperty("R16VMNativeWiFiVerified",false);
                    IOLog("R16VM station retained; no registration, stop or networking verified\n");
                    registerService();return true;
                }
#endif
                IOLog("R16VM base stop begin\n");
                controller->IO80211Controller::stop(this);
                IOLog("R16VM base stop returned\n");
                if(enabled>=3){
                    // Experiment only: enter through IOKit's termination path,
                    // never manually double-stop/detach a published CC child.
                    // The ledger keeps every object and the workqueue alive.
                    // Stop the consumers before pipes; abort on any deferred or
                    // incomplete result. Other native consumers remain unproved.
                    bool terminated=terminateSupport(ledger->dataStream,"dataStream");
                    if(terminated)terminated=terminateSupport(ledger->logStream,"logStream");
                    if(terminated)terminated=terminateSupport(ledger->dataPipe,"dataPipe");
                    if(terminated)terminated=terminateSupport(ledger->logPipe,"logPipe");
                    setProperty("R16VMCCServicesTerminated",terminated);
                    IOLog("R16VM CC services termination result=%u; references retained\n",unsigned(terminated));
                    if(terminated&&enabled==4){
                        releaseOwned(ledger->wrapper,"faultWrapper");
                        releaseOwned(ledger->reporter,"faultReporter");
                        ledger->faultData=nullptr; ledger->logger=nullptr;
                        releaseOwned(ledger->dataStream,"dataStream");
                        releaseOwned(ledger->logStream,"logStream");
                        releaseOwned(ledger->dataPipe,"dataPipe");
                        releaseOwned(ledger->logPipe,"logPipe");
                        setProperty("R16VMCCOwnedReferencesReturned",true);
                        IOLog("R16VM CC owned references returned; controller and queue retained\n");
                    }
                }
            }
            // Disposable VM containment only. Partial factory/base rollback is
            // not established. Controller, queue and provider stay retained.
            // Stage 4 returns only the explicitly owned CC references after
            // successful termination; earlier stages retain every dependency.
            // There is no
            // physical PCI provider, hardware enable or native station here.
            setProperty("R16VMBaseStartPassed",started);
            setProperty("R16VMBaseStopReturned",started);
            setProperty("R16VMSupportRetained",true);
            setProperty("R16VMFullCleanupVerified",false);
            setProperty("R16VMNativeWiFiVerified",false);
            IOLog("R16VM contained experiment complete; native owner retained, cleanup UNPROVED\n");
            registerService();return true;
        }
#endif
        IOLog("R16VM controller init result=%u; release begin\n",unsigned(initialized));
        controller->release();
        IOLog("R16VM controller release returned\n");
        setProperty("R16VMControllerInitPassed",initialized);
        setProperty("R16VMControllerReleaseReturned",true);
        setProperty("R16VMNativeWiFiVerified",false);
        registerService();
        return true;
    }
    void stop(IOService *provider) override {
        IOLog("R16VM harness stop begin\n");
        IOService::stop(provider);
        IOLog("R16VM harness stop returned\n");
    }
};
OSDefineMetaClassAndStructors(R16VMHarness,IOService);
