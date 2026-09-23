#!/usr/bin/env python3
"""Generate host failure tests for the actual native base-entry function."""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parents[1]
PRE = r'''
#include <cassert>
#include <cstdio>
struct IOWorkLoop {};
struct IOService {
    IOWorkLoop *loop{};
    IOWorkLoop *getWorkLoop() const { return loop; }
};
struct IO80211Controller : IOService {
    void *logger{}, *wrapper{};
    IOWorkLoop *queue{};
    unsigned starts{};
    bool result=true;
    void *getLogger() const { return logger; }
    void *getFaultReporterFromDriver() const { return wrapper; }
    IOWorkLoop *getWorkQueue() const { return queue; }
    bool start(IOService *p) { assert(p); ++starts; return result; }
};
'''
POST = r'''
struct StartupLedger {
    StartupPhase phase=StartupPhase::supportReady;
    StartupFailure failure=StartupFailure::none;
    IO80211Controller *controller{};
    IOService *provider{};
    IOWorkLoop *workQueue{};
    void *logger{}, *wrapper{};
};
'''
TEST = r'''
int main() {
    IOWorkLoop queue, other;
    IOService provider;
    IO80211Controller controller;
    StartupLedger ledger;
    auto reset=[&]() {
        controller={}; provider={}; ledger={};
        provider.loop=controller.loop=controller.queue=&queue;
        ledger.controller=&controller; ledger.provider=&provider;
        ledger.workQueue=&queue;
    };
    assert(!r16_startup_enter_base(nullptr));
    for (unsigned fault=0; fault<4; ++fault) {
        reset();
        if(fault==0) provider.loop=nullptr;
        if(fault==1) provider.loop=&other;
        if(fault==2) controller.loop=&other;
        if(fault==3) ledger.provider=nullptr;
        assert(!r16_startup_enter_base(&ledger));
        assert(controller.starts==0);
        assert(ledger.failure==StartupFailure::providerWorkQueue);
        assert(ledger.phase==StartupPhase::quarantined);
        provider.loop=controller.loop=&queue; ledger.provider=&provider;
        assert(!r16_startup_enter_base(&ledger)); // No retry after quarantine.
        assert(controller.starts==0);
    }
    reset(); controller.queue=&other;
    assert(!r16_startup_enter_base(&ledger));
    assert(controller.starts==0&&ledger.failure==StartupFailure::getterWiring);
    reset(); controller.result=false;
    assert(!r16_startup_enter_base(&ledger));
    assert(controller.starts==1&&ledger.failure==StartupFailure::baseStart);
    assert(!r16_startup_enter_base(&ledger)&&controller.starts==1);
    reset(); assert(r16_startup_enter_base(&ledger));
    assert(controller.starts==1&&ledger.phase==StartupPhase::baseReady);
    assert(!r16_startup_enter_base(&ledger)&&controller.starts==1);
    puts("Native startup entry: provider mismatch rejected before base; one-shot success/failure passed");
}
'''

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output',type=Path)
a=p.parse_args()
source=(ROOT/'tools/native_abi/startup_prototype.cpp').read_text()
enums=source[source.index('enum class StartupPhase'):source.index('struct StartupLedger')]
quarantine=source[source.index('static bool quarantine'):source.index('template<size_t')]
entry=source[source.index('extern "C" bool r16_startup_enter_base'):source.index('// Emit compiler-selected')]
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(PRE+enums+POST+quarantine+entry+TEST)
