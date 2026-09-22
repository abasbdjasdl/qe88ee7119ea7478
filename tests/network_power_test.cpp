// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/TxPowerPlan.hpp"
#include <cassert>
#include <cstdio>
#include <map>
namespace p=rtl8852be::power;
namespace c=rtl8852be::channel;
struct PolicyState {std::map<unsigned,unsigned> calls;int ceiling=63;unsigned failChannel=255;};
bool query(void *ctx,uint8_t band,uint8_t ch,int16_t &v){
    auto &s=*static_cast<PolicyState *>(ctx);++s.calls[(band<<8)|ch];v=int16_t(s.ceiling);return ch!=s.failChannel;
}
p::Policy policy(PolicyState &s,unsigned domain=0){return {uint8_t(domain),9,&s,query};}
const p::Write &find(const p::TxPowerPlan &plan,unsigned address){
    for(unsigned i=0;i<plan.size();++i)if(plan[i].address==address)return plan[i];
    assert(false);return plan[0];
}
int byteAt(const p::TxPowerPlan &plan,unsigned start,unsigned offset){
    const unsigned value=(find(plan,start+(offset&~3u)).value>>((offset&3)*8))&255;
    return value<128?int(value):int(value)-256;
}
// Independent shape/page expectations: explicit hardware byte offsets, no calls
// to the imported fill functions or production lookup helpers.
int limit(unsigned band,unsigned bw,unsigned ntx,unsigned rs,unsigned bf,unsigned reg,unsigned ch,int ceiling){
    const unsigned ci=band==0?ch-1:ch<=64?(ch-36)/2:ch<=144?(ch-100)/2+15:(ch-149)/2+38;
    const unsigned stride=band==0?14:53;
    const auto *t=band==0?p::txpwr_lmt_2g:p::txpwr_lmt_5g;
    const unsigned index=((((bw*2+ntx)*3+rs)*2+bf)*16+reg)*stride+ci;
    int v=t[index];if(v==0)v=t[index-reg*stride];
    v=v>=0?v/2:(v-1)/2;return v<ceiling?v:ceiling;
}
int ruLimit(unsigned band,unsigned ru,unsigned ntx,unsigned reg,unsigned ch,int ceiling){
    const unsigned ci=band==0?ch-1:ch<=64?(ch-36)/2:ch<=144?(ch-100)/2+15:(ch-149)/2+38;
    const unsigned stride=band==0?14:53;
    const auto *t=band==0?p::txpwr_lmt_ru_2g:p::txpwr_lmt_ru_5g;
    const unsigned index=((ru*2+ntx)*16+reg)*stride+ci;
    int v=t[index];if(v==0)v=t[index-reg*stride];v=v>=0?v/2:(v-1)/2;return v<ceiling?v:ceiling;
}
void checkPage(const p::TxPowerPlan &plan,c::Channel ch,unsigned domain,int ceiling){
    for(unsigned ntx=0;ntx<2;++ntx){int bytes[40]{};
        auto pair=[&](unsigned pos,unsigned bw,unsigned rs,unsigned channel){
            for(unsigned bf=0;bf<2;++bf)bytes[pos+bf]=limit(ch.band,bw,ntx,rs,bf,domain,channel,ceiling);};
        pair(4,0,1,ch.primary);
        if(ch.width<2){pair(0,0,0,ch.center-(ch.width?2:0));pair(2,1,0,ch.center);}
        const unsigned n=1u<<ch.width;
        for(unsigned i=0;i<n;++i)pair(6+2*i,0,2,ch.center-2*(n-1)+4*i);
        if(ch.width>=1)for(unsigned i=0;i<n/2;++i)pair(22+2*i,1,2,ch.center-(ch.width==2?4:0)+8*i);
        if(ch.width==2){pair(30,2,2,ch.center);for(unsigned bf=0;bf<2;++bf)bytes[36+bf]=bytes[22+bf]<bytes[24+bf]?bytes[22+bf]:bytes[24+bf];}
        for(unsigned i=0;i<40;++i)assert(byteAt(plan,p::R_AX_PWR_LMT,ntx*40+i)==bytes[i]);
        for(unsigned ru=0;ru<3;++ru)for(unsigned i=0;i<8;++i){
            int expected=i<n?ruLimit(ch.band,ru,ntx,domain,ch.center-2*(n-1)+4*i,ceiling):0;
            assert(byteAt(plan,p::R_AX_PWR_RU_LMT,ntx*24+ru*8+i)==expected);}
    }
}
int main(){
    for(int i=-128;i<128;++i)assert(p::rfToMac(int8_t(i))==(i>=0?i/2:(i-1)/2));
    for(unsigned ch=0;ch<256;++ch){
        assert((p::channelIndex(0,ch)>=0)==(ch>=1&&ch<=14));
        assert((p::channelIndex(1,ch)>=0)==((ch>=36&&ch<=64&&ch%2==0)||(ch>=100&&ch<=144&&ch%2==0)||(ch>=149&&ch<=177&&ch%2==1)));
        assert(p::channelIndex(2,ch)<0);}
    p::TxPowerPlan plan;PolicyState state;
    assert(plan.build({0,0,1,1},policy(state))&&plan.size()==58);
    assert(find(plan,p::R_AX_PWR_BY_RATE).value==0x28282828);
    assert(find(plan,p::R_AX_PWR_BY_RATE+8).value==0x24262828);
    assert(find(plan,p::R_AX_PWR_LMT).value==0x0000001c); // absent BF / ch1 CCK40 entries stay zero
    assert(find(plan,0x5804).value==find(plan,0x7808).value);
    assert(find(plan,p::R_AX_PWR_RATE_OFST_CTRL).mask==0xfffff&&find(plan,p::R_AX_PWR_RATE_OFST_CTRL).value==0);
    assert(find(plan,p::R_TXFIR0).value==0x023d23ff); // world flat CCK
    assert(plan.build({0,0,1,1},policy(state,p::RTW89_FCC)));
    assert(find(plan,p::R_TXFIR0).value==0x023d83ff&&find(plan,p::R_DCFO_OPT).value==0x03000000);
    assert(plan.build({0,0,14,14},policy(state))&&find(plan,p::R_TXFIR0).value==0x023b13ff);
    unsigned plans=0;
    // All legal geometries, every imported domain, and signed/global ceilings.
    for(unsigned band=0;band<2;++band)for(unsigned width=0;width<3;++width)for(unsigned center=1;center<=177;++center)
    for(int distance=-6;distance<=6;++distance){
        c::Channel ch{uint8_t(band),uint8_t(width),uint8_t(center),uint8_t(int(center)+distance)};
        if(!c::validChannel(ch))continue;
        for(unsigned domain=0;domain<16;++domain)for(int cap:{63,17,-1,-64}){
            state.calls.clear();state.ceiling=cap;
            assert(plan.build(ch,policy(state,domain)));assert(plan.size()==(band?50u:58u));
            checkPage(plan,ch,domain,cap);
            for(auto entry:state.calls)assert(entry.second==1); // immutable cached per-frequency policy
            std::map<unsigned,unsigned> addresses;
            for(unsigned i=0;i<plan.size();++i){const auto &w=plan[i];assert(++addresses[w.address]==1);
                assert(!(w.value&~w.mask));assert(w.baseband||p::macAddress(w.address));}
            ++plans;
        }
    }
    state.ceiling=63;
    for(unsigned ch:{36u,38u,40u,42u,44u,46u,48u}){state.failChannel=ch;
        assert(!plan.build({1,2,42,36},policy(state))&&plan.error()==p::PlanError::policy&&plan.size()==0);}
    state.failChannel=255;
    for(int invalid:{-65,64,32767}){state.ceiling=invalid;assert(!plan.build({0,0,1,1},policy(state))&&plan.size()==0);}
    assert(!plan.build({0,0,1,1},{})&&plan.size()==0);
    auto pol=policy(state);pol.domain=16;assert(!plan.build({0,0,1,1},pol));
    pol=policy(state);pol.generation=0;assert(!plan.build({0,0,1,1},pol));
    for(auto ch:{c::Channel{2,0,1,1},c::Channel{1,3,50,36},c::Channel{0,2,6,6},c::Channel{1,2,42,52}})
        assert(!plan.build(ch,policy(state))&&plan.error()==p::PlanError::channel&&plan.size()==0);
    printf("PASS: %u power plans, all supported channel geometries / 16 domains / signed ceilings, exact AX pages, shape and rate goldens, policy failures; no regulatory authorization or hardware power measurement\n",plans);
}
