// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/InitialChannelPolicy.hpp"
#include <assert.h>
#include <stdio.h>
using namespace rtl8852be;
int main(){
    network::InitialChannelPolicy p;power::TxPowerPlan plan;
    assert(!p.allows({0,0,1,1})&&!plan.build({0,0,1,1},p.powerPolicy()));
    assert(p.initialize(42)&&!p.initialize(43));
    for(unsigned band=0;band<3;++band)for(unsigned width=0;width<3;++width)for(unsigned n=0;n<256;++n){
        channel::Channel c{uint8_t(band),uint8_t(width),uint8_t(n),uint8_t(n)};
        assert(p.allows(c)==(band==0&&width==0&&n>=1&&n<=11));
    }
    for(uint8_t n=1;n<=11;++n){assert(plan.build({0,0,n,n},p.powerPolicy()));
        for(unsigned i=0;i<plan.size();++i){const auto &w=plan[i];
            if(w.address>=power::R_AX_PWR_LMT&&w.address<power::R_AX_PWR_LMT+80)
                for(unsigned b=0;b<4;++b)assert(int8_t(uint8_t(w.value>>(b*8)))<=0);}}
    p.invalidate();assert(!p.allows({0,0,1,1})&&!plan.build({0,0,1,1},p.powerPolicy()));
    puts("PASS: initial 2.4 GHz channel policy rejects unauthorized geometry and enforces actual power ceilings");
}
