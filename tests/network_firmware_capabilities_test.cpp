// SPDX-License-Identifier: BSD-3-Clause
#include "network_rfk_fakes/Fake.hpp"
inline uint16_t OSReadLittleInt16(const volatile void *base,unsigned a){
    const auto p=static_cast<const volatile uint8_t *>(base)+a;return uint16_t(p[0])|(uint16_t(p[1])<<8);}
static void (*firmwareHook)();
inline void capabilitySync(){++fakeRfk::barriers;if(firmwareHook)firmwareHook();}
#define OSSynchronizeIO capabilitySync
#include "../src/network/MacFirmwareMailboxIo.cpp"
#undef OSSynchronizeIO
#include "../src/network/FirmwareCapabilities.cpp"
#include <cstdio>
#include <initializer_list>
namespace f=rtl8852be::firmware;namespace n=rtl8852be::network;
struct Fixture;
static Fixture *current;
struct Fixture {
    IOPCIDevice pci;IOMemoryMap map;IOWorkLoop loop;f::MacMailboxIo io{&pci,&map,&loop};
    f::Mailbox<f::MacMailboxIo> mail{io};f::FirmwareCapabilities<decltype(mail)> caps{mail};
    n::CalibrationSnapshot cal{};f::Plan plan{};
    uint32_t reply[4]={0x55020403,0x4433aa02,0x77665522,0x11020299};
    bool suppress{};unsigned triggers{};
    Fixture(){fakeRfk::reset();current=this;firmwareHook=respond;map.set(f::firmwareControl,0xe0);
        cal.cut=1;cal.board.identityValid=true;cal.board.mac[0]=2;cal.board.mac[5]=7;cal.board.rfe=5;
        plan.valid=true;plan.cut=1;plan.type=1;}
    ~Fixture(){firmwareHook=nullptr;current=nullptr;}
    static void respond(){auto &x=*current;
        if(x.map.data[f::h2cControl]!=1)return;
        ++x.triggers;x.map.data[f::h2cControl]=0;
        assert(x.map.get(f::h2cData[0])==0x103);
        for(unsigned i=1;i<4;++i)assert(x.map.get(f::h2cData[i])==0);
        if(x.suppress)return;
        for(unsigned i=0;i<4;++i)x.map.set(f::c2hData[i],x.reply[i]);
        x.map.data[f::c2hControl]=1;
    }
    bool query(){return caps.query(&cal,plan,37);}
};
// Inject faults around actual native MMIO methods; byte register accesses still
// use the original volatile implementation, and barriers drive modeled firmware.
struct FaultIo {
    Fixture &f;unsigned ops{},failAt{},cancelAt{},slowAt{},delayCalls{};uint64_t slowUs{};
    bool after{},frozen{},backwards{};
    bool inGate(){return f.io.inGate();}bool cancelled(){return f.io.cancelled();}
    uint64_t nowUs(){return backwards&&delayCalls?--fakeRfk::time:fakeRfk::time;}
    bool pre(){++ops;if(ops==cancelAt)f.io.cancel();return after||ops!=failAt;}
    bool post(bool ok){if(ops==slowAt)fakeRfk::time+=slowUs;return ok&&ops!=failAt;}
    bool read8(uint32_t a,uint8_t &v){return pre()&&post(f.io.read8(a,v));}
    bool read16(uint32_t a,uint16_t &v){return pre()&&post(f.io.read16(a,v));}
    bool read32(uint32_t a,uint32_t &v){return pre()&&post(f.io.read32(a,v));}
    bool write8(uint32_t a,uint8_t v){return pre()&&post(f.io.write8(a,v));}
    bool write32(uint32_t a,uint32_t v){return pre()&&post(f.io.write32(a,v));}
    bool delayUs(unsigned us){++delayCalls;const auto before=fakeRfk::time;const bool ok=f.io.delayUs(us);
        if(frozen)fakeRfk::time=before;return ok;}
};
int main(){
    {Fixture x;assert(x.query());const auto *s=x.caps.snapshot();assert(s&&s->epoch==37&&s->cut==1&&s->rfe==5&&s->mac[5]==7);
        assert(s->txNss==2&&s->rxNss==2&&!s->txNssFallback&&!s->rxNssFallback);
        assert(s->bandwidthRaw==0x55&&s->protocolRaw==0xaa&&s->nicRaw==0x33&&s->wirelessFunctionRaw==0x44&&s->hardwareTypeRaw==0x22);
        assert(!s->bandwidthEncodingKnown&&!s->protocolEncodingKnown&&s->effectiveTxPaths()==3&&s->effectiveRxPaths()==3);
        assert(s->supportCckpd&&!s->supportIgi&&x.triggers==1&&x.map.data[f::c2hControl]==0&&x.map.data[f::hostCounters]==0x11);
        assert(!x.query()&&x.triggers==1);x.caps.invalidate();assert(!x.caps.snapshot()&&!x.query());}
    // Every NSS byte, zero fallback, clamping and all supported antenna cases.
    unsigned combinations=0;
    for(unsigned tx=0;tx<256;++tx)for(unsigned rx:{0u,1u,2u,255u})for(unsigned ta=0;ta<=2;++ta)for(unsigned ra=0;ra<=2;++ra){
        Fixture x;x.cal.cut=x.plan.cut=0;x.reply[0]=0x8a00b483|(rx<<16);x.reply[1]=0x97ef6500|tx;x.reply[3]=(ta<<8)|(ra<<16);
        assert(x.query());const auto &s=*x.caps.snapshot();++combinations;
        assert(s.reportedTxNss==tx&&s.reportedRxNss==rx&&s.txNss==(tx==1?1:2)&&s.rxNss==(rx==1?1:2));
        assert(s.txNssFallback==(tx==0)&&s.rxNssFallback==(rx==0)&&s.sequence==11&&s.ack);
        const bool diversity=tx==1&&ta==2&&ra==2;
        assert(s.antennaTx==(ta==1||diversity?2:0)&&s.antennaRx==(ra==1?2:0)&&s.txPathDiversity==diversity);
        assert(!s.supportCckpd&&!s.supportIgi&&s.bandwidthRaw==0x8a&&s.protocolRaw==0x65);
    }
    for(unsigned invalid=0;invalid<7;++invalid){Fixture x;
        if(invalid==0)x.cal.board.identityValid=false;if(invalid==1)x.cal.cut=2;
        if(invalid==2)x.plan.cut=0;if(invalid==3)x.plan.valid=false;if(invalid==4)x.plan.type=2;
        assert(!x.caps.query(invalid==5?nullptr:&x.cal,x.plan,invalid==6?0:37));
        assert(x.caps.result().error==f::CapabilityError::identity&&!x.triggers&&!x.caps.snapshot());
    }
    {Fixture x;x.plan.type=5;assert(x.query());}
    for(unsigned ant=3;ant<256;++ant)for(unsigned shift:{8u,16u}){Fixture x;x.reply[3]=(x.reply[3]&~(255u<<shift))|(ant<<shift);
        assert(!x.query()&&x.caps.result().error==f::CapabilityError::antenna&&!x.caps.snapshot());}
    for(unsigned words=0;words<=5;++words){if(words==4)continue;Fixture x;x.reply[0]=(x.reply[0]&~0xf00u)|(words<<8);
        assert(!x.query()&&!x.caps.snapshot());assert(x.caps.result().error==(words>=1&&words<=3?f::CapabilityError::length:f::CapabilityError::mailbox));}
    {Fixture x;x.reply[0]=(x.reply[0]&~127u)|4;assert(!x.query()&&x.caps.result().mail.error==f::MailError::unexpectedReply&&!x.caps.snapshot());}
    {Fixture x;x.map.data[f::c2hControl]=1;assert(!x.query()&&!x.triggers&&!x.caps.snapshot()&&x.caps.result().mail.error==f::MailError::staleReply);}
    unsigned totalOps=0;
    {Fixture x;FaultIo io{x};f::Mailbox<FaultIo> mail(io);f::FirmwareCapabilities<decltype(mail)> caps(mail);
        assert(caps.query(&x.cal,x.plan,1));totalOps=io.ops;}
    for(bool after:{false,true})for(unsigned pos=1;pos<=totalOps;++pos){Fixture x;FaultIo io{x};io.failAt=pos;io.after=after;
        f::Mailbox<FaultIo> mail(io);f::FirmwareCapabilities<decltype(mail)> caps(mail);
        assert(!caps.query(&x.cal,x.plan,1)&&!caps.snapshot()&&mail.faulted());
        assert(caps.result().mail.error==f::MailError::io&&io.ops==pos);}
    for(unsigned pos=1;pos<=totalOps;++pos){Fixture x;FaultIo io{x};io.cancelAt=pos;
        f::Mailbox<FaultIo> mail(io);f::FirmwareCapabilities<decltype(mail)> caps(mail);
        assert(!caps.query(&x.cal,x.plan,1)&&!caps.snapshot()&&mail.faulted());}
    for(bool frozen:{false,true}){Fixture x;x.suppress=true;FaultIo io{x};io.frozen=frozen;
        f::Mailbox<FaultIo> mail(io);f::FirmwareCapabilities<decltype(mail)> caps(mail);
        assert(!caps.query(&x.cal,x.plan,1)&&!caps.snapshot()&&caps.result().mail.error==f::MailError::timeout);}
    {Fixture x;x.suppress=true;FaultIo io{x};io.backwards=true;f::Mailbox<FaultIo> mail(io);f::FirmwareCapabilities<decltype(mail)> caps(mail);
        assert(!caps.query(&x.cal,x.plan,1)&&!caps.snapshot()&&caps.result().mail.error==f::MailError::clock);}
    // Actual read of C2H ready consumes > 1 second; and last payload read > global
    // deadline. Neither may publish the capability snapshot despite valid bytes.
    for(unsigned pos:{12u,16u}){Fixture x;FaultIo io{x};io.slowAt=pos;io.slowUs=pos==12?1000001:1100001;
        f::Mailbox<FaultIo> mail(io);f::FirmwareCapabilities<decltype(mail)> caps(mail);
        assert(!caps.query(&x.cal,x.plan,1)&&!caps.snapshot()&&caps.result().mail.error==f::MailError::timeout);}
    for(unsigned reason=0;reason<4;++reason){Fixture x;if(reason==0)x.loop.gate=false;if(reason==1)x.io.cancel();
        if(reason==2)x.pci.command=0xffff;if(reason==3)x.pci.command=0;
        assert(!x.query()&&!x.caps.snapshot()&&!x.triggers);}
    {Fixture x;x.pci.vendor=0;f::MacMailboxIo io(&x.pci,&x.map,&x.loop);assert(!io.valid());}
    {Fixture x;x.map.physical+=4096;f::MacMailboxIo io(&x.pci,&x.map,&x.loop);assert(!io.valid());}
    {Fixture x;x.map.length=x.pci.bar.length=0x8000;f::MacMailboxIo io(&x.pci,&x.map,&x.loop);assert(!io.valid());}
    printf("PASS: actual MacMailboxIo GET_FEATURE/PHY_CAP, %u NSS/antenna combinations, %u native I/O fault positions before/after, identity/cut/epoch and timeout/no-publication checks; modeled hardware\n",combinations,totalOps);
}
