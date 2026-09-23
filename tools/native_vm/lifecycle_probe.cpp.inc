
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef R16_VM_LIFECYCLE_EXPERIMENT
#error "Disposable VM experiment only; never add to the hardware driver"
#endif
#include <pexpert/pexpert.h>
#include "native_runtime_identity.hpp"

class R16VMHarness final : public IOService {
    OSDeclareDefaultStructors(R16VMHarness);
public:
    bool start(IOService *provider) override {
        unsigned enabled=0;
        if (!PE_parse_boot_argn("r16vmtest", &enabled, sizeof(enabled)))
            return false;
#ifdef R16_VM_START_PROBE
        if(enabled!=1&&enabled!=2)return false;
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
        if(initialized&&enabled==2){
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
                IOLog("R16VM base start begin\n");
                started=r16_startup_enter_base(ledger);
                IOLog("R16VM base start result=%u phase=%u failure=%u\n",unsigned(started),unsigned(ledger->phase),unsigned(ledger->failure));
            }
            if(started){
                IOLog("R16VM base stop begin\n");
                controller->IO80211Controller::stop(this);
                IOLog("R16VM base stop returned\n");
            }
            // Disposable VM containment only. Partial factory/base rollback is
            // not established. Retain every dependency, including this provider,
            // so no support callback can target freed owner storage. There is no
            // physical PCI provider, hardware enable or native station here.
            setProperty("R16VMBaseStartPassed",started);
            setProperty("R16VMBaseStopReturned",started);
            setProperty("R16VMSupportRetained",true);
            setProperty("R16VMFullCleanupVerified",false);
            setProperty("R16VMNativeWiFiVerified",false);
            IOLog("R16VM contained experiment complete; support retained, cleanup UNPROVED\n");
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
