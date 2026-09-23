// SPDX-License-Identifier: GPL-2.0-or-later
// Offline prototype, also included by the explicitly gated disposable VM test.
// Not enabled in the physical driver; complete support teardown is unproved.
// See startup-evidence.md before reusing even a single operation below.
#ifndef R16_NATIVE_ABI_AUDIT_ONLY
#error "This incomplete lifecycle prototype must never enter a driver build"
#endif
#ifndef __x86_64__
#error "Only the pinned x86_64 Darwin 24.4 profile is audited"
#endif
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <libkern/libkern.h>
#include "support_options.hpp"

// Suppress unverified old CC instance/options declarations in the overlay.
// These narrow declarations emit only the audited slots/factories. No instance
// is allocated, sized, subclassed into a loadable object or field-accessed.
#define CCPipe_h 1
#define CCStream_h 1
#define CCLogPipe_h 1
#define CCDataPipe_h 1
#define CCLogStream_h 1
class CCPipe : public IOService {
public:
    // The first three new entries after the pinned IOService vtable precede
    // startPipe. Never call them; their bodies and returns are not audited here.
    virtual bool clientClose() = 0;
    virtual void *getCoreCapturePipeReporter() = 0;
    virtual bool isClientConnected() = 0;
    virtual bool startPipe(); // raw slot 271, object vptr +0x868, bool in AL
    static CCPipe *withOwnerNameCapacity(IOService *,const char *,const char *,const CCPipeOptions *);
};
class CCLogPipe : public CCPipe {public:static const OSMetaClass * const metaClass;};
class CCDataPipe : public CCPipe {public:static const OSMetaClass * const metaClass;};
class CCStream : public IOService {
public:static CCStream *withPipeAndName(CCPipe *,const char *,const CCStreamOptions *);
};
class CCLogStream : public CCStream {public:static const OSMetaClass * const metaClass;};
class CCDataStream : public CCStream {public:static const OSMetaClass * const metaClass;};
enum CCStreamLogLevel {R16StartupUnusedLogLevel};
// Exact KC constructors call IOService::C2 at 31b0bc1 and OSObject::C2
// at 22489e7 respectively. Only inherited public lifetime slots are used.
class CCFaultReporter : public IOService {
public:static CCFaultReporter *withStreamWorkloop(CCDataStream *,IOWorkLoop *);
};
class IO80211FaultReporter : public OSObject {
public:static IO80211FaultReporter *allocWithParams(CCFaultReporter *);
};
#include <Airport/Apple80211.h>
static_assert(__is_same(decltype(&CCPipe::startPipe),bool(CCPipe::*)()),"startPipe returns bool in AL");
static_assert(__is_same(decltype(&IO80211Controller::getLogger),CCLogStream*(IO80211Controller::*)()const),"logger return ABI");
static_assert(__is_same(decltype(&IO80211Controller::getFaultReporterFromDriver),IO80211FaultReporter*(IO80211Controller::*)()),"fault wrapper return ABI");

enum class StartupPhase : unsigned {offline,constructing,supportReady,baseEntered,baseReady,quarantined};
enum class StartupFailure : unsigned {none,workQueue,logPipe,logPipeType,logPipeStart,
    logStream,logStreamType,dataPipe,dataPipeType,dataPipeStart,dataStream,dataStreamType,
    rawReporter,wrapper,getterWiring,baseStart,providerWorkQueue};

