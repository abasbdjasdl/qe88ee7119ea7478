// SPDX-License-Identifier: GPL-2.0-or-later
// Host-only model. Test tokens are NOT fabricated IOSkywalkPacket instances.
#include "native_cpu_packet_bridge.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
using namespace r16_native_audit;
static unsigned checks;
static void check(bool ok,int line) {++checks;if(!ok){std::fprintf(stderr,"line %d\n",line);std::abort();}}
#define CHECK(x) check(bool(x),__LINE__)
static constexpr Status failure = static_cast<Status>(0xe00002d7u);
struct TestPacket {
    uint8_t bytes[96]{};
    size_t offset{3},length{20},size{96};
    unsigned buffers{1},returns{};
    bool acquired{true},prepared{true},metadata{true};
};
struct TestPort {
    using Packet = TestPacket;
    enum class Fault { none,allocate,prepare,view,length,enqueue,ambiguousAllocation,nullSuccess };
    Fault fault{};
    Packet rx{};
    unsigned allocations{},rxFree{},rxTransfer{},txFree{},txComplete{};
    bool completionFails{};
    TestPort(){rx.acquired=false;rx.prepared=false;}
    PacketView view(Packet *p) {
        CHECK(p && p->acquired);
        return {p->bytes,p->size,p->offset,p->length,
                (p==&rx && fault==Fault::view)?2u:p->buffers,p->prepared,p->metadata};
    }
#ifdef R16_TEST_BAD_ALLOCATE_RETURN
    bool allocateRx(Packet **out) {
#else
    Status allocateRx(Packet **out) {
#endif
        CHECK(out && !*out && !rx.acquired);++allocations;
        if(fault==Fault::allocate) return failure;
        if(fault==Fault::nullSuccess) return success;
        rx.acquired=true;rx.prepared=false;*out=&rx;
        return fault==Fault::ambiguousAllocation?failure:success;
    }
    Status prepareRx(Packet *p){CHECK(p==&rx&&p->acquired);if(fault==Fault::prepare)return failure;p->prepared=true;return success;}
    Status setRxLength(Packet *p,unsigned n){CHECK(p==&rx&&p->prepared);if(fault==Fault::length)return failure;p->length=n;return success;}
    Status enqueueRx(Packet * const *p,unsigned n){
        CHECK(n==1&&p&&*p==&rx&&rx.prepared);
        if(fault==Fault::enqueue)return failure;
        rx.acquired=false;rx.prepared=false;++rxTransfer;++rx.returns;return success;
    }
    Status completeTx(Packet * const *p,unsigned n){
        CHECK(n==1&&p&&*p&&(*p)->acquired&&(*p)->prepared);
        if(completionFails)return failure;
        ++txComplete;retire(*p);return success;
    }
    void reclaimRx(Packet *p){CHECK(p==&rx&&p->acquired);++rxFree;retire(p);}
    void reclaimPreparedTx(Packet *p){CHECK(p&&p!=&rx&&p->prepared&&p->acquired);++txFree;retire(p);}
    static void retire(Packet *p){CHECK(p->acquired);p->acquired=false;p->prepared=false;++p->returns;}
};
using Bridge=CpuPacketBridge<TestPort,2,64>;

static void txPrefixAndCopy() {
    Bridge b;TestPort port;TestPacket p[3];TestPacket *batch[]={p,p+1,p+2};
    unsigned bytes=9;CHECK(b.capacity(&bytes)==0&&bytes==0);
    CHECK(!b.begin(7,true)&&!b.begin(8,false)&&b.begin(8,true));
    CHECK(b.capacity(&bytes)==2&&bytes==128);
    for(size_t i=0;i<sizeof(p[0].bytes);++i)p[0].bytes[i]=uint8_t(i);
    CHECK(b.submitTx(port,batch,3)==2&&b.outstandingTx()==2);
    CHECK(p[2].acquired&&p[2].returns==0); // unconsumed suffix is untouched.
    CHECK(port.txComplete==0&&port.txFree==0); // no synchronous completion.
    std::memset(p[0].bytes,0xff,sizeof(p[0].bytes));
    Bridge::Ticket first{},second{},extra{};
    CHECK(b.takeTx(first)&&b.takeTx(second)&&!b.takeTx(extra));
    CHECK(first.bytes[0]==3&&first.length==20&&first.bytes[19]==22);
    CHECK(b.finishTx(first,true)&&!b.finishTx(first,true));
    port.completionFails=true;CHECK(b.returnCompletedTx(port)==0&&p[0].acquired);
    port.completionFails=false;CHECK(b.returnCompletedTx(port)==1&&!p[0].acquired);
    CHECK(b.submitTx(port,batch+2,1)==1);
    Bridge::Ticket reused{};CHECK(b.takeTx(reused));
    CHECK(reused.slot==first.slot&&reused.generation!=first.generation);
    CHECK(!b.finishTx(first,true)); // late completion must not free reused slot.
    b.beginStop();CHECK(!b.takeTx(extra)&&b.submitTx(port,batch,1)==0);
    CHECK(!b.finishStop(true,true));
    CHECK(b.finishTx(second,false)&&b.finishTx(reused,true));
    CHECK(b.returnCompletedTx(port,true)==2);
    CHECK(!b.finishStop(false,true)&&!b.finishStop(true,false));
    CHECK(b.finishStop(true,true)&&!b.begin(8,true));
    CHECK(b.counters().txCompleted==2&&b.counters().txFailed==1);
    for(const auto &packet:p)CHECK(packet.returns==1);
}
static void malformedAndStop() {
    for(unsigned fault=0;fault<8;++fault) {
        Bridge b;TestPort port;TestPacket p;TestPacket *batch[]={&p};CHECK(b.begin(8,true));
        if(fault==0)p.buffers=2;
        if(fault==1)p.offset=97;
        if(fault==2)p.length=94;
        if(fault==3)p.length=13;
        if(fault==4)p.length=65;
        if(fault==5)p.metadata=false;
        if(fault==6)p.prepared=false;
        if(fault==7){p.offset=std::numeric_limits<size_t>::max();p.length=20;}
        CHECK(b.submitTx(port,batch,1)==1&&b.counters().invalidTx==1);
        Bridge::Ticket t;CHECK(!b.takeTx(t));
        // The real callback guarantees prepare(dir=1); a false prepared bit
        // in this view means inspection rejected it, not manual unprepare.
        p.prepared=true;CHECK(b.returnCompletedTx(port)==1&&p.returns==1);
        b.beginStop();CHECK(b.finishStop(true,true));
    }
    Bridge b;TestPort port;TestPacket p;TestPacket *batch[]={&p};CHECK(b.begin(8,true));
    CHECK(b.submitTx(port,batch,1)==1);b.beginStop();
    CHECK(!b.finishStop(true,true)&&b.returnCompletedTx(port,true)==1&&b.finishStop(true,true));
}
static void fifoAfterSlotReuse() {
    Bridge b;TestPort port;TestPacket p[3];TestPacket *batch[]={p,p+1,p+2};
    CHECK(b.begin(8,true)&&b.submitTx(port,batch,2)==2);
    Bridge::Ticket first{},older{},newer{},empty{};
    CHECK(b.takeTx(first)&&b.finishTx(first,true)&&b.returnCompletedTx(port)==1);
    CHECK(b.submitTx(port,batch+2,1)==1); // reuse slot zero while older slot one is queued.
    CHECK(b.takeTx(older)&&older.slot==1);
    CHECK(b.takeTx(newer)&&newer.slot==0&&older.generation<newer.generation);
    empty=first;CHECK(!b.takeTx(empty)&&empty.generation==0&&empty.bytes==nullptr);
    CHECK(b.finishTx(older,true)&&b.finishTx(newer,true)&&b.returnCompletedTx(port)==2);
    b.beginStop();CHECK(b.finishStop(true,true));
}
static void duplicateQuarantine() {
    Bridge b;TestPort port;TestPacket p;TestPacket *dup[]={&p,&p};CHECK(b.begin(8,true));
    CHECK(b.submitTx(port,dup,2)==1&&b.phase()==Phase::quarantined);
    CHECK(b.outstandingTx()==1&&b.returnCompletedTx(port,true)==0&&p.returns==0);
    b.beginStop();CHECK(!b.finishStop(true,true));
    // Deliberately retained for forensic owner-directed recovery, no auto free.
}
static void rxOwnership() {
    const uint8_t data[20]={0x12,0x34,0x56};
    const TestPort::Fault faults[]={TestPort::Fault::none,TestPort::Fault::allocate,
        TestPort::Fault::prepare,TestPort::Fault::view,TestPort::Fault::length,TestPort::Fault::enqueue};
    const RxResult results[]={RxResult::delivered,RxResult::allocationFailed,
        RxResult::preparationFailed,RxResult::invalidBuffer,RxResult::lengthFailed,RxResult::enqueueFailed};
    for(unsigned i=0;i<6;++i) {
        Bridge b;TestPort port;port.fault=faults[i];CHECK(b.begin(8,true));
        CHECK(b.receive(port,data,sizeof(data))==results[i]);
        CHECK(b.rxState()==RxState::empty&&!port.rx.acquired);
        CHECK(port.rxTransfer==(i==0?1u:0u));
        CHECK(port.rxFree==(i>=2?1u:0u));
        CHECK(port.rx.returns==(i==1?0u:1u));
        if(i==0)CHECK(std::memcmp(port.rx.bytes+3,data,sizeof(data))==0&&port.rx.length==20);
        b.beginStop();CHECK(b.receive(port,data,20)==RxResult::stopped&&b.finishStop(true,true));
    }
    for(auto fault:{TestPort::Fault::ambiguousAllocation,TestPort::Fault::nullSuccess}) {
        Bridge b;TestPort port;port.fault=fault;CHECK(b.begin(8,true));
        CHECK(b.receive(port,data,20)==RxResult::contractViolation);
        CHECK(b.phase()==Phase::quarantined&&b.rxState()==RxState::quarantined);
        CHECK(port.rxFree==0&&port.rxTransfer==0&&!b.finishStop(true,true));
        CHECK((b.quarantinedRx()!=nullptr)==(fault==TestPort::Fault::ambiguousAllocation));
    }
    Bridge b;TestPort port;CHECK(b.begin(8,true));
    CHECK(b.receive(port,nullptr,20)==RxResult::invalidFrame);
    CHECK(b.receive(port,data,13)==RxResult::invalidFrame);
    CHECK(b.receive(port,data,65)==RxResult::invalidFrame&&port.allocations==0);
    b.beginStop();CHECK(b.finishStop(true,true));
}
int main(){
    CHECK(validConsumedCount(0,0)&&validConsumedCount(3,2)&&!validConsumedCount(3,4));
    CHECK(!validConsumedCount(3,uint32_t(failure))); // never return IOReturn as count.
    txPrefixAndCopy();fifoAfterSlotReuse();malformedAndStop();duplicateQuarantine();rxOwnership();
    std::printf("CPU packet bridge: %u checks\n",checks);
}
