// SPDX-License-Identifier: GPL-2.0-or-later
// Compile-only ABI probe. No native object is constructed, linked or called.
#ifndef R16_NATIVE_ABI_AUDIT_ONLY
#error "WCL event sender probe is offline ABI audit only"
#endif
#ifndef __x86_64__
#error "Only the pinned x86_64 Darwin 24.4 KC is examined"
#endif

class IO80211SkywalkInterface;

// Incomplete declaration solely to make Clang emit the exact KC symbol.
// This says nothing about constructing or retaining a live PostOffice.
class IO80211PostOffice {
public:
    int sendMail(IO80211SkywalkInterface *,unsigned,void *,unsigned long,bool);
};

namespace r16_wcl_event_audit {
template<class A,class B> struct Same {static constexpr bool value=false;};
template<class A> struct Same<A,A> {static constexpr bool value=true;};
static_assert(sizeof(unsigned long)==8,"x86_64 event length ABI");
static_assert(Same<decltype(((IO80211PostOffice*)0)->sendMail(
    (IO80211SkywalkInterface*)0,201,(void*)0,64,true)),int>::value,
    "PostOffice status must use full 32-bit IOReturn");

// All fields start false. They are prerequisites that a future registered
// native owner must prove under its own gate, not facts established here.
struct Proof {
    bool exactRuntimeKC{};
    bool ownerGateHeld{};
    bool currentFullScan{};
    bool payloadValidated{};
    bool interfaceRegistered{};
    bool postOfficeStarted{};
    bool glueAndManagersReady{};
    bool interfaceLifetimeThroughDelivery{};
    bool queueDrainAndStopSerialized{};
};
struct Outcome {bool invoked{};int queueStatus{};};

// The real producer uses IO80211Controller::postMessage(...,true), which
// dispatches through PostOffice's virtual sendMail. This *direct* method
// reference probes its mangled call ABI only. It is not a replacement for
// that dispatch or permission to emit. Success means a synchronous copy was
// queued; WCL/userland processing is asynchronous and unacknowledged.
Outcome submitCallsiteProbe(IO80211PostOffice *office,
                            IO80211SkywalkInterface *interface,
                            unsigned event,void *payload,unsigned long bytes,
                            const Proof &proof) {
    if(!office||!interface||!payload||!proof.exactRuntimeKC||
       !proof.ownerGateHeld||!proof.currentFullScan||!proof.payloadValidated||
       !proof.interfaceRegistered||!proof.postOfficeStarted||
       !proof.glueAndManagersReady||
       !proof.interfaceLifetimeThroughDelivery||!proof.queueDrainAndStopSerialized)
        return {};
    if(event==201){
        if(bytes<64||bytes>2112)return {};
        const auto *p=static_cast<const unsigned char*>(payload);
        const unsigned inner=unsigned(p[0])|(unsigned(p[1])<<8)|
                             (unsigned(p[2])<<16)|(unsigned(p[3])<<24);
        if(inner!=bytes-64||inner>2048)return {};
    }else if(event==237){
        if(bytes!=4)return {};
        const auto *p=static_cast<const unsigned char*>(payload);
        if(p[0]||p[1]||p[2]||p[3])return {};
    }else return {};
    // No code in this repository promotes every Proof bit. This callsite
    // cannot enter a kext target because of the audit-only compile guard.
    return {true,office->sendMail(interface,event,payload,bytes,true)};
}
} // namespace r16_wcl_event_audit
