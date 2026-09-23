// SPDX-License-Identifier: GPL-2.0-or-later
// Exact-KC controller vtable probe only. Never link into a kext or instantiate.
#ifndef R16_NATIVE_ABI_AUDIT_ONLY
#error "The native controller prototype is an offline ABI audit only"
#endif
#ifndef __x86_64__
#error "Only the pinned x86_64 Darwin 24.4 profile is audited"
#endif

#include <Airport/Apple80211.h>

struct StartupLedger;
extern "C" IO80211WorkQueue *r16_startup_work_queue(const StartupLedger *);
extern "C" CCLogStream *r16_startup_logger(const StartupLedger *);
extern "C" IO80211FaultReporter *r16_startup_fault_wrapper(const StartupLedger *);

// This is deliberately not an OSDefineMetaClassAndStructors service. Its only
// owner link is the audit-only StartupLedger pointer; no PCI provider, IOKit
// registration, base start, hardware callback or teardown is possible here.
class R16NativeControllerAudit final : public IO80211Controller {
    StartupLedger *ledger_{};
public:
    R16NativeControllerAudit() = delete;
    ~R16NativeControllerAudit() override;

    IO80211WorkQueue *getWorkQueue() const override {
        return r16_startup_work_queue(ledger_);
    }
    CCLogStream *getLogger() const override {
        return r16_startup_logger(ledger_);
    }
    IO80211FaultReporter *getFaultReporterFromDriver() override {
        return r16_startup_fault_wrapper(ledger_);
    }

    IOReturn isCommandProhibited(int) override { return kIOReturnUnsupported; }
    SInt32 handleCardSpecific(IO80211SkywalkInterface *, unsigned long, void *, bool) override {
        return kIOReturnUnsupported;
    }
    IOReturn getDRIVER_VERSION(IO80211SkywalkInterface *, apple80211_version_data *) override { return kIOReturnUnsupported; }
    IOReturn getHARDWARE_VERSION(IO80211SkywalkInterface *, apple80211_version_data *) override { return kIOReturnUnsupported; }
    IOReturn getCARD_CAPABILITIES(IO80211SkywalkInterface *, apple80211_capability_data *) override { return kIOReturnUnsupported; }
    IOReturn getPOWER(IO80211SkywalkInterface *, apple80211_power_data *) override { return kIOReturnUnsupported; }
    IOReturn setPOWER(IO80211SkywalkInterface *, apple80211_power_data *) override { return kIOReturnUnsupported; }
    IOReturn getCOUNTRY_CODE(IO80211SkywalkInterface *, apple80211_country_code_data *) override { return kIOReturnUnsupported; }
    IOReturn setCOUNTRY_CODE(IO80211SkywalkInterface *, apple80211_country_code_data *) override { return kIOReturnUnsupported; }
    IOReturn setGET_DEBUG_INFO(IO80211SkywalkInterface *, apple80211_debug_command *) override { return kIOReturnUnsupported; }
};

R16NativeControllerAudit::~R16NativeControllerAudit() {}
static_assert(!__is_abstract(R16NativeControllerAudit), "Every controller pure callback must be explicit");
static_assert(__is_same(decltype(((R16NativeControllerAudit *)nullptr)->isCommandProhibited(0)), IOReturn),
              "Controller command-prohibition return must be a 32-bit IOReturn");
