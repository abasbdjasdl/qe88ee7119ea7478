// SPDX-License-Identifier: GPL-2.0-or-later
#define R16_NATIVE_ABI_AUDIT_ONLY 1
#include "native_infra_scan_bridge.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
using namespace r16_infra_audit;
static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static void word(uint8_t *p, size_t off, uint32_t value) {
    for (unsigned i=0;i<4;++i) p[off+i]=uint8_t(value>>(i*8));
}
struct Mock {
    uint8_t bytes[wire::messageBytes]{};
    ScanBridge *bridge{};
    uint8_t *scratch{};
    Snapshot snapshot{};
    CopiedScan copied{};
    Status copyStatus{Status::accepted}, submitStatus{Status::accepted};
    unsigned copies{}, submits{};
    bool reenterCopy{}, reenterSubmit{};
    void fixture(uint32_t mode=1) {
        std::memset(bytes,0,sizeof(bytes));
        word(bytes,0,0xaabbccdd); bytes[4]=1;
        word(bytes,0x10,3); word(bytes,0x40,mode);
        word(bytes,0x48,40); word(bytes,0x4c,110); word(bytes,0x54,3);
        unsigned channels[]={11,1,6};
        for (unsigned i=0;i<3;++i) {
            word(bytes,0x58+i*12,1);word(bytes,0x5c+i*12,channels[i]);
            word(bytes,0x60+i*12,0x0a);
        }
        snapshot={sizeof(bytes),{wire::TargetProfile::darwin24_4_0_d8b50fc2,
            wire::PrivateMacPolicy::unsupported,0x7ff,0x7ff},{99,1}};
        copyStatus=submitStatus=Status::accepted;
    }
    static Status copy(void *context,const void *argument,uint8_t *out,size_t cap,Snapshot &snap) {
        auto &m=*static_cast<Mock*>(context);++m.copies;
        // Deliberately UNREADABLE argument: proves the bridge only forwards it.
        CHECK(argument==reinterpret_cast<const void*>(uintptr_t(1)));
        CHECK(cap==sizeof(m.bytes));m.scratch=out;
        if(m.reenterCopy) {
            CHECK(m.bridge->scan(argument)==Status::busy);
            CHECK(!m.bridge->unbindIdle());
            CHECK(!m.bridge->terminal(m.snapshot.identity));
        }
        std::memcpy(out,m.bytes,sizeof(m.bytes));snap=m.snapshot;
        return m.copyStatus;
    }
    static Status submit(void *context,const CopiedScan &value) {
        auto &m=*static_cast<Mock*>(context);++m.submits;m.copied=value;
        if(m.reenterSubmit) {
            CHECK(m.bridge->scan(reinterpret_cast<const void*>(uintptr_t(1)))==Status::busy);
            CHECK(!m.bridge->terminal(value.identity));
            CHECK(!m.bridge->unbindIdle());
        }
        return m.submitStatus;
    }
    Backend backend() {return {this,copy,submit};}
    void wiped() const {
        CHECK(scratch);
        for(size_t i=0;i<sizeof(bytes);++i) CHECK(scratch[i]==0);
    }
};
static const void *opaque() {return reinterpret_cast<const void*>(uintptr_t(1));}
int main() {
    // Heap allocation mirrors the storage requirement; no actual native calls.
    auto b=std::unique_ptr<ScanBridge>(new ScanBridge);
    auto m=std::unique_ptr<Mock>(new Mock);m->bridge=b.get();m->fixture();
    CHECK(b->scan(nullptr)==Status::badArgument);CHECK(b->scan(opaque())==Status::notReady);
    CHECK(!b->bind({},99));CHECK(!b->bind(m->backend(),0));
    CHECK(b->bind(m->backend(),99));CHECK(!b->bind(m->backend(),99));
    m->reenterCopy=m->reenterSubmit=true;
    CHECK(b->scan(opaque())==Status::accepted);m->wiped();
    CHECK(m->copies==1&&m->submits==1);CHECK(!b->idle());
    CHECK(m->copied.identity.epoch==99&&m->copied.identity.generation==1);
    CHECK(m->copied.request.opaqueSourceHeader==0xaabbccdd);
    CHECK(m->copied.channels.active&&m->copied.channels.dwellMs==40);
    CHECK(m->copied.channels.channelCount==3&&m->copied.channels.channels[0]==11&&
          m->copied.channels.channels[1]==1&&m->copied.channels.channels[2]==6);
    std::memset(m->bytes,0xcc,sizeof(m->bytes));
    CHECK(m->copied.channels.channels[0]==11); // backend copied no borrowed data
    CHECK(b->scan(opaque())==Status::busy);CHECK(!b->unbindIdle());
    CHECK(!b->terminal({100,1}));CHECK(!b->terminal({99,2}));CHECK(!b->terminal({99,0}));
    CHECK(b->terminal({99,1}));CHECK(!b->terminal({99,1}));CHECK(b->idle());
    m->fixture();CHECK(b->scan(opaque())==Status::stale);m->wiped();CHECK(m->submits==1);
    m->snapshot.identity={100,2};CHECK(b->scan(opaque())==Status::stale);
    m->snapshot.identity={99,0};CHECK(b->scan(opaque())==Status::stale);
    m->snapshot.identity={99,2};m->snapshot.copiedBytes=wire::messageBytes-1;
    CHECK(b->scan(opaque())==Status::badArgument);m->wiped();
    m->snapshot.copiedBytes=wire::messageBytes+1;CHECK(b->scan(opaque())==Status::badArgument);
    m->snapshot.copiedBytes=wire::messageBytes;
    for(auto status:{Status::badArgument,Status::unsupported,Status::notReady,
                     Status::busy,Status::stale,Status::failed}) {
        m->copyStatus=status;CHECK(b->scan(opaque())==status);m->wiped();
        CHECK(b->idle());CHECK(m->submits==1);
    }
    m->copyStatus=Status::accepted;
    // Ordinary native home timings cannot be silently discarded.
    for(size_t off:{size_t(8),size_t(0x50)}) {
        m->fixture();m->snapshot.identity.generation=++m->copies+100;
        word(m->bytes,off,45);CHECK(b->scan(opaque())==Status::unsupported);m->wiped();
        CHECK(b->idle());CHECK(m->submits==1);
    }
    uint64_t generation=1000;
    for(unsigned failure=0;failure<5;++failure) {
        m->fixture();m->snapshot.identity.generation=++generation;
        if(failure==0)m->snapshot.policy.profile=wire::TargetProfile::unknown;
        if(failure==1)m->snapshot.policy.permittedActiveChannels=1;
        if(failure==2)m->snapshot.policy.privateMac=wire::PrivateMacPolicy::enabled;
        if(failure==3)word(m->bytes,0x48,9);
        if(failure==4)word(m->bytes,0x1c,1); // directed SSID is not supported
        CHECK(b->scan(opaque())==Status::unsupported);m->wiped();CHECK(m->submits==1);
    }
    for(auto status:{Status::badArgument,Status::unsupported,Status::notReady,
                     Status::busy,Status::stale,Status::failed}) {
        m->fixture();m->snapshot.identity.generation=++generation;m->submitStatus=status;
        CHECK(b->scan(opaque())==status);m->wiped();CHECK(b->idle());
        CHECK(b->scan(opaque())==Status::stale); // failed ticket is consumed
    }
    m->fixture(2);m->snapshot.identity.generation=++generation;
    CHECK(b->scan(opaque())==Status::accepted);CHECK(!m->copied.channels.active);
    CHECK(m->copied.channels.dwellMs==110);CHECK(b->terminal(m->snapshot.identity));
    CHECK(b->unbindIdle());CHECK(b->scan(opaque())==Status::notReady);
    CHECK(!b->bind(m->backend(),99));CHECK(!b->bind(m->backend(),98));
    CHECK(b->bind(m->backend(),100));
    m->fixture();m->snapshot.identity={100,1};
    CHECK(b->scan(opaque())==Status::accepted);
    CHECK(!b->terminal({99,1}));CHECK(!b->terminal({99,generation}));
    CHECK(b->terminal({100,1}));b->poison();
    CHECK(b->scan(opaque())==Status::notReady);CHECK(b->unbindIdle());
    CHECK(!b->bind(m->backend(),101));
    std::printf("native Infra scan bridge: %u checks passed\n",checks);
}