struct StartupLedger {
    // Must be zero-initialized OWNER storage, embedded in the real controller
    // or equally durable heap storage. Never a kernel-stack temporary.
    StartupPhase phase{};StartupFailure failure{};
    IO80211Controller *controller{};IOService *provider{}; // one retain each
    IO80211WorkQueue *workQueue{}; // one factory-owned reference
    CCPipe *logPipe{},*dataPipe{};CCStream *logStream{},*dataStream{}; // owning raw results
    CCLogStream *logger{};CCDataStream *faultData{}; // borrowed checked aliases
    CCFaultReporter *reporter{};IO80211FaultReporter *wrapper{}; // owning results
    bool enteredLogPipeStart{},enteredDataPipeStart{};
    CCPipeOptions logPipeOptions{},dataPipeOptions{};
    CCStreamOptions logStreamOptions{},dataStreamOptions{};
};
static_assert(sizeof(StartupLedger)>0xd40,"Full factory records must remain owner storage");
static bool quarantine(StartupLedger &s,StartupFailure why){
    s.failure=why;s.phase=StartupPhase::quarantined;return false;
}
template<size_t N,size_t M> static void ownName(char (&dst)[N],const char (&src)[M]){
    static_assert(M<=N,"literal must fit including NUL");
    for(size_t i=0;i<M;++i)dst[i]=src[i];
}
static void options(StartupLedger &s){
    // Every consumed byte is initialized here, including unknown tail fields.
    bzero(&s.logPipeOptions,sizeof(s.logPipeOptions));bzero(&s.dataPipeOptions,sizeof(s.dataPipeOptions));
    bzero(&s.logStreamOptions,sizeof(s.logStreamOptions));bzero(&s.dataStreamOptions,sizeof(s.dataStreamOptions));
    // Family's callback-free log profile (221d94b..221da55), with OUR names.
    s.logPipeOptions.capacity=0x10000;s.logPipeOptions.notificationAmount=0x6666;
    s.logPipeOptions.notificationThreshold=1000;
    s.logPipeOptions.unknown224=0x200000;s.logPipeOptions.unknown228=2;
    ownName(s.logPipeOptions.fileName,"R16Native");ownName(s.logPipeOptions.name,"R16Native");
    ownName(s.logPipeOptions.directoryName,"R16Native");
    s.logStreamOptions.logLevel=-1;s.logStreamOptions.consoleLevel=-1;
    s.logStreamOptions.unknown150=0x96;ownName(s.logStreamOptions.name,"R16Log");
    // Observed Broadcom data profile minus its driver-specific callback pair.
    s.dataPipeOptions.pipeKind=1;s.dataPipeOptions.logType=2;
    s.dataPipeOptions.logDataType=2;s.dataPipeOptions.capacity=0x80;
    ownName(s.dataPipeOptions.fileName,"R16Fault");ownName(s.dataPipeOptions.name,"R16Fault");
    ownName(s.dataPipeOptions.directoryName,"R16Native");
    s.dataStreamOptions.streamKind=1;s.dataStreamOptions.logLevel=-1;
    s.dataStreamOptions.consoleLevel=-1;ownName(s.dataStreamOptions.name,"R16Fault");
}

// Explicit borrowed getters for the future native owner to wire to its real
// virtual callbacks. No retain is taken by a getter; base start retains wrapper
// separately and only borrows logger. Quarantine keeps these pointers stable.
extern "C" CCLogStream *r16_startup_logger(const StartupLedger *s){return s?s->logger:nullptr;}
extern "C" IO80211FaultReporter *r16_startup_fault_wrapper(const StartupLedger *s){return s?s->wrapper:nullptr;}
extern "C" IO80211WorkQueue *r16_startup_work_queue(const StartupLedger *s){return s?s->workQueue:nullptr;}

