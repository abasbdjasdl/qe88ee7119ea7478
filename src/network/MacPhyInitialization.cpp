// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation (upstream BSD option)
// Exact RTL8852B/AX initial branches of pinned rtw89 rtw8852b.c and phy.c.
#include "MacPhyInitialization.hpp"
#include "Rtw8852bPhyInitConstants.hpp"
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
namespace rtl8852be { namespace network {
using namespace phyinit_constants;
namespace {
unsigned shift(uint32_t m){unsigned n=0;while(!(m&1)){++n;m>>=1;}return n;}
int clamp(int v,int lo=-128,int hi=127){return v<lo?lo:v>hi?hi:v;}
// Explicit arithmetic right shift, including negative gain bases.
int asr(int v,unsigned n){return v>=0?v/(1<<n):-((-v+(1<<n)-1)/(1<<n));}
bool bad(uint32_t v){return v==0xffffffff||v==0xdeadbeef||v==0xeaeaeaea;}
}
MacPhyInitialization::MacPhyInitialization(IOPCIDevice *d,IOMemoryMap *m,IOWorkLoop *w,RadioAccessGuard g):
    guard_(g),radioIo_(d,m,g),radio_(radioIo_){
    if(!d||!m||!w||!g.owner||!g.check||!radioIo_.valid()||m->getLength()<0x20000)return;
    device_=d;map_=m;loop_=w;
}
bool MacPhyInitialization::fail(PhyInitError e,uint32_t a,uint32_t wanted,uint32_t got){
    if(result_.error==PhyInitError::none){result_.error=e;result_.address=a;result_.expected=wanted;result_.actual=got;}
    result_.requiresReset=result_.operations!=0;return false;
}
bool MacPhyInitialization::check(){
    if(result_.error!=PhyInitError::none)return false;
    if(!valid()||!loop_->inGate()||!guard_.check(guard_.owner))return fail(PhyInitError::ownership);
    if(cancelled_)return fail(PhyInitError::cancelled);
    const auto command=device_->configRead16(kIOPCIConfigCommand);
    if(command==0xffff||!(command&2))return fail(PhyInitError::ownership,4,2,command);
    uint64_t time=0,ns=0;clock_get_uptime(&time);absolutetime_to_nanoseconds(time,&ns);time=ns/1000;
    if(!clockStarted_){first_=previous_=time;clockStarted_=true;}
    if(time<previous_)return fail(PhyInitError::clock);previous_=time;
    return (time-first_<1000000&&phaseOps_<12000)||fail(PhyInitError::timeout);
}
bool MacPhyInitialization::enter(PhyInitStage phase){
    if(result_.error!=PhyInitError::none)return false;
    result_.stage=phase;clockStarted_=false;phaseOps_=0;return check();
}
bool MacPhyInitialization::read(uint32_t a,uint32_t &v){
    v=0;if(!check()||(a&3)||a>=0x20000)return false;++result_.operations;++phaseOps_;
    OSSynchronizeIO();v=OSReadLittleInt32(reinterpret_cast<const volatile void *>(map_->getVirtualAddress()),a);OSSynchronizeIO();
    return (check()&&!bad(v))||fail(PhyInitError::io,a,0,v);
}
bool MacPhyInitialization::write(uint32_t a,uint32_t v){
    if(!check()||(a&3)||a>=0x20000)return false;++result_.operations;++phaseOps_;
    OSWriteLittleInt32(reinterpret_cast<volatile void *>(map_->getVirtualAddress()),a,v);OSSynchronizeIO();return check();
}
bool MacPhyInitialization::update(uint32_t a,uint32_t m,uint32_t v,bool bb){
    if(!m)return fail(PhyInitError::identity,a);if(bb)a+=0x10000;
    uint32_t old=0,got=0;if(!read(a,old))return false;
    const auto desired=(old&~m)|((v<<shift(m))&m);
    if(!write(a,desired)||!read(a,got))return false;
    return !((got^desired)&m)||fail(PhyInitError::readback,a,desired&m,got&m);
}
bool MacPhyInitialization::delay(unsigned us){
    if(!check()||us>1000)return false;IODelay(us);return check();
}
bool MacPhyInitialization::siIdle(){
    if(!check())return false;const auto start=previous_;
    for(unsigned i=0;i<=1000;++i){uint32_t v=0;if(!read(0x270,v))return false;
        if(previous_-start>50000)break;if(!(v&0x80000000))return true;
        if(i==1000||previous_-start==50000)break;if(!delay(50))return false;}
    return fail(PhyInitError::timeout,0x270);
}
bool MacPhyInitialization::crystal(uint8_t a,uint8_t cap){
    // Only XO/XI crystal-cap registers, never OTP or eFuse commands.
    if((a!=4&&a!=5)||cap>127)return fail(PhyInitError::identity,0x270);
    if(!siIdle()||!write(0x270,0x80ff0000u|(uint32_t(cap)<<8)|a)||!siIdle()||
       !write(0x270,0x81000000u|a)||!siIdle())return false;
    uint32_t got=0;if(!read(0x270,got))return false;
    return uint8_t(got>>8)==cap||fail(PhyInitError::readback,0x270,cap,uint8_t(got>>8));
}
bool MacPhyInitialization::powerUnit(){
    if(result_.stage!=PhyInitStage::idle)return false;if(!enter(PhyInitStage::powerUnit))return false;
    // rtw8852b_init_txpwr_unit and set_txpwr_ul_tb_offset(0), MAC0 only.
    if(!update(R_AX_PWR_UL_CTRL2,0xffffffff,0x07763333,false)||!update(R_AX_PWR_COEXT_CTRL,0xffffffff,0x01ebf000,false)||
       !update(R_AX_PWR_UL_CTRL0,0xffffffff,0x0002f8ff,false)||!update(R_AX_PWR_UL_TB_CTRL,B_AX_PWR_UL_TB_CTRL_EN,1,false)||
       !update(R_AX_PWR_UL_TB_1T,B_AX_PWR_UL_TB_1T_MASK,0,false)||!update(R_AX_PWR_UL_TB_2T,B_AX_PWR_UL_TB_2T_MASK,uint32_t(-3),false))return false;
    result_.powerUnitReady=true;return true;
}
bool MacPhyInitialization::bbReset(){
    return update(R_S0_HW_SI_DIS,B_S0_HW_SI_DIS_W_R_TRIG,7)&&update(R_S1_HW_SI_DIS,B_S1_HW_SI_DIS_W_R_TRIG,7)&&delay(1)&&
        update(R_RSTB_ASYNC,B_RSTB_ASYNC_ALL,1)&&update(R_RSTB_ASYNC,B_RSTB_ASYNC_ALL,0)&&
        update(R_S0_HW_SI_DIS,B_S0_HW_SI_DIS_W_R_TRIG,0)&&update(R_S1_HW_SI_DIS,B_S1_HW_SI_DIS_W_R_TRIG,0)&&
        update(R_RSTB_ASYNC,B_RSTB_ASYNC_ALL,1);
}
bool MacPhyInitialization::reset(){
    if(!result_.powerUnitReady||result_.resetDone)return false;if(!enter(PhyInitStage::reset))return false;
    if(!update(R_P0_TXPW_RSTB,B_P0_TXPW_RSTB_MANON,1)||!update(R_P0_TSSI_TRK,B_P0_TSSI_TRK_EN,1)||
       !update(R_P1_TXPW_RSTB,B_P1_TXPW_RSTB_MANON,1)||!update(R_P1_TSSI_TRK,B_P1_TSSI_TRK_EN,1)||!bbReset()||
       !update(R_P0_TXPW_RSTB,B_P0_TXPW_RSTB_MANON,0)||!update(R_P0_TSSI_TRK,B_P0_TSSI_TRK_EN,0)||
       !update(R_P1_TXPW_RSTB,B_P1_TXPW_RSTB_MANON,0)||!update(R_P1_TSSI_TRK,B_P1_TSSI_TRK_EN,0))return false;
    result_.resetDone=true;return true;
}
bool MacPhyInitialization::environment(){
    if(!update(R_CCX,B_CCX_EN_MSK,1)||!update(R_CCX,B_CCX_TRIG_OPT_MSK,1)||!update(R_CCX,B_MEASUREMENT_TRIG_MSK,1)||
       !update(R_CCX,B_CCX_EDCCA_OPT_MSK,0))return false; // BW20_0
    const uint32_t addresses[]={R_IFS_T1,R_IFS_T2,R_IFS_T3,R_IFS_T4};
    const uint32_t lowMasks[]={B_IFS_T1_TH_LOW_MSK,B_IFS_T2_TH_LOW_MSK,B_IFS_T3_TH_LOW_MSK,B_IFS_T4_TH_LOW_MSK};
    const uint32_t highMasks[]={B_IFS_T1_TH_HIGH_MSK,B_IFS_T2_TH_HIGH_MSK,B_IFS_T3_TH_HIGH_MSK,B_IFS_T4_TH_HIGH_MSK};
    const uint32_t enables[]={B_IFS_T1_EN_MSK,B_IFS_T2_EN_MSK,B_IFS_T3_EN_MSK,B_IFS_T4_EN_MSK};
    // Source initial unit 32 us, high thresholds 64/256/1024/4096 us.
    const uint32_t low[]={0,3,9,33},high[]={2,8,32,128};
    for(unsigned i=0;i<4;++i)if(!update(addresses[i],lowMasks[i],low[i]))return false;
    for(unsigned i=0;i<4;++i)if(!update(addresses[i],highMasks[i],high[i]))return false;
    if(!update(R_IFS_COUNTER,B_IFS_COLLECT_EN,1))return false;
    for(unsigned i=0;i<4;++i)if(!update(addresses[i],enables[i],1))return false;return true;
}
bool MacPhyInitialization::phyStatus(){
    if(!update(R_PLCP_HISTOGRAM,B_STS_DIS_TRIG_BY_FAIL,1)||!update(R_PLCP_HISTOGRAM,B_STS_DIS_TRIG_BY_BRK,1))return false;
    // PHY status page 9 is reserved; pages >9 are physically compressed by one.
    for(unsigned page=0;page<16;++page){if(page==9)continue;
        const auto a=R_PHY_STS_BITMAP_ADDR_START+4*(page>9?page-1:page);
        if(page>=11&&!update(a,1u<<9,1))return false;
        if(!((page>=4&&page<=7)||(page>=9&&page<=11))&&!update(a,1u<<24,1))return false;
    }
    return update(R_PHY_STS_BITMAP_ADDR_START+4*13,1u<<13,1)&&update(R_PHY_STS_BITMAP_ADDR_START+4*14,1u<<13,1)&&
        update(R_PHY_STS_BITMAP_ADDR_START+4*10,1u<<1,1);
}
bool MacPhyInitialization::dig(){
    // 8852B support_igi is false: no forced IGI or gain-table reads. Initial DIG
    // is unlinked, its zero-initialized igi_rssi yields CCK clamp -18 => -128.
    if(!update(R_SEG0R_PD_V1,B_SEG0R_PD_LOWER_BOUND_MSK,0)||!update(R_SEG0R_PD_V1,B_SEG0R_PD_SPATIAL_REUSE_EN_MSK_V1,0))return false;
    if(caps_.supportCckpd&&(!update(R_BMODE_PDTH_EN_V1,B_BMODE_PDTH_LIMIT_EN_MSK_V1,0)||
       !update(R_BMODE_PDTH_V1,B_BMODE_PDTH_LOWER_BOUND_MSK_V1,uint32_t(-128))))return false;
    return update(R_PATH0_P20_FOLLOW_BY_PAGCUGC_V2,B_PATH0_P20_FOLLOW_BY_PAGCUGC_EN_MSK,0)&&
        update(R_PATH0_S20_FOLLOW_BY_PAGCUGC_V2,B_PATH0_S20_FOLLOW_BY_PAGCUGC_EN_MSK,0)&&
        update(R_PATH1_P20_FOLLOW_BY_PAGCUGC_V2,B_PATH1_P20_FOLLOW_BY_PAGCUGC_EN_MSK,0)&&
        update(R_PATH1_S20_FOLLOW_BY_PAGCUGC_V2,B_PATH1_S20_FOLLOW_BY_PAGCUGC_EN_MSK,0);
}
bool MacPhyInitialization::cfo(){
    result_.crystalCap=board_.xtal&0x7f;
    return crystal(5,result_.crystalCap)&&crystal(4,result_.crystalCap)&&update(R_DCFO_OPT,B_DCFO_OPT_EN,1)&&
        update(R_DCFO_WEIGHT,B_DCFO_WEIGHT_MSK,8)&&update(R_AX_PWR_UL_CTRL2,B_AX_PWR_UL_CFO_MASK,6,false);
}
bool MacPhyInitialization::beforeRfk(const CalibrationSnapshot &cal,const firmware::CapabilitySnapshot &caps){
    if(!result_.resetDone||result_.beforeRfkDone)return false;
    if(!cal.board.identityValid||!cal.board.xtalValid||cal.cut>1||caps.cut!=cal.cut||!caps.epoch||caps.rfe!=cal.board.rfe||
       caps.rxNss<1||caps.rxNss>2||caps.antennaRx>3||caps.supportIgi||caps.supportCckpd!=(cal.cut>0))return fail(PhyInitError::identity);
    for(unsigned i=0;i<6;++i)if(caps.mac[i]!=cal.board.mac[i])return fail(PhyInitError::identity);
    board_=cal.board;phy_=cal.phy;caps_=caps;if(!enter(PhyInitStage::beforeRfk))return false;
    // Initial thermal sampling in rtw89_phy_stat_init, before TSSI mode exists.
    for(unsigned p=0;p<2;++p){uint32_t thermal=0;
        if(!check()||!radio_.writeRf(uint8_t(p),RR_TM,RR_TM_TRI,1)||!radio_.writeRf(uint8_t(p),RR_TM,RR_TM_TRI,0)||
           !radio_.writeRf(uint8_t(p),RR_TM,RR_TM_TRI,1)||!delay(200)||!radio_.readRf(uint8_t(p),RR_TM,RR_TM_VAL,thermal)||!check())return fail(PhyInitError::io,RR_TM);
        result_.thermal[p]=uint8_t(thermal);result_.thermalPresent[p]=thermal!=0;
    }
    if(!update(R_P0_EN_SOUND_WO_NDP,B_P0_EN_SOUND_WO_NDP,0)||!update(R_P1_EN_SOUND_WO_NDP,B_P1_EN_SOUND_WO_NDP,0))return false;
    for(uint32_t a=R_AX_PWR_MACID_LMT_TABLE0;a<=R_AX_PWR_MACID_LMT_TABLE127;a+=4)if(!update(a,0xffffffff,0,false))return false;
    uint32_t base=0;if(!read(0x10000+R_P0_RPL1,base))return false;
    result_.offsetBase=signedByte(uint8_t((base&B_P0_RPL1_BIAS_MASK)>>shift(B_P0_RPL1_BIAS_MASK)));
    if(!read(0x10000+R_P1_RPL1,base))return false;
    result_.rssiBase=signedByte(uint8_t((base&B_P0_RPL1_BIAS_MASK)>>shift(B_P0_RPL1_BIAS_MASK)));
    if(!environment()||!phyStatus()||!dig()||!cfo()||!update(R_TX_COLLISION_T2R_ST,B_TX_COLLISION_T2R_ST_M,0x29)||
       !read(0x10000+R_BANDEDGE,base))return false;
    result_.defaultBandedge=(base&B_BANDEDGE_EN)!=0;
    // AX bb_wrap/ch_info and 8852B rfe_gpio are NULL; ant_diversity is false on
    // this dual-RF-path chip. No hardware operation is omitted for those hooks.
    result_.beforeRfkDone=check();return result_.beforeRfkDone;
}
bool MacPhyInitialization::gainOffset(uint8_t subband){
    if(subband!=0&&subband!=1&&subband!=3&&subband!=4)return fail(PhyInitError::identity);
    const uint32_t error[]={R_P0_AGC_RSVD,R_P1_AGC_RSVD};
    const uint32_t rssi[]={R_PATH0_G_TIA1_LNA6_OP1DB_V1,R_PATH1_G_TIA1_LNA6_OP1DB_V1};
    if(phy_.gainCompValid)for(unsigned p=0;p<2;++p)if(!update(error[p],0xff,uint32_t(clamp(int(phy_.gainComp[p][subband])*4))))return false;
    if(!board_.gainOffsetValid)return true;const unsigned band=subband==0?1:subband==1?2:subband;
    for(unsigned p=0;p<2;++p){const int offset=-board_.gainOffset[p][band];
        if(!update(rssi[p],B_PATH0_R_G_OFST_MASK,uint32_t(clamp(-(offset*4+asr(result_.offsetBase,2))))))return false;}
    const unsigned p=caps_.antennaRx==2?1:0;const int ofdm=-board_.gainOffset[p][band],cck=-board_.gainOffset[p][0];
    if(!update(R_P0_RPL1,B_P0_RPL1_BIAS_MASK,uint32_t(clamp(ofdm*16+result_.offsetBase)))||
       !update(R_P1_RPL1,B_P0_RPL1_BIAS_MASK,uint32_t(clamp(ofdm*16+result_.rssiBase))))return false;
    return subband!=0||update(R_RX_RPL_OFST,B_RX_RPL_OFST_CCK_MASK,uint32_t(clamp(cck*8+asr(result_.offsetBase,1),-64,63)));
}
bool MacPhyInitialization::receivePaths(rfk::Channel channel){
    const uint8_t path=caps_.effectiveRxPaths();const uint8_t nss=path==3?1:0;
    if(!update(R_CHBW_MOD_V1,B_ANT_RX_SEG0,path)||!update(R_FC0_BW_V1,B_ANT_RX_1RCCA_SEG0,path)||
       !update(R_FC0_BW_V1,B_ANT_RX_1RCCA_SEG1,path)||!update(R_RXHT_MCS_LIMIT,B_RXHT_MCS_LIMIT,nss)||
       !update(R_RXVHT_MCS_LIMIT,B_RXVHT_MCS_LIMIT,nss)||!update(R_RXHE,B_RXHE_USER_MAX,4)||
       !update(R_RXHE,B_RXHE_MAX_NSS,nss)||!update(R_RXHE,B_RXHETB_MAX_NSS,nss))return false;
    const uint8_t subband=channel.band==0?0:channel.center<=64?1:channel.center<=144?3:4;
    if(!gainOffset(subband))return false;
    const bool bt=channel.band==0&&(path==2||path==3);
    if(!update(R_PATH0_BT_SHARE_V1,B_PATH0_BT_SHARE_V1,bt)||!update(R_PATH0_BTG_PATH_V1,B_PATH0_BTG_PATH_V1,0)||
       !update(R_PATH1_G_LNA6_OP1DB_V1,B_PATH1_G_LNA6_OP1DB_V1,bt?0x20:0x1a)||
       !update(R_PATH1_G_TIA0_LNA6_OP1DB_V1,B_PATH1_G_TIA0_LNA6_OP1DB_V1,bt?0x30:0x2a)||
       !update(R_PATH1_BT_SHARE_V1,B_PATH1_BT_SHARE_V1,bt)||!update(R_PATH1_BTG_PATH_V1,B_PATH1_BTG_PATH_V1,bt)||
       !update(R_PMAC_GNT,B_PMAC_GNT_P1,bt?0:0xc)||!update(R_CHBW_MOD_V1,B_BT_SHARE,bt)||
       !update(R_FC0_BW_V1,B_ANT_RX_BT_SEG0,bt?2:0)||!update(R_BT_DYN_DC_EST_EN_V1,B_BT_DYN_DC_EST_EN_MSK,1)||
       !update(R_GNT_BT_WGT_EN,B_GNT_BT_WGT_EN,bt))return false;
    if(path==1){if(!update(R_P0_TXPW_RSTB,B_P0_TXPW_RSTB_MANON|B_P0_TXPW_RSTB_TSSI,1)||
        !update(R_P0_TXPW_RSTB,B_P0_TXPW_RSTB_MANON|B_P0_TXPW_RSTB_TSSI,3))return false;}
    else if(!update(R_P1_TXPW_RSTB,B_P1_TXPW_RSTB_MANON|B_P1_TXPW_RSTB_TSSI,1)||
        !update(R_P1_TXPW_RSTB,B_P1_TXPW_RSTB_MANON|B_P1_TXPW_RSTB_TSSI,3))return false;
    if(!update(R_P0_RFMODE,B_P0_RFMODE_ORI_TXRX_FTM_TX,path==2?0x1111111:0x1233312)||
       !update(R_P0_RFMODE_FTM_RX,B_P0_RFMODE_FTM_RX,path==2?0x111:0x333)||
       !update(R_P1_RFMODE,B_P1_RFMODE_ORI_TXRX_FTM_TX,path==1?0x1111111:0x1233312)||
       !update(R_P1_RFMODE_FTM_RX,B_P1_RFMODE_FTM_RX,path==1?0x111:0x333))return false;
    const auto limit=caps_.rxNss==1?0:1;
    return update(R_RXHT_MCS_LIMIT,B_RXHT_MCS_LIMIT,limit)&&update(R_RXVHT_MCS_LIMIT,B_RXVHT_MCS_LIMIT,limit)&&
        update(R_RXHE,B_RXHE_MAX_NSS,limit)&&update(R_RXHE,B_RXHETB_MAX_NSS,limit)&&update(R_MAC_SEL,B_MAC_SEL_MOD,0);
}
bool MacPhyInitialization::powerReference(){
    if(!result_.beforeRfkDone||result_.powerReferenceReady)return false;
    if(!enter(PhyInitStage::afterRfk))return false;
    // Source ref_ofdm/ref_cck=0: RF code 0x27<<3, TSSI 0x12c-128.
    const uint32_t value=((172u<<shift(B_DPD_TSSI_CW))&B_DPD_TSSI_CW)|((312u<<shift(B_DPD_PWR_CW))&B_DPD_PWR_CW);
    if(!update(R_AX_PWR_RATE_CTRL,B_AX_PWR_REF,0,false))return false;
    const uint32_t mask=B_DPD_TSSI_CW|B_DPD_PWR_CW|B_DPD_REF;
    for(unsigned p=0;p<2;++p)if(!update((p?0x7800:0x5800)+4,mask,value))return false;
    for(unsigned p=0;p<2;++p)if(!update((p?0x7800:0x5800)+8,mask,value))return false;
    result_.powerReferenceReady=check();return result_.powerReferenceReady;
}
bool MacPhyInitialization::afterRfk(rfk::Channel channel){
    if(!result_.powerReferenceReady||result_.afterRfkDone||!rfk::validChannel(channel))return false;
    if(!enter(PhyInitStage::afterRfk))return false;
    if(!receivePaths(channel)||!radio_.drain())return fail(PhyInitError::io);
    result_.afterRfkDone=check();return result_.afterRfkDone;
}
} }
