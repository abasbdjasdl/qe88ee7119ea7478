// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/ChannelProgramming.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
namespace c=rtl8852be::channel;
namespace r=rtl8852be::rfk;
struct Backend {
    std::map<unsigned,unsigned> bb,mac,radio[2],rfWrites[2];
    unsigned operations{},failAt{},cancelAt{},delays{},failDelay{},begins{},ends{},failBegin{},failEnd{},drains{},failDrain{};
    unsigned ignoredBb=~0u,ignoredMac=~0u,ignoredRf=~0u,ignoredPath{},lockAfter{},lockReads{};
    bool active{},cancel{},busy{},frozen{},backward{},neverLock{},failRecovery{},endSuccess{};
    uint64_t clock=100;
    Backend(){mac[c::R_AX_CMAC_FUNC_EN]=c::B_AX_CMAC_EN;mac[c::R_AX_PPDU_STAT]=c::B_AX_PPDU_STAT_RPT_EN;
        mac[c::R_AX_WMAC_RFMOD]=0xa0;mac[c::R_AX_TXRATE_CHK]=0x80;mac[c::R_AX_HW_RPT_FWD]=0xaaaa0000;
        bb[c::R_CHBW_MOD_V1]=c::fieldPrep(c::B_ANT_RX_SEG0,3);
        bb[c::R_RSTB_ASYNC]=c::fieldPrep(c::B_RSTB_ASYNC_ALL,1);
        for(unsigned p=0;p<2;++p){radio[p][c::RR_LDO]=0x42;radio[p][c::RR_CFGCH]=0;radio[p][c::RR_CFGCH_V1]=0;}}
    bool step(){assert(active);++operations;if(operations==cancelAt)cancel=true;return operations!=failAt;}
    bool cancelled(){return cancel;}uint64_t nowUs(){return backward&&delays?--clock:clock;}
    bool delayUs(unsigned us){assert(active);++delays;if(!frozen)clock+=us;return delays!=failDelay;}
    bool leaseActive(){return active;}
    bool begin(r::Kind k){assert(k==r::Kind::channel&&!active);++begins;if(begins==failBegin)return false;active=true;return true;}
    bool end(r::Kind k,bool success){assert(active&&k==r::Kind::channel);++ends;endSuccess=success;
        if(ends==failEnd||(!success&&failRecovery))return false;active=false;return true;}
    bool drain(){assert(active);return ++drains!=failDrain;}
    bool readRf(uint8_t p,unsigned a,unsigned m,unsigned &v){if(!step())return false;assert(p<2);
        if(a==c::RR_SYNFB){++lockReads;v=neverLock||lockReads<=lockAfter?0:1;return true;}
        v=c::fieldGet(m,a==c::RR_LPF&&busy?c::RR_LPF_BUSY:radio[p][a]);return true;}
    bool writeRf(uint8_t p,unsigned a,unsigned m,unsigned v){if(!step())return false;assert(p<2);++rfWrites[p][a];
        if(a!=ignoredRf||p!=ignoredPath)radio[p][a]=(radio[p][a]&~m)|c::fieldPrep(m,v);return true;}
    bool readBb(unsigned a,unsigned &v){if(!step())return false;v=bb[a];return true;}
    bool writeBb(unsigned a,unsigned v){if(!step())return false;if(a!=ignoredBb)bb[a]=v;return true;}
    bool readChannelMac8(unsigned a,uint8_t &v){assert(c::macByteAddress(a));if(!step())return false;v=mac[a];return true;}
    bool writeChannelMac8(unsigned a,uint8_t v){assert(c::macByteAddress(a));if(!step())return false;if(a!=ignoredMac)mac[a]=v;return true;}
    bool readChannelMac32(unsigned a,unsigned &v){assert(c::macWordAddress(a,false));if(!step())return false;v=mac[a];return true;}
    bool writeChannelMac32(unsigned a,unsigned v){assert(c::macWordAddress(a,true));if(!step())return false;if(a!=ignoredMac)mac[a]=v;return true;}
};
void configure(c::ChannelProgramming<Backend> &p,bool monitor=false){
    rtl8852be::network::BasebandGain gain;rtl8852be::network::BoardCalibration board;rtl8852be::network::PhyCalibration phy;
    board.identityValid=true;board.gainOffsetValid=true;phy.gainCompValid=true;
    for(unsigned band=0;band<4;++band)for(unsigned path=0;path<2;++path){
        for(unsigned i=0;i<7;++i)gain.lna_gain[band][path][i]=int(i)-4+int(path)+int(band);
        for(unsigned i=0;i<2;++i)gain.tia_gain[band][path][i]=-7+int(i)+int(path);
        gain.rpl_ofst_20[band][path]=-13+int(path);
        for(unsigned i=0;i<9;++i)gain.rpl_ofst_40[band][path][i]=int(i)-4;
        for(unsigned i=0;i<13;++i)gain.rpl_ofst_80[band][path][i]=int(i)-5;
    }
    for(unsigned pth=0;pth<2;++pth)for(unsigned i=0;i<5;++i){board.gainOffset[pth][i]=int(i)-3;phy.gainComp[pth][i]=-1;}
    assert(p.configure(gain,board,phy,-17,23,3,monitor));
    assert(!p.configure(gain,board,phy,-17,23,3,monitor));
}
bool run(Backend &b,c::Result &result,c::Channel ch={1,2,42,36}){
    c::ChannelProgramming<Backend> p(b);configure(p);const bool ok=p.program(ch)&&p.finish();result=p.result;return ok;
}
int main(){
    Backend golden;c::ChannelProgramming<Backend> p(golden);configure(p);
    assert(p.program({1,2,42,36}));const auto programCount=golden.operations;
    assert(golden.active&&p.result.stage==c::Stage::prepared&&p.result.registersProgrammed&&!p.result.receiversRestored);
    assert(!(golden.mac[c::R_AX_PPDU_STAT]&c::B_AX_PPDU_STAT_RPT_EN));
    assert(c::fieldGet(c::B_ADC_FIFO_RST,golden.bb[c::R_ADC_FIFO])==0xf);
    assert(golden.mac[c::R_AX_WMAC_RFMOD]==0xa2&&golden.mac[c::R_AX_TX_SUB_CARRIER_VALUE]==0xa4);
    assert(golden.mac[c::R_AX_TXRATE_CHK]==0x83);
    assert(c::fieldGet(c::B_CHBW_MOD_PRICH,golden.bb[c::R_CHBW_MOD_V1])==4);
    assert(c::fieldGet(c::B_CH_IDX_SEG0,golden.bb[c::R_MAC_PIN_SEL])==0x20);
    // Signed gain records must keep their byte representation in actual fields.
    assert(c::fieldGet(0x00ff0000,golden.bb[0x45dc])==253);
    assert(c::fieldGet(c::B_P0_RPL1_20_MASK,golden.bb[c::R_P0_RPL1])==244);
    assert(!p.program({0,0,6,6})&&golden.operations==programCount);
    assert(p.finish()&&!golden.active&&p.result.ownershipReleased&&p.result.receiversRestored);
    const auto count=golden.operations,delayCount=golden.delays;
    assert(golden.mac[c::R_AX_HW_RPT_FWD]==0xaaaa0001);
    assert(c::fieldGet(c::B_ADC_FIFO_RST,golden.bb[c::R_ADC_FIFO])==0);
    const c::Channel channels[]={{0,0,1,1},{0,0,14,14},{0,1,3,1},{0,1,11,13},
        {1,0,36,36},{1,0,177,177},{1,1,38,36},{1,1,175,177},
        {1,2,42,36},{1,2,42,40},{1,2,42,44},{1,2,42,48},{1,2,171,177}};
    for(auto ch:channels){Backend b;c::ChannelProgramming<Backend> x(b);configure(x,true);
        assert(x.program(ch)&&x.finish());
        assert(c::fieldGet(c::B_PATH0_BAND_SEL_MSK_V1,b.bb[c::R_PATH0_BAND_SEL_V1])==unsigned(ch.band==0));
        assert(c::fieldGet(c::B_ENABLE_CCK,b.bb[c::R_UPD_CLK_ADC])==unsigned(ch.band==0));
        assert(!(b.bb[c::R_PKT_CTRL]&c::B_PKT_POP_EN));
        for(unsigned path=0;path<2;++path){assert(b.radio[path][c::RR_LDO]==0x42);
            for(auto a:{c::RR_CFGCH,c::RR_CFGCH_V1})assert(c::fieldGet(c::RR_CFGCH_CH,b.radio[path][a])==ch.center);}}
    c::Result result;
    for(unsigned i=1;i<=count;++i){Backend b;b.failAt=i;assert(!run(b,result));
        assert(result.error==c::Error::io&&b.operations==i&&result.requiresReset&&!b.active&&!result.registersProgrammed&&!result.receiversRestored);}
    for(unsigned i=1;i<=delayCount;++i){Backend b;b.failDelay=i;assert(!run(b,result)&&result.error==c::Error::io&&!b.active);}
    for(unsigned i:{1u,programCount,count/2,count}){Backend b;b.cancelAt=i;
        assert(!run(b,result)&&result.error==c::Error::cancelled&&!b.active&&!result.receiversRestored);}
    for(unsigned what=0;what<3;++what){Backend b;if(what==0)b.failBegin=1;if(what==1)b.failEnd=1;if(what==2)b.failDrain=1;
        assert(!run(b,result)&&result.requiresReset&&!result.receiversRestored);assert(b.active==(what==1));}
    for(bool frozen:{false,true}){Backend b;b.busy=true;b.frozen=frozen;
        assert(!run(b,result)&&result.error==c::Error::timeout&&!b.active&&result.polls<=1001);}
    for(unsigned tries=1;tries<=3;++tries){Backend b;b.lockAfter=tries;assert(run(b,result));
        assert(b.rfWrites[0][c::RR_MMD]>=4&&b.lockReads>tries);}
    {Backend b;b.neverLock=true;assert(!run(b,result)&&result.error==c::Error::pllUnlocked&&!b.active);}
    {Backend b;b.backward=true;assert(!run(b,result)&&result.error==c::Error::clock&&!b.active);}
    for(auto a:{c::R_AX_WMAC_RFMOD,c::R_AX_TX_SUB_CARRIER_VALUE,c::R_AX_TXRATE_CHK,c::R_AX_PPDU_STAT}){Backend b;b.ignoredMac=a;
        assert(!run(b,result)&&result.error==c::Error::readback&&!b.active);}
    for(auto a:{c::R_FC0_BW_V1,c::R_CHBW_MOD_V1,c::R_MAC_PIN_SEL,c::R_ADC_FIFO,c::R_RSTB_ASYNC,
        c::R_PD_CTRL,c::R_RXCCA,c::R_S0_HW_SI_DIS,c::R_S1_HW_SI_DIS,c::R_P0_TSSI_TRK,c::R_P1_TSSI_TRK,
        c::R_P0_TXPW_RSTB,c::R_P1_TXPW_RSTB}){Backend b;b.ignoredBb=a;
        assert(!run(b,result)&&result.error==c::Error::readback&&!b.active);}
    for(unsigned path=0;path<2;++path)for(auto a:{c::RR_CFGCH,c::RR_CFGCH_V1}){Backend b;b.ignoredPath=path;b.ignoredRf=a;
        assert(!run(b,result)&&result.error==c::Error::readback&&!b.active);}
    for(auto a:{c::R_ADC_FIFO,c::R_PD_CTRL,c::R_P0_TSSI_TRK,c::R_P1_TSSI_TRK,c::R_P0_TXPW_RSTB,c::R_P1_TXPW_RSTB}){
        Backend b;c::ChannelProgramming<Backend> x(b);configure(x);assert(x.program({1,2,42,36}));
        b.ignoredBb=a;assert(!x.finish()&&x.result.error==c::Error::readback&&!b.active);}
    for(auto a:{c::R_AX_PPDU_STAT,c::R_AX_HW_RPT_FWD}){Backend b;c::ChannelProgramming<Backend> x(b);configure(x);
        assert(x.program({1,2,42,36}));b.ignoredMac=a;
        assert(!x.finish()&&x.result.error==c::Error::readback&&!b.active);}
    {Backend b;c::ChannelProgramming<Backend> x(b);assert(!x.program({0,0,1,1})&&!x.finish()&&!b.operations);
        configure(x);for(auto ch:{c::Channel{2,0,1,1},c::Channel{0,2,6,6},c::Channel{0,0,6,7},c::Channel{0,1,11,14},
            c::Channel{1,2,42,52},c::Channel{1,1,38,44},c::Channel{1,2,36,36}})
            assert(!x.program(ch)&&!b.operations);}
    {Backend b;c::ChannelProgramming<Backend> x(b);configure(x);assert(x.program({1,2,42,36}));
        b.failRecovery=true;assert(!x.abort()&&b.active&&!x.result.ownershipReleased);
        b.failRecovery=false;assert(x.abort()&&!b.active&&!x.result.receiversRestored);}
    {Backend b;c::ChannelProgramming<Backend> x(b);configure(x);assert(x.program({1,2,42,36}));b.clock+=3000000;
        assert(!x.finish()&&x.result.error==c::Error::timeout&&!b.active);}
    for(unsigned i=0;i<128;++i){golden.clock+=3000000;assert(p.program(channels[i%13])&&p.finish());}
    assert(p.result.operations>20000);
    printf("PASS: MAC/BB/DAV-DDV RF channel model, primary geometry, signed gain, held power-programming phase, %u I/O and %u delay faults, PLL retries/final failure and critical readback; not TX power or hardware tuning\n",count,delayCount);
}
