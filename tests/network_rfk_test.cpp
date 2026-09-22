// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/RfkInitialization.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
namespace r=rtl8852be::rfk;
struct Backend {
    std::map<unsigned,unsigned> bb,rf[2],mac;
    unsigned operations{},failAt{},delayCalls{},failDelay{},begins{},ends{},drains{},failBegin{},failEnd{},failDrain{};
    unsigned notReady=0xffffffff,cancelAt{};bool frozen{},backwards{},cancel{},active{},failedWrite{};
    uint64_t clock=100;std::vector<bool> endSuccess;
    Backend(){
        bb[r::R_DRCK_RS]=r::B_DRCK_RS_DONE|(9u<<15);
        bb[r::R_ADDCKR0]=(0x123u<<10)|0x155;bb[r::R_ADDCKR1]=(0x234u<<10)|0x2ab;
        for(auto a:{r::R_DACK_S0P0,r::R_DACK_S0P1,r::R_DACK_S1P0,r::R_DACK_S1P1})bb[a]=0x80000000;
        for(auto a:{r::R_DACK_S0P2,r::R_DACK_S0P3,r::R_DACK10S,r::R_DACK11S})bb[a]=4;
        bb[r::R_DACK_BIAS00]=0x12u<<2;bb[r::R_DACK_BIAS01]=0x34u<<2;
        bb[r::R_DACK_BIAS10]=0x56u<<2;bb[r::R_DACK_BIAS11]=0x78u<<2;
        bb[r::R_DACK_DADCK00]=0x21u<<24;bb[r::R_DACK_DADCK01]=0x43u<<24;
        bb[r::R_DACK_DADCK10]=0x65u<<24;bb[r::R_DACK_DADCK11]=0x87u<<24;
        for(unsigned p=0;p<2;++p){rf[p][r::RR_RSV1]=0x345;rf[p][r::RR_MOD]=0x30001;
            rf[p][r::RR_RCKS]=8;rf[p][r::RR_DCK]=2;}
    }
    bool step(){assert(active);++operations;if(operations==cancelAt)cancel=true;return operations!=failAt;}
    bool readRf(uint8_t p,unsigned a,unsigned m,unsigned &v){
        if(!step())return false;assert(p<2);v=r::fieldGet(m,a==notReady?0:rf[p][a]);return true;
    }
    bool writeRf(uint8_t p,unsigned a,unsigned m,unsigned v){
        if(!step()){failedWrite=true;return false;}assert(p<2);
        rf[p][a]=(rf[p][a]&~m)|((v<<r::shift(m))&m);
        if(a==r::RR_RCKC&&v==0x240)rf[p][a]|=(0x12u+p)<<10;return true;
    }
    bool readBb(unsigned a,unsigned &v){
        if(!step())return false;v=bb[a];
        const unsigned results[]={r::R_DACK_S0P2,r::R_DACK_S0P3,r::R_DACK10S,r::R_DACK11S};
        const unsigned selects[]={r::R_DCOF0,r::R_DCOF8,r::R_DACK10,r::R_DACK11};
        for(unsigned i=0;i<4;++i)if(a==results[i])v|=(0x20u+i*0x10+((bb[selects[i]]>>1)&15))<<24;
        if(a==notReady)v=0;return true;
    }
    bool writeBb(unsigned a,unsigned v){if(!step()){failedWrite=true;return false;}bb[a]=v;return true;}
    bool writeMac(unsigned a,unsigned v){if(!step()){failedWrite=true;return false;}mac[a]=v;return true;}
    bool cancelled(){return cancel;}
    uint64_t nowUs(){return backwards&&delayCalls?--clock:clock;}
    bool delayUs(unsigned us){assert(active);++delayCalls;if(!frozen)clock+=us;return delayCalls!=failDelay;}
    bool begin(r::Kind){assert(!active);++begins;if(begins==failBegin)return false;active=true;return true;}
    bool end(r::Kind,bool success){assert(active);++ends;endSuccess.push_back(success);if(ends==failEnd)return false;active=false;return true;}
    bool drain(){assert(active);return ++drains!=failDrain;}
};
bool run(Backend &b,r::Result &result){r::Initialization<Backend> cal(b,1);bool ok=cal.initialize();result=cal.result;return ok;}
int main(){
    Backend b;r::Initialization<Backend> cal(b,1);assert(cal.initialize());const auto count=b.operations;
    assert(cal.result.stage==r::Stage::complete&&cal.result.rckReady&&cal.result.dackReady&&cal.result.rxDcReady);
    assert(!cal.result.requiresReset&&cal.result.ownershipReleased&&!b.active&&b.begins==3&&b.ends==3);
    assert(!cal.initialize()&&b.operations==count);
    assert(cal.dpdBackoff()==0x5b);const auto &d=cal.dack();assert(d.dack_done&&d.dack_cnt==1);
    assert(d.addck_d[0][0]==0x123&&d.addck_d[0][1]==0x155&&d.addck_d[1][0]==0x234&&d.addck_d[1][1]==0x2ab);
    assert(d.biask_d[0][0]==0x12&&d.biask_d[0][1]==0x34&&d.biask_d[1][0]==0x56&&d.biask_d[1][1]==0x78);
    assert(d.dadck_d[0][0]==0x21&&d.dadck_d[0][1]==0x43&&d.dadck_d[1][0]==0x65&&d.dadck_d[1][1]==0x87);
    for(unsigned p=0;p<2;++p)for(unsigned iq=0;iq<2;++iq)for(unsigned i=0;i<16;++i)assert(d.msbk_d[p][iq][i]==0x20+p*0x20+iq*0x10+i);
    for(unsigned p=0;p<2;++p){assert(b.rf[p][r::RR_RSV1]==0x345);assert(b.rf[p][r::RR_MOD]==0x30001);
        assert(b.rf[p][r::RR_RCKC]==0x12+p);assert((b.rf[p][r::RR_DCK]&3)==3);}
    assert(b.mac[r::R_AX_PHYREG_SET]==0xf);
    r::Result result;
    for(unsigned i=1;i<=count;++i){Backend f;f.failAt=i;assert(!run(f,result));
        assert(result.error==r::Error::io&&f.operations==i&&result.requiresReset&&!f.active&&f.endSuccess.back()==false);
        assert(!result.rxDcReady);
    }
    for(unsigned i=1;i<=b.delayCalls;++i){Backend f;f.failDelay=i;assert(!run(f,result)&&result.error==r::Error::io&&!f.active);}
    for(unsigned i=1;i<=3;++i){
        Backend f;f.failBegin=i;assert(!run(f,result)&&result.error==r::Error::precondition&&result.requiresReset&&!f.active);
        Backend e;e.failEnd=i;assert(!run(e,result)&&result.requiresReset&&e.active&&!result.ownershipReleased);
        Backend d;d.failDrain=i;assert(!run(d,result)&&result.error==r::Error::io&&!d.active&&d.endSuccess.back()==false);
    }
    for(auto addr:{r::RR_RCKS,r::R_DRCK_RS,r::R_ADDCKR0,r::R_ADDCKR1,r::R_DACK_S0P0,r::R_DACK_S0P2}){
        for(bool frozen:{false,true}){Backend f;f.notReady=addr;f.frozen=frozen;
            assert(!run(f,result)&&result.error==r::Error::timeout&&result.address==addr&&!f.active);
            assert(result.polls<11000&&!result.rxDcReady);
        }
    }
    {Backend f;f.bb[r::R_DACK_S1P0]=f.bb[r::R_DACK_S1P1]=0;
        assert(!run(f,result)&&result.error==r::Error::timeout&&!result.dackReady);}
    {Backend f;f.bb[r::R_DACK10S]=f.bb[r::R_DACK11S]=0;
        assert(!run(f,result)&&result.error==r::Error::timeout&&!result.dackReady);}
    {Backend f;f.backwards=true;assert(!run(f,result)&&result.error==r::Error::clock&&!f.active);}
    {Backend f;f.cancel=true;assert(!run(f,result)&&result.error==r::Error::cancelled&&!f.operations&&!f.begins);}
    for(unsigned i:{1u,count/2,count}){Backend f;f.cancelAt=i;
        assert(!run(f,result)&&result.error==r::Error::cancelled&&f.operations==i&&!f.active&&f.endSuccess.back()==false);}
    {Backend f;f.bb[r::R_DPD_BF]=(22<<12)|22;r::Initialization<Backend> c(f,1);assert(c.initialize()&&c.dpdBackoff()==0x7f);
        assert((f.bb[0x81bc]&0x7fffff)==0x7f7f7f&&(f.bb[0x82bc]&0x7fffff)==0x7f7f7f);}
    printf("PASS: initial RTL8852B RCK/DACK/RXDCK model, %u I/O failures, calibration tables/state, bounded timeouts and mandatory leases; not hardware RFK\n",count);
}
