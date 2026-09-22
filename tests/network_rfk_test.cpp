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
    unsigned oneshots{},failOneshot{},failCommand{},failCommandIndex{},deferredShots{};bool shotActive{};
    std::vector<bool> shotStarts;std::vector<unsigned> shotMaps,commands;
    unsigned arms{},stops{},failArm{},failStop{},noCwPath=2;
    bool txArmed{},stopAlwaysFails{};r::Kind kind{};
    unsigned cwRequests[2]{},cwWait[2]{},pmacStarts{},pmacStops{};
    int alignmentDefault[2][3]{};
    unsigned dpkCorrelation=200,dpkDcI=10,dpkDcQ=12,dpkGain=0x556,dpkLoss=2;
    unsigned dpkCommand{};bool corruptDpkDone{},corruptDpkAck{};
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
            rf[p][r::RR_RCKS]=8;rf[p][r::RR_DCK]=2;
            rf[p][r::RR_TM]=r::fieldPrep(r::RR_TM_VAL,30+p);
            rf[p][r::RR_TXMO]=(0x10u<<r::shift(r::RR_TXMO_COI))|(0x10u<<r::shift(r::RR_TXMO_COQ));
            rf[p][r::RR_LOKVB]=(0x10u<<r::shift(r::RR_LOKVB_COI))|(0x10u<<r::shift(r::RR_LOKVB_COQ));}
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
        if(kind==r::Kind::dpk&&a==r::R_RPT_COM){
            const unsigned select=r::fieldGet(r::B_KIP_RPT1_SEL,bb[r::R_KIP_RPT1]);
            if(select==3)v=corruptDpkAck?0:0x8000;
            else if(select==0)v=r::fieldPrep(r::B_PRT_COM_CORI,3)|r::fieldPrep(r::B_PRT_COM_CORV,dpkCorrelation)|r::fieldPrep(r::B_PRT_COM_DCI,dpkGain);
            else if(select==9)v=r::fieldPrep(r::B_PRT_COM_DCI,dpkDcI)|r::fieldPrep(r::B_PRT_COM_DCQ,dpkDcQ);
            else if(select==6)v=(bb[r::R_DPK_CFG2]&r::B_DPK_CFG2_ST)?r::fieldPrep(r::B_PRT_COM_GL,dpkLoss):0x00010001;
        }
        if(kind==r::Kind::dpk&&a==0xbff8&&corruptDpkDone)v=0;
        for(unsigned p=0;p<2;++p)if(a==r::_tssi_cw_rpt_addr[p]&&cwWait[p]){--cwWait[p];v&=~r::B_TSSI_CWRPT_RDY;}
        const unsigned results[]={r::R_DACK_S0P2,r::R_DACK_S0P3,r::R_DACK10S,r::R_DACK11S};
        const unsigned selects[]={r::R_DCOF0,r::R_DCOF8,r::R_DACK10,r::R_DACK11};
        for(unsigned i=0;i<4;++i)if(a==results[i])v|=(0x20u+i*0x10+((bb[selects[i]]>>1)&15))<<24;
        if(a==notReady)v=0;return true;
    }
    bool writeBb(unsigned a,unsigned v){
        if(!step()){failedWrite=true;return false;}bb[a]=v;
        if(a==r::R_NCTL_CFG){commands.push_back(v);bb[0xbff8]=0x55;
            if(kind==r::Kind::dpk)dpkCommand=v;
            bb[r::R_NCTL_RPT]=((failCommand&&v==failCommand)||commands.size()==failCommandIndex)?r::B_NCTL_RPT_FLG:0;}
        if(a==r::R_PMAC_TX_CTRL&&(v&r::B_PMAC_TXEN_DIS)){
            assert(txArmed);++pmacStarts;const unsigned path=r::fieldGet(r::B_TXPATH_SEL_MSK,bb[r::R_TXPATH_SEL])==2?1:0;
            const bool second=cwRequests[path]++%2;
            if(!second)for(unsigned i=0;i<3;++i)alignmentDefault[path][i]=r::sign_extend32(
                r::fieldGet(r::_tssi_cw_default_mask[i+1],bb[r::_tssi_cw_default_addr[path][i+1]]),8);
            const unsigned cw=second?140:200;
            cwWait[path]=2;
            bb[r::_tssi_cw_rpt_addr[path]]=r::fieldPrep(r::B_TSSI_CWRPT,cw)|(path==noCwPath?0:r::B_TSSI_CWRPT_RDY);
            bb[r::R_TX_COUNTER]+=100;
        }
        return true;
    }
    bool writeMac(unsigned a,unsigned v){if(!step()){failedWrite=true;return false;}mac[a]=v;return true;}
    bool cancelled(){return cancel;}
    uint64_t nowUs(){return backwards&&delayCalls?--clock:clock;}
    bool delayUs(unsigned us){assert(active);++delayCalls;if(!frozen)clock+=us;return delayCalls!=failDelay;}
    bool begin(r::Kind k){assert(!active);++begins;if(begins==failBegin)return false;active=true;kind=k;return true;}
    bool end(r::Kind,bool success){assert(active);++ends;endSuccess.push_back(success);if(ends==failEnd)return false;
        if(txArmed&&!stopCalibrationTx())return false;
        if(shotActive)++deferredShots; // native end recovers before this release
        shotActive=false;active=false;return true;}
    bool oneshot(r::Kind k,uint8_t map,bool start){
        assert(active&&k==kind&&(k==r::Kind::iqk||k==r::Kind::tssi));
        if(!start)assert(!txArmed);++oneshots;shotStarts.push_back(start);shotMaps.push_back(map);
        assert(start!=shotActive);if(oneshots==failOneshot)return false;shotActive=start;return true;
    }
    bool armCalibrationTx(){assert(active&&kind==r::Kind::tssi&&shotActive&&!txArmed);
        if(++arms==failArm)return false;txArmed=true;return true;}
    bool stopCalibrationTx(){if(!txArmed)return true;++stops;if(stops==failStop||stopAlwaysFails)return false;
        assert(active&&kind==r::Kind::tssi);bb[r::R_PMAC_TX_PRD]&=~(r::B_PMAC_CTX_EN|r::B_PMAC_PTX_EN);
        txArmed=false;++pmacStops;return true;}
    bool drain(){assert(active);return ++drains!=failDrain;}
};
bool run(Backend &b,r::Result &result){r::Initialization<Backend> cal(b,1);bool ok=cal.initialize();result=cal.result;return ok;}
void testAsynchronousSteps(){
    struct AdmittedBackend:Backend {
        bool admitted{};r::Kind expected{};
        void admit(r::Kind k){assert(!active&&!admitted);expected=k;admitted=true;clock+=3000000;}
        bool begin(r::Kind k){assert(admitted&&expected==k);admitted=false;return Backend::begin(k);}
    } b;
    r::Initialization<AdmittedBackend> c(b,1);const r::Channel home{1,2,42};
    assert(!c.initializeDack()&&!c.initializeRxDc()&&!c.calibrateIqOnly(home)&&!b.begins);
    b.admit(r::Kind::rck);assert(c.initializeRck()&&!b.active&&!b.admitted&&b.begins==1&&b.ends==1);
    assert(!c.initializeRck()&&!c.initializeRxDc()&&b.begins==1);
    b.admit(r::Kind::dack);assert(c.initializeDack()&&!b.active&&!b.admitted&&b.begins==2&&b.ends==2);
    b.admit(r::Kind::rxDc);assert(c.initializeRxDc()&&!b.active&&!b.admitted&&b.begins==3&&b.ends==3);
    assert(c.result.stage==r::Stage::complete&&!c.calibrateIqOnly(home));
    b.admit(r::Kind::rxDc);assert(c.calibrateRxDc(home)&&!c.result.iqReady&&!b.active&&b.begins==4);
    // A successful DC measurement for one channel cannot authorize IQK for another.
    assert(!c.calibrateIqOnly({0,0,6})&&b.begins==4);
    b.admit(r::Kind::iqk);assert(c.calibrateIqOnly(home)&&c.result.iqReady&&!b.active&&b.begins==5&&b.ends==5);
    assert(!c.calibrateIqOnly(home)&&b.begins==5); // consumed; requires a new RXDCK
    Backend sync;r::Initialization<Backend> golden(sync,1);
    assert(golden.initialize()&&golden.calibrateIq(home));
    assert(b.bb==sync.bb&&b.mac==sync.mac&&b.rf[0]==sync.rf[0]&&b.rf[1]==sync.rf[1]);
    assert(b.operations==sync.operations&&b.commands==sync.commands);
    puts("PASS: independently admitted RFK stages return between leases, enforce order/channel/one-use DC result, and match synchronous hardware writes");
}
void testIq(){
    const r::Channel channels[]={{0,0,1},{0,0,14},{0,1,3},{0,1,11},{1,0,36},{1,0,177},{1,1,38},{1,1,175},{1,2,42},{1,2,171}};
    unsigned maxOps=0;
    for(auto channel:channels){
        Backend b;r::Initialization<Backend> c(b,1);assert(c.initialize());
        for(auto a:r::rtw8852b_backup_bb_regs)b.bb[a]=0x12345678;
        for(unsigned p=0;p<2;++p)for(auto a:r::rtw8852b_backup_rf_regs)b.rf[p][a]=0x30031+p;
        const auto beforeBb=b.bb,beforeA=b.rf[0],beforeB=b.rf[1];const auto base=b.operations;
        assert(c.calibrateIq(channel));maxOps=b.operations-base>maxOps?b.operations-base:maxOps;
        assert(c.result.iqReady&&c.result.ownershipReleased&&!b.active&&!b.shotActive);
        assert(c.result.iqChannel.center==channel.center&&c.result.stage==r::Stage::complete);
        assert((b.shotStarts==std::vector<bool>{true,false,true,false}));
        for(auto m:b.shotMaps)assert(m==(channel.band?0x53u:0x13u));
        for(auto a:r::rtw8852b_backup_bb_regs)assert(b.bb[a]==beforeBb.at(a));
        for(auto a:r::rtw8852b_backup_rf_regs){assert(b.rf[0][a]==beforeA.at(a));assert(b.rf[1][a]==beforeB.at(a));}
        for(unsigned p=0;p<2;++p){assert(c.iqk().iqk_ch[p]==channel.center&&c.iqk().iqk_bw[p]==channel.width);
            assert(!c.iqk().lok_fail[p]&&!c.iqk().iqk_tx_fail[0][p]&&!c.iqk().iqk_rx_fail[0][p]);}
    }
    const r::Channel target{1,2,42};Backend golden;r::Initialization<Backend> g(golden,1);
    assert(g.initialize());const auto initial=golden.operations,initialDelay=golden.delayCalls;
    assert(g.calibrateIq(target));const auto count=golden.operations-initial,delays=golden.delayCalls-initialDelay;
    for(unsigned i=1;i<=count;++i){Backend b;r::Initialization<Backend> c(b,1);assert(c.initialize());
        b.failAt=b.operations+i;assert(!c.calibrateIq(target));
        assert(c.result.error==r::Error::io&&b.operations==b.failAt&&!c.result.iqReady&&c.result.requiresReset);
        assert(!b.active&&!b.shotActive&&!b.endSuccess.back());
        const auto stopped=b.operations;assert(!c.calibrateIq(target)&&b.operations==stopped);
    }
    for(unsigned i=1;i<=delays;++i){Backend b;r::Initialization<Backend> c(b,1);assert(c.initialize());
        b.failDelay=b.delayCalls+i;assert(!c.calibrateIq(target)&&c.result.error==r::Error::io&&!b.active&&!c.result.iqReady);}
    for(unsigned i=1;i<=4;++i){Backend b;r::Initialization<Backend> c(b,1);assert(c.initialize());
        b.failOneshot=i;assert(!c.calibrateIq(target)&&c.result.error==r::Error::io&&!b.active&&!c.result.iqReady);}
    for(auto command:golden.commands){Backend b;r::Initialization<Backend> c(b,1);assert(c.initialize());
        b.failCommand=command;assert(!c.calibrateIq(target));
        assert(c.result.error==r::Error::calibration&&!c.result.iqReady&&!b.active&&!b.endSuccess.back());}
    // A transient LOK error must retry the affected path, including either
    // VBUFFER command. Individual TX/RX groups and restore cannot be ignored.
    for(unsigned i=0;i<golden.commands.size();++i){Backend b;r::Initialization<Backend> c(b,1);assert(c.initialize());
        b.failCommandIndex=i+1;const auto type=(golden.commands[i]>>8)&15;
        const bool retriable=type>=1&&type<=3;
        assert(c.calibrateIq(target)==retriable&&c.result.iqReady==retriable);
        if(retriable)assert(b.commands.size()==golden.commands.size()+4&&c.result.error==r::Error::none);
        else assert(c.result.error==r::Error::calibration);
    }
    for(unsigned p=0;p<2;++p)for(auto addr:{r::RR_TXMO,r::RR_LOKVB}){Backend b;r::Initialization<Backend> c(b,1);
        assert(c.initialize());b.rf[p][addr]=0;assert(!c.calibrateIq(target)&&c.result.error==r::Error::calibration&&!c.result.iqReady);}
    for(bool frozen:{false,true}){Backend b;r::Initialization<Backend> c(b,1);assert(c.initialize());
        b.notReady=0xbff8;b.frozen=frozen;assert(!c.calibrateIq(target)&&c.result.error==r::Error::timeout&&!b.active);
        assert(b.oneshots==1&&b.deferredShots==1);}
    {Backend b;r::Initialization<Backend> c(b,1);assert(c.initialize());b.backwards=true;
        assert(!c.calibrateIq(target)&&c.result.error==r::Error::clock&&!b.active);}
    for(unsigned i:{1u,count/2,count}){Backend b;r::Initialization<Backend> c(b,1);assert(c.initialize());
        b.cancelAt=b.operations+i;assert(!c.calibrateIq(target)&&c.result.error==r::Error::cancelled&&!b.active&&!c.result.iqReady);}
    {Backend b;r::Initialization<Backend> c(b,1);assert(c.initialize());b.failBegin=4;
        assert(!c.calibrateIq(target)&&c.result.requiresReset&&!b.active);}
    {Backend b;r::Initialization<Backend> c(b,1);assert(c.initialize());b.failEnd=4;
        assert(!c.calibrateIq(target)&&c.result.requiresReset&&b.active&&!c.result.ownershipReleased);}
    {Backend b;r::Initialization<Backend> c(b,1);assert(c.initialize());b.failDrain=4;
        assert(!c.calibrateIq(target)&&c.result.error==r::Error::io&&!b.active&&!b.endSuccess.back());}
    {Backend b;r::Initialization<Backend> c(b,1);assert(!c.calibrateIq(target)&&b.operations==0);assert(c.initialize());
        const auto before=b.operations;
        for(auto ch:{r::Channel{0,2,6},r::Channel{0,1,14},r::Channel{1,0,37},r::Channel{1,2,36},r::Channel{2,0,1}})
            assert(!c.calibrateIq(ch)&&b.operations==before);
        // Each calibration gets a fresh finite budget; ordinary channel changes
        // must not exhaust a lifetime I/O limit or a two-second boot deadline.
        for(unsigned i=0;i<512;++i){b.clock+=3000000;assert(c.calibrateIq(channels[i%10]));}
        assert(c.result.operations>200000);
    }
    printf("PASS: dual-path IQK 2G/5G 20/40/80 model, %u I/O failures, %u delays, all command failures, restored registers and repeated channels (max %u ops); not hardware IQK\n",count,delays,maxOps);
}
void configure(r::Initialization<Backend> &c){
    rtl8852be::network::BoardCalibration board;rtl8852be::network::PhyCalibration phy;
    board.identityValid=true;board.gainOffsetValid=true;phy.gainCompValid=true;
    for(unsigned p=0;p<2;++p){board.thermal[p]=30+p;
        for(unsigned i=0;i<6;++i)board.tssiCck[p][i]=int(i)-3+int(p);
        for(unsigned i=0;i<19;++i)board.tssiMcs[p][i]=int(i)-9+int(p);
        for(unsigned i=0;i<8;++i)phy.tssiTrim[p][i]=int(i)-4+int(p);
        for(unsigned i=0;i<5;++i){board.gainOffset[p][i]=int(i)-3;phy.gainComp[p][i]=int(p)-2;}}
    assert(c.configureCalibration(board,phy,-17,23,2));
    assert(!c.configureCalibration(board,phy,-17,23,2));
}
void prepareTssi(Backend &b,r::Initialization<Backend> &c,r::Channel channel={1,2,42}){
    configure(c);assert(c.initialize());assert(c.calibrateIq(channel));
    b.bb[r::R_TXPATH_SEL]=r::fieldPrep(r::B_TXPATH_SEL_MSK,3);
    b.bb[r::R_CHBW_MOD_V1]=r::fieldPrep(r::B_ANT_RX_SEG0,3);
    b.bb[r::R_TXPWR]=r::fieldPrep(r::B_TXPWR_MSK,unsigned(-12));
}
void testTssi(){
    Backend golden;r::Initialization<Backend> g(golden,1);prepareTssi(golden,g);
    const auto base=golden.operations,delayBase=golden.delayCalls;
    assert(g.calibrateTssi());const auto count=golden.operations-base,delays=golden.delayCalls-delayBase;
    assert(g.result.tssiReady&&!golden.active&&!golden.txArmed&&golden.arms==4&&golden.pmacStarts==4&&golden.pmacStops==4);
    assert(r::fieldGet(r::B_TXPATH_SEL_MSK,golden.bb[r::R_TXPATH_SEL])==3);
    assert(r::fieldGet(r::B_ANT_RX_SEG0,golden.bb[r::R_CHBW_MOD_V1])==3);
    assert(r::sign_extend32(r::fieldGet(r::B_TXPWR_MSK,golden.bb[r::R_TXPWR]),8)==-12);
    for(unsigned p=0;p<2;++p){assert(g.tssi().alignment_done[p][r::TSSI_ALIMK_5GL]);
        assert(g.tssi().check_backup_aligmk[p][17]); // ch42 -> (42-36)/2 +14
        const int expected=(int(5)-9+int(p)+int(6)-9+int(p))/2+(2-4+int(p));
        assert(r::sign_extend32(r::fieldGet(r::_TSSI_DE_MASK,golden.bb[r::_tssi_de_mcs_80m[p]]),9)==expected);
        assert(r::fieldGet(p?r::B_P1_TSSI_EN:r::B_P0_TSSI_EN,golden.bb[p?r::R_P1_TSSI_AVG:r::R_P0_TSSI_AVG])==1);
        const unsigned masks[]={r::B_P1_TSSI_ALIM11,r::B_P1_TSSI_ALIM12,r::B_P1_TSSI_ALIM13};
        for(unsigned i=0;i<3;++i)assert(r::sign_extend32(r::fieldGet(masks[i],golden.bb[r::R_P0_TSSI_ALIM1+(p<<13)]),8)==golden.alignmentDefault[p][i]+4);
    }
    assert(g.calibrateTssi()&&golden.arms==4); // use measured same-channel alignment
    for(unsigned i=1;i<=count;++i){Backend b;r::Initialization<Backend> c(b,1);prepareTssi(b,c);b.failAt=b.operations+i;
        assert(!c.calibrateTssi()&&c.result.error==r::Error::io&&b.operations==b.failAt);
        assert(!c.result.tssiReady&&!b.active&&!b.txArmed&&!b.shotActive&&!b.endSuccess.back());}
    for(unsigned i=1;i<=delays;++i){Backend b;r::Initialization<Backend> c(b,1);prepareTssi(b,c);b.failDelay=b.delayCalls+i;
        assert(!c.calibrateTssi()&&c.result.error==r::Error::io&&!b.active&&!b.txArmed);}
    for(unsigned i=1;i<=4;++i){Backend b;r::Initialization<Backend> c(b,1);prepareTssi(b,c);b.failArm=i;
        assert(!c.calibrateTssi()&&c.result.error==r::Error::io&&!b.active&&!b.txArmed);
        Backend s;r::Initialization<Backend> sc(s,1);prepareTssi(s,sc);s.failStop=i;
        assert(!sc.calibrateTssi()&&sc.result.error==r::Error::io&&!s.active&&!s.txArmed);}
    {Backend b;r::Initialization<Backend> c(b,1);prepareTssi(b,c);b.stopAlwaysFails=true;
        assert(!c.calibrateTssi()&&c.result.requiresReset&&b.active&&b.txArmed&&b.shotActive&&!c.result.ownershipReleased);}
    for(unsigned p=0;p<2;++p)for(bool frozen:{false,true}){Backend b;r::Initialization<Backend> c(b,1);prepareTssi(b,c);
        const auto shots=b.oneshots;b.noCwPath=p;b.frozen=frozen;
        assert(!c.calibrateTssi()&&c.result.error==r::Error::timeout&&!b.txArmed&&!b.active);
        assert(b.oneshots==shots+1&&b.deferredShots==1);}
    for(unsigned i:{1u,count/2,count}){Backend b;r::Initialization<Backend> c(b,1);prepareTssi(b,c);b.cancelAt=b.operations+i;
        assert(!c.calibrateTssi()&&c.result.error==r::Error::cancelled&&!b.txArmed&&!b.active);}
    for(unsigned i=1;i<=2;++i){Backend b;r::Initialization<Backend> c(b,1);prepareTssi(b,c);b.failOneshot=b.oneshots+i;
        assert(!c.calibrateTssi()&&c.result.error==r::Error::io&&!b.txArmed&&!b.active);}
    {Backend b;r::Initialization<Backend> c(b,1);prepareTssi(b,c);b.failBegin=b.begins+1;
        assert(!c.calibrateTssi()&&c.result.requiresReset&&!b.active);}
    {Backend b;r::Initialization<Backend> c(b,1);prepareTssi(b,c);b.failEnd=b.ends+1;
        assert(!c.calibrateTssi()&&c.result.requiresReset&&b.active&&!c.result.ownershipReleased);}
    {Backend b;r::Initialization<Backend> c(b,1);prepareTssi(b,c);b.failDrain=b.drains+1;
        assert(!c.calibrateTssi()&&c.result.error==r::Error::io&&!b.active&&!b.endSuccess.back());}
    {Backend b;r::Initialization<Backend> c(b,1);prepareTssi(b,c);b.backwards=true;
        assert(!c.calibrateTssi()&&c.result.error==r::Error::clock&&!b.active&&!b.txArmed);}
    {Backend b;r::Initialization<Backend> c(b,1);assert(!c.calibrateTssi()&&b.operations==0);
        assert(c.initialize()&&c.calibrateIq({1,2,42}));const auto n=b.operations;assert(!c.calibrateTssi()&&b.operations==n);}
    // Cover all thermal subbands and missing thermal calibration.
    for(auto ch:{r::Channel{0,0,1},r::Channel{0,1,11},r::Channel{1,0,100},r::Channel{1,1,151},r::Channel{1,2,171}}){
        Backend b;r::Initialization<Backend> c(b,1);prepareTssi(b,c,ch);assert(c.calibrateTssi()&&!b.txArmed);}
    {Backend b;r::Initialization<Backend> c(b,1);rtl8852be::network::BoardCalibration board;
        rtl8852be::network::PhyCalibration phy;board.identityValid=true;board.thermal[0]=board.thermal[1]=0xff;
        assert(c.configureCalibration(board,phy,0,0,0));assert(c.initialize()&&c.calibrateIq({0,0,6})&&c.calibrateTssi());
        for(unsigned i=0;i<64;i+=4)assert(b.bb[r::R_P0_TSSI_BASE+i]==0&&b.bb[r::R_TSSI_THOF+i]==0);}
    printf("PASS: TSSI dual-path measured alignment model, thermal/eFuse/gain, %u I/O and %u delay failures, PMAC cleanup after faults/cancel/timeout and lease retention; not measured RF power\n",count,delays);
}
void prepareDpk(Backend &b,r::Initialization<Backend> &c,r::Channel channel={1,2,42}){
    prepareTssi(b,c,channel);assert(c.calibrateTssi());
}
void testDpk(){
    Backend golden;r::Initialization<Backend> g(golden,1);prepareDpk(golden,g);
    const auto base=golden.operations,baseDelay=golden.delayCalls;
    assert(g.calibrateDpk());const auto count=golden.operations-base,delays=golden.delayCalls-baseDelay;
    assert(g.result.dpkReady&&!golden.active&&g.dpk().is_dpk_enable&&!g.dpk().is_dpk_reload_en);
    for(unsigned p=0;p<2;++p){const auto &b=g.dpk().bp[p][0];assert(b.path_ok&&b.ch==42&&b.bw==r::RTW89_CHANNEL_WIDTH_80);
        assert(b.txagc_dpk==0x36&&b.ther_dpk==30+p&&b.pwsf==0x78&&b.gs==0x5b);
        assert(g.dpk().corr_val[p][0]==200&&g.dpk().dc_i[p][0]==10&&g.dpk().dc_q[p][0]==12);
        assert((golden.bb[r::R_DPD_CH0A+(p<<8)]>>24)&1);
        assert(!(golden.bb[r::R_P0_TSSI_TRK+(p<<13)]&r::B_P0_TSSI_TRK_EN));}
    for(unsigned i=1;i<=count;++i){Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);b.failAt=b.operations+i;
        assert(!c.calibrateDpk()&&c.result.error==r::Error::io&&b.operations==b.failAt&&!c.result.dpkReady);
        assert(c.result.requiresReset&&!b.active&&!b.endSuccess.back());}
    for(unsigned i=1;i<=delays;++i){Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);b.failDelay=b.delayCalls+i;
        assert(!c.calibrateDpk()&&c.result.error==r::Error::io&&!c.result.dpkReady&&!b.active);}
    for(unsigned phase=0;phase<2;++phase)for(bool frozen:{false,true}){Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);
        b.corruptDpkDone=phase==0;b.corruptDpkAck=phase==1;b.frozen=frozen;
        assert(!c.calibrateDpk()&&c.result.error==r::Error::timeout&&!c.result.dpkReady&&!b.active);}
    for(unsigned what=0;what<4;++what){Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);
        if(what==0)b.dpkCorrelation=169;if(what==1)b.dpkDcI=201;if(what==2)b.dpkDcQ=4096-201;
        if(what==3)b.dpkGain=1923; // stays out of AGC range until bounded exhaustion
        assert(!c.calibrateDpk()&&c.result.error==r::Error::calibration&&!c.result.dpkReady&&!b.active);}
    // Constant extreme gain loss can converge at a valid TXAGC bound. This is
    // distinct from exhausting the search without the source's goout flag.
    for(unsigned loss:{0u,7u}){Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);b.dpkLoss=loss;
        assert(c.calibrateDpk()&&c.result.dpkReady&&!b.active);
        for(unsigned p=0;p<2;++p)assert(c.dpk().bp[p][0].txagc_dpk==(loss?0x2e:0x3f));}
    for(unsigned p=0;p<2;++p){Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);b.rf[p][r::RR_TM]=0;
        assert(!c.calibrateDpk()&&c.result.error==r::Error::calibration&&!c.result.dpkReady);}
    for(auto channel:{r::Channel{0,0,6},r::Channel{0,1,11},r::Channel{1,0,36},r::Channel{1,1,151}}){
        Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c,channel);assert(c.calibrateDpk());
        const unsigned order=channel.band?0:r::B_DPD_ORDER_V1;
        assert((b.bb[r::R_DPD_CH0A]&r::B_DPD_ORDER_V1)==order);}
    for(unsigned i:{1u,count/2,count}){Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);b.cancelAt=b.operations+i;
        assert(!c.calibrateDpk()&&c.result.error==r::Error::cancelled&&!b.active&&!c.result.dpkReady);}
    {Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);b.failBegin=b.begins+1;
        assert(!c.calibrateDpk()&&c.result.requiresReset&&!b.active);}
    {Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);b.failEnd=b.ends+1;
        assert(!c.calibrateDpk()&&c.result.requiresReset&&b.active&&!c.result.ownershipReleased);}
    {Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);b.failDrain=b.drains+1;
        assert(!c.calibrateDpk()&&c.result.error==r::Error::io&&!b.active&&!b.endSuccess.back());}
    {Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);b.backwards=true;
        assert(!c.calibrateDpk()&&c.result.error==r::Error::clock&&!b.active&&!c.result.dpkReady);}
    {Backend b;r::Initialization<Backend> c(b,1);assert(!c.calibrateDpk()&&!c.trackDpk()&&b.operations==0);}
    // Real temperature sampling comes from BB while TSSI is enabled. Two paths
    // have deliberately different deltas; neither may inherit the other's term.
    for(unsigned p=0;p<2;++p){golden.bb[0x1c10+(p<<13)]=unsigned(p?35:26)<<24;
        golden.bb[r::R_TXAGC_BB+(p<<13)]=1;golden.bb[r::R_TXAGC_TP+(p<<13)]=0;}
    const auto trackBase=golden.operations;assert(g.trackDpk());const auto trackCount=golden.operations-trackBase;
    assert(g.averageThermal(0)==26&&g.averageThermal(1)==35);
    assert(r::fieldGet(r::B_DPD_BND_0,golden.bb[r::R_DPD_BND])==130);
    assert(r::fieldGet(r::B_DPD_BND_1,golden.bb[r::R_DPD_BND+0x100])==110);
    golden.bb[0x1c10]=30u<<24;golden.bb[0x3c10]=0;
    assert(g.trackDpk()&&g.averageThermal(0)==27&&g.averageThermal(1)==35);
    // Full six-bit sensor range must not wrap through an eight-bit delta.
    for(unsigned baseline:{1u,63u}){Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);
        for(unsigned p=0;p<2;++p)b.rf[p][r::RR_TM]=r::fieldPrep(r::RR_TM_VAL,baseline);
        assert(c.calibrateDpk());const unsigned current=64-baseline;
        b.bb[0x1c10]=b.bb[0x3c10]=current<<24;b.bb[r::R_TXAGC_BB]=b.bb[r::R_TXAGC_BB+0x2000]=1;
        assert(c.trackDpk());const auto expected=unsigned(120+(int(baseline)-int(current))*5/2)&511;
        assert(r::fieldGet(r::B_DPD_BND_0,b.bb[r::R_DPD_BND])==expected);}
    {Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);assert(c.calibrateDpk());
        b.bb[0x1c10]=b.bb[0x3c10]=0;const auto before=b.bb[r::R_DPD_BND];assert(c.trackDpk());
        assert(c.averageThermal(0)==0&&b.bb[r::R_DPD_BND]==before);}
    for(unsigned i=1;i<=trackCount;++i){Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);assert(c.calibrateDpk());
        b.bb[0x1c10]=26u<<24;b.bb[0x3c10]=35u<<24;b.bb[r::R_TXAGC_BB]=b.bb[r::R_TXAGC_BB+0x2000]=1;
        b.failAt=b.operations+i;assert(!c.trackDpk()&&c.result.error==r::Error::io&&!b.active&&!c.result.dpkReady);}
    for(unsigned i:{1u,trackCount/2,trackCount}){Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);assert(c.calibrateDpk());
        b.bb[0x1c10]=26u<<24;b.bb[0x3c10]=35u<<24;b.bb[r::R_TXAGC_BB]=b.bb[r::R_TXAGC_BB+0x2000]=1;
        b.cancelAt=b.operations+i;assert(!c.trackDpk()&&c.result.error==r::Error::cancelled&&!b.active&&!c.result.dpkReady);}
    assert(g.calibrateIq({1,0,149})&&!g.result.dpkReady&&!g.result.tssiReady); // new channel invalidates power state
    printf("PASS: DPK dual-path model, %u I/O and %u delay failures, two NCTL completion stages, DC/correlation/AGC checks, measured thermal EWMA and %u tracking faults; not hardware DPD\n",count,delays,trackCount);
}
void prepareScan(Backend &b,r::Initialization<Backend> &c){prepareDpk(b,c);assert(c.beginScan());}
void testScan(){
    const r::Channel home{1,2,42},other{0,0,6};
    Backend golden;r::Initialization<Backend> g(golden,1);prepareDpk(golden,g);assert(g.calibrateDpk());
    const auto before=g.tssi();const auto dpkBefore=g.dpk();const auto iqBefore=g.iqk();
    const unsigned preserved[]={r::R_IQK_RES,r::R_TXIQC,r::R_RXIQC,r::R_DPD_CH0A,r::R_DPD_BND};
    unsigned coefficients[2][5]{};
    for(unsigned p=0;p<2;++p)for(unsigned i=0;i<5;++i)coefficients[p][i]=golden.bb[preserved[i]+(p<<8)];
    const auto pmac=golden.pmacStarts,shots=golden.oneshots;
    assert(g.beginScan()&&g.result.scanActive&&g.result.scanReady);
    assert(!g.result.iqReady&&!g.result.tssiReady&&!g.result.dpkReady);
    auto n=golden.operations;
    assert(!g.beginScan()&&!g.calibrateIq(home)&&!g.calibrateTssi()&&!g.calibrateDpk()&&!g.trackDpk());
    assert(!g.prepareScanChannel({2,0,1})&&!g.finishScan(other)&&golden.operations==n);
    assert(g.prepareScanChannel(other));const auto hopCount=golden.operations-n;
    assert(g.result.scanReady&&r::sameChannel(g.result.scanChannel,other));
    // Default scan alignment for an unmeasured band is never recorded as a
    // measurement. Visiting channels must not run PMAC or overwrite IQK/DPK.
    for(auto ch:{r::Channel{1,0,100},r::Channel{1,0,149},r::Channel{1,0,36}}){
        assert(g.prepareScanChannel(ch));
        for(unsigned p=0;p<2;++p){
            assert(!g.tssi().alignment_done[p][r::TSSI_ALIMK_2G]);
            assert(!g.tssi().alignment_done[p][r::TSSI_ALIMK_5GM]);
            assert(!g.tssi().alignment_done[p][r::TSSI_ALIMK_5GH]);
            assert(g.dpk().bp[p][0].ch==dpkBefore.bp[p][0].ch&&g.dpk().bp[p][0].pwsf==dpkBefore.bp[p][0].pwsf);
            assert(g.iqk().iqk_ch[p]==iqBefore.iqk_ch[p]);
            for(unsigned i=0;i<5;++i)assert(golden.bb[preserved[i]+(p<<8)]==coefficients[p][i]);
        }
    }
    const unsigned align[]={r::R_P0_TSSI_ALIM1,r::R_P0_TSSI_ALIM3,r::R_P0_TSSI_ALIM2,r::R_P0_TSSI_ALIM4};
    for(unsigned p=0;p<2;++p)for(unsigned i=0;i<4;++i)
        assert(golden.bb[align[i]+(p<<13)]==before.alignment_value[p][r::TSSI_ALIMK_5GL][i]);
    assert(g.prepareScanChannel(other));n=golden.operations;
    assert(g.finishScan(home));const auto finishCount=golden.operations-n;
    assert(!g.result.scanActive&&!g.result.scanReady&&g.result.iqReady&&g.result.tssiReady&&g.result.dpkReady);
    assert(golden.pmacStarts==pmac&&golden.oneshots==shots);
    for(unsigned p=0;p<2;++p){
        const unsigned reg=golden.bb[r::R_P0_TSSI_TRK+(p<<13)];
        assert(r::fieldGet(r::B_P0_TSSI_OFT,reg)==0xc0&&(reg&r::B_P0_TSSI_OFT_EN));
        for(unsigned i=0;i<4;++i)assert(golden.bb[align[i]+(p<<13)]==before.alignment_value[p][r::TSSI_ALIMK_5GL][i]);
        for(unsigned i=0;i<5;++i)assert(golden.bb[preserved[i]+(p<<8)]==coefficients[p][i]);
    }
    n=golden.operations;assert(!g.finishScan(home)&&!g.prepareScanChannel(other)&&golden.operations==n);
    // Scan can start before DPK but cannot promote it to calibrated afterward.
    {Backend b;r::Initialization<Backend> c(b,1);prepareTssi(b,c);
        assert(!c.result.tssiReady&&c.beginScan()&&b.pmacStarts==4);
        assert(c.prepareScanChannel(other)&&c.finishScan(home)&&!c.result.dpkReady&&c.result.tssiReady);}
    {Backend b;r::Initialization<Backend> c(b,1);assert(!c.beginScan()&&!b.operations);
        prepareTssi(b,c);b.noCwPath=1;assert(!c.beginScan()&&!c.result.scanActive&&!c.result.scanReady);}
    for(unsigned phase=0;phase<2;++phase){const auto count=phase?finishCount:hopCount;
        for(unsigned i=1;i<=count;++i){Backend b;r::Initialization<Backend> c(b,1);prepareScan(b,c);
            if(phase)assert(c.prepareScanChannel(other));b.failAt=b.operations+i;
            assert(!(phase?c.finishScan(home):c.prepareScanChannel(other))&&c.result.error==r::Error::io);
            assert(b.operations==b.failAt&&!b.active&&!c.result.scanReady&&c.result.scanActive&&c.result.requiresReset);
            assert(!c.result.iqReady&&!c.result.tssiReady&&!c.result.dpkReady);
            const auto stopped=b.operations;assert(!c.finishScan(home)&&b.operations==stopped);
        }
        for(unsigned i:{1u,count/2,count}){Backend b;r::Initialization<Backend> c(b,1);prepareScan(b,c);
            if(phase)assert(c.prepareScanChannel(other));b.cancelAt=b.operations+i;
            assert(!(phase?c.finishScan(home):c.prepareScanChannel(other))&&c.result.error==r::Error::cancelled&&!c.result.scanReady&&!b.active);}
    }
    for(unsigned phase=0;phase<3;++phase)for(unsigned fault=0;fault<3;++fault){
        Backend b;r::Initialization<Backend> c(b,1);prepareDpk(b,c);
        if(phase)assert(c.beginScan());if(phase==2)assert(c.prepareScanChannel(other));
        if(fault==0)b.failBegin=b.begins+1;if(fault==1)b.failEnd=b.ends+1;if(fault==2)b.failDrain=b.drains+1;
        assert(!(phase==0?c.beginScan():phase==1?c.prepareScanChannel(other):c.finishScan(home)));
        assert(!c.result.scanReady&&!c.result.iqReady&&!c.result.tssiReady&&!c.result.dpkReady&&c.result.requiresReset);
        assert(b.active==(fault==1));
    }
    {Backend b;r::Initialization<Backend> c(b,1);prepareScan(b,c);b.backwards=true;
        assert(!c.prepareScanChannel(other)&&c.result.error==r::Error::clock&&!c.result.scanReady&&!b.active);}
    for(unsigned i=0;i<128;++i){golden.clock+=3000000;assert(g.beginScan());
        assert(g.prepareScanChannel(other)&&g.finishScan(home)&&g.result.dpkReady);}
    printf("PASS: scan RFK four thermal bands, measured/default alignment distinction, home IQK/DPK preservation, %u hop and %u restore I/O failures, cancellation/leases and repeated scans; not AP scanning\n",hopCount,finishCount);
}
int main(){
    testAsynchronousSteps();
    // PAS reports arrive in 16-bit containers but contain signed 12-bit data.
    assert(r::sign_extend32(0xf001,11)==1&&r::sign_extend32(0xf7ff,11)==2047);
    assert(r::sign_extend32(0x8800,11)==-2048&&r::sign_extend32(0xffff,11)==-1);
    assert(r::sign_extend32(0x80000000,31)==(-2147483647-1));
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
    testIq();
    testTssi();
    testDpk();
    testScan();
}
