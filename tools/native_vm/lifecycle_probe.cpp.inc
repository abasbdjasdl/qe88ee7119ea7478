
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
        if (!PE_parse_boot_argn("r16vmtest", &enabled, sizeof(enabled)) || enabled!=1)
            return false;
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