extern "C" bool r16_startup_prepare(IO80211Controller *controller,IOService *provider,
                                     StartupLedger *ledger,bool pinnedProfileVerified){
    if(!controller||!provider||!ledger||!pinnedProfileVerified||
       ledger->phase!=StartupPhase::offline||ledger->controller||ledger->provider)return false;
    auto &s=*ledger;
    // Own both BEFORE the first publishing/retaining factory. These references
    // are intentionally retained on every ambiguous failure; never auto-retry.
    controller->retain();provider->retain();s.controller=controller;s.provider=provider;
    s.phase=StartupPhase::constructing;options(s);
    s.workQueue=IO80211WorkQueue::workQueue();
    if(!s.workQueue)return quarantine(s,StartupFailure::workQueue);
    s.logPipe=CCPipe::withOwnerNameCapacity(controller,"R16Native","R16Native",&s.logPipeOptions);
    if(!s.logPipe)return quarantine(s,StartupFailure::logPipe);
    if(!OSDynamicCast(CCLogPipe,s.logPipe))return quarantine(s,StartupFailure::logPipeType);
    s.enteredLogPipeStart=true;
    if(!s.logPipe->startPipe())return quarantine(s,StartupFailure::logPipeStart);
    s.logStream=CCStream::withPipeAndName(s.logPipe,"R16Log",&s.logStreamOptions);
    if(!s.logStream)return quarantine(s,StartupFailure::logStream);
    s.logger=OSDynamicCast(CCLogStream,s.logStream);
    if(!s.logger)return quarantine(s,StartupFailure::logStreamType);
    s.dataPipe=CCPipe::withOwnerNameCapacity(controller,"R16Native","R16Fault",&s.dataPipeOptions);
    if(!s.dataPipe)return quarantine(s,StartupFailure::dataPipe);
    if(!OSDynamicCast(CCDataPipe,s.dataPipe))return quarantine(s,StartupFailure::dataPipeType);
    s.enteredDataPipeStart=true;
    if(!s.dataPipe->startPipe())return quarantine(s,StartupFailure::dataPipeStart);
    s.dataStream=CCStream::withPipeAndName(s.dataPipe,"R16Fault",&s.dataStreamOptions);
    if(!s.dataStream)return quarantine(s,StartupFailure::dataStream);
    s.faultData=OSDynamicCast(CCDataStream,s.dataStream);
    if(!s.faultData)return quarantine(s,StartupFailure::dataStreamType);
    s.reporter=CCFaultReporter::withStreamWorkloop(s.faultData,s.workQueue);
    if(!s.reporter)return quarantine(s,StartupFailure::rawReporter);
    s.wrapper=IO80211FaultReporter::allocWithParams(s.reporter);
    if(!s.wrapper)return quarantine(s,StartupFailure::wrapper);
    s.phase=StartupPhase::supportReady;return true;
}

extern "C" bool r16_startup_enter_base(StartupLedger *ledger){
    if(!ledger||ledger->phase!=StartupPhase::supportReady)return false;
    auto &s=*ledger;
    // These are real target-ordered virtual calls, not dummy callbacks. Future
    // owner callbacks must return the ledger objects before base start enters.
    if(s.controller->getLogger()!=s.logger||s.controller->getWorkQueue()!=s.workQueue||
       s.controller->getFaultReporterFromDriver()!=s.wrapper)
        return quarantine(s,StartupFailure::getterWiring);
    // Darwin 24.4's native queue intentionally has no getThread(). The parent
    // networking start applies ml_thread_policy without a null check unless
    // its provider exposes that same workloop. Require the provider wrapper
    // relationship before entering any partially initializing base method.
    if(!s.workQueue||!s.provider||s.controller->getWorkLoop()!=s.workQueue||
       s.provider->getWorkLoop()!=s.workQueue)
        return quarantine(s,StartupFailure::providerWorkQueue);
    s.phase=StartupPhase::baseEntered; // set BEFORE a possibly partially successful base call
    if(!s.controller->IO80211Controller::start(s.provider))return quarantine(s,StartupFailure::baseStart);
    s.phase=StartupPhase::baseReady;return true;
    // This means only base-start returned true. No interface, WCL publication,
    // hardware enable, data queues or networking is performed by this prototype.
}

// Emit compiler-selected Itanium member-pointer offsets for independent audit.
// Their first word must be target vptr byte offset +1, second word zero.
extern "C" {
extern bool (CCPipe::* const r16_startup_pipe_slot)()=&CCPipe::startPipe;
extern CCLogStream *(IO80211Controller::* const r16_startup_logger_slot)()const=&IO80211Controller::getLogger;
extern IO80211WorkQueue *(IO80211Controller::* const r16_startup_work_queue_slot)()const=&IO80211Controller::getWorkQueue;
extern IO80211FaultReporter *(IO80211Controller::* const r16_startup_fault_slot)()=&IO80211Controller::getFaultReporterFromDriver;
extern bool (IO80211Controller::* const r16_startup_base_start_slot)(IOService*)=&IO80211Controller::start;
}
