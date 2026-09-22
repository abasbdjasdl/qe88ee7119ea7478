// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "Rtw8852bChannelConstants.hpp"
#include "RfkInitialization.hpp"
#include "BasebandGain.hpp"
namespace rtl8852be { namespace channel {
struct Channel {u8 band{},width{},center{},primary{};};
inline bool validChannel(Channel c){
    if(!rfk::validChannel({c.band,c.width,c.center})||!rfk::validChannel({c.band,0,c.primary}))return false;
    const int distance=int(c.primary)-c.center;
    if(c.width==0)return distance==0;
    if(c.band==0&&c.primary==14)return false;
    return c.width==1?(distance==2||distance==-2):(distance==2||distance==-2||distance==6||distance==-6);
}
enum class Error {none,precondition,io,timeout,clock,cancelled,pllUnlocked,readback};
enum class Stage {idle,programming,prepared,complete,failed};
struct Result {
    Error error{Error::none};Stage stage{Stage::idle};Channel channel{};
    u32 address{},mask{},value{};u8 path{};unsigned operations{},polls{};
    bool registersProgrammed{},receiversRestored{},ownershipReleased{},requiresReset{};
};
// Workloop-serialized, off-stack state. program() leaves the channel lease held:
// controller programs by-rate/offset/shape/limit/RU power before finish(). Neither
// phase authorizes TX. Kind::channel end must keep scheduler TX paused until
// firmware, power, RFK and regulatory prerequisites have all been established.
template<class Backend> class ChannelProgramming {
    struct rtw89_chan {rtw89_band band_type{};rtw89_bandwidth band_width{};u8 channel{},primary_channel{},pri_ch_idx{};rtw89_subband subband_type{};};
    struct rtw89_hal {u8 antenna_rx{};};
    struct Context {
        Backend &io;Result &result;network::BasebandGain gain{};rtw89_phy_efuse_gain efuse_gain{};
        rtw89_hal hal{};rtw89_chan chan{};bool dbcc_en=false,monitor=false;
        bool started{};uint64_t first{},previous{};unsigned base{};
        Context(Backend &b,Result &r):io(b),result(r){}
    };
    static constexpr unsigned RTW89_DBG_RFK=0;
    static bool fail(Context *d,Error e){if(d->result.error==Error::none){d->result.error=e;
        d->result.stage=Stage::failed;d->result.requiresReset=d->result.operations!=0;
        d->result.registersProgrammed=false;d->result.receiversRestored=false;}return false;}
    static bool check(Context *d){
        if(d->result.error!=Error::none)return false;
        if(d->io.cancelled())return fail(d,Error::cancelled);
        const auto now=d->io.nowUs();if(!d->started){d->first=d->previous=now;d->started=true;}
        if(now<d->previous)return fail(d,Error::clock);d->previous=now;
        if(now-d->first>2000000||d->result.operations-d->base>=20000)return fail(d,Error::timeout);
        return true;
    }
    static bool op(Context *d,u32 a,u32 m,u32 v=0,u8 p=0){
        if(!check(d))return false;++d->result.operations;
        d->result.address=a;d->result.mask=m;d->result.value=v;d->result.path=p;return true;
    }
    static u32 rtw89_read_rf(Context *d,u8 p,u32 a,u32 m){u32 v=0;
        if(!op(d,a,m,0,p))return 0;
        if(p>1||!m||!d->io.readRf(p,a,m,v))fail(d,Error::io);d->result.value=v;return v;}
    static void rtw89_write_rf(Context *d,u8 p,u32 a,u32 m,u32 v){
        if(op(d,a,m,v,p)&&(p>1||!m||!d->io.writeRf(p,a,m,v)))fail(d,Error::io);}
    static u32 rtw89_phy_read32_mask(Context *d,u32 a,u32 m){u32 v=0;
        if(!op(d,a,m))return 0;
        if(!m||!d->io.readBb(a,v)||v==0xffffffff||v==0xdeadbeef){fail(d,Error::io);return 0;}
        d->result.value=fieldGet(m,v);return d->result.value;}
    static void rtw89_phy_write32_mask(Context *d,u32 a,u32 m,u32 v){
        if(!m){fail(d,Error::precondition);return;}const auto old=m==0xffffffff?0:rtw89_phy_read32_mask(d,a,0xffffffff);
        if(op(d,a,m,v)&&!d->io.writeBb(a,(old&~m)|fieldPrep(m,v)))fail(d,Error::io);}
    static void rtw89_phy_write32(Context *d,u32 a,u32 v){rtw89_phy_write32_mask(d,a,0xffffffff,v);}
    static void rtw89_phy_write32_clr(Context *d,u32 a,u32 m){rtw89_phy_write32_mask(d,a,m,0);}
    static void rtw89_phy_write32_idx(Context *d,u32 a,u32 m,u32 v,rtw89_phy_idx p){
        if(p!=RTW89_PHY_0){fail(d,Error::precondition);return;}rtw89_phy_write32_mask(d,a,m,v);}
    static u32 rtw89_phy_read32_idx(Context *d,u32 a,u32 m,rtw89_phy_idx p){
        if(p!=RTW89_PHY_0){fail(d,Error::precondition);return 0;}return rtw89_phy_read32_mask(d,a,m);}
    static u32 readMac(Context *d,u32 a){u32 v=0;if(!op(d,a,0xffffffff))return 0;
        if(!d->io.readChannelMac32(a,v)||v==0xffffffff||v==0xdeadbeef)fail(d,Error::io);d->result.value=v;return v;}
    static void rtw89_write32(Context *d,u32 a,u32 v){if(op(d,a,0xffffffff,v)&&!d->io.writeChannelMac32(a,v))fail(d,Error::io);}
    static void rtw89_write32_mask(Context *d,u32 a,u32 m,u32 v){
        if(!m){fail(d,Error::precondition);return;}const auto old=readMac(d,a);rtw89_write32(d,a,(old&~m)|fieldPrep(m,v));}
    static void rtw89_write32_clr(Context *d,u32 a,u32 m){rtw89_write32_mask(d,a,m,0);}
    static u8 readByte(Context *d,u32 a){u8 v=0;if(!op(d,a,255))return 0;
        if(!d->io.readChannelMac8(a,v))fail(d,Error::io);d->result.value=v;return v;}
    static void rtw89_write8_mask(Context *d,u32 a,u32 m,u32 v){
        if(!m||(m&~255u)){fail(d,Error::precondition);return;}const auto old=readByte(d,a);
        if(op(d,a,m,v)&&!d->io.writeChannelMac8(a,u8((old&~m)|fieldPrep(m,v))))fail(d,Error::io);}
    static void rtw89_write8_clr(Context *d,u32 a,u32 m){rtw89_write8_mask(d,a,m,0);}
    static void rtw89_write8_set(Context *d,u32 a,u32 m){rtw89_write8_mask(d,a,m,m>>shift(m));}
    static u32 rtw89_mac_reg_by_idx(Context *d,u32 a,u8 i){if(i)fail(d,Error::precondition);return a;}
    static int rtw89_mac_check_mac_en(Context *d,u8 i,rtw89_mac_hwmod_sel sel){
        if(i||sel!=RTW89_CMAC_SEL){fail(d,Error::precondition);return -1;}
        if(!(readMac(d,R_AX_CMAC_FUNC_EN)&B_AX_CMAC_EN)){fail(d,Error::precondition);return -1;}return check(d)?0:-1;}
    static int rtw89_mac_cfg_ppdu_status(Context *d,u8 i,bool en){return rtw89_mac_cfg_ppdu_status_ax(d,i,en);}
    static void rtw89_debug(Context *,unsigned,const char *,...){}
    static void rtw89_warn(Context *d,const char *,...){fail(d,Error::precondition);}
    static void delay(Context *d,unsigned us){if(check(d)&&(us>50000||!d->io.delayUs(us)))fail(d,Error::io);}
    template<class Predicate> static int poll(Context *d,unsigned step,unsigned timeout,Predicate ready){
        const auto start=d->io.nowUs();for(unsigned i=0;i<=timeout/(step?step:1);++i){
            if(!check(d))return -1;if(d->previous<start){fail(d,Error::clock);return -1;}
            const bool done=ready();++d->result.polls;if(!check(d))return -1;if(done)return 0;
            if(d->previous-start>=timeout)break;delay(d,step);}
        fail(d,Error::timeout);return -1;
    }
#define R16_CHANNEL_POLL(op,value,condition,step,timeout,sleep,device,...) \
    poll(device,step,timeout,[&](){value=op(device,__VA_ARGS__);return bool(condition);})
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "Rtw8852bChannelFunctions.inc"
#pragma clang diagnostic pop
#undef R16_CHANNEL_POLL
public:
    Result result{};
private:
    Context context_;bool configured_{},owned_{};
    bool release(bool success){
        if(!owned_)return false;result.ownershipReleased=context_.io.end(rfk::Kind::channel,success);
        if(result.ownershipReleased)owned_=false;else {fail(&context_,Error::io);result.requiresReset=true;}
        return success&&result.ownershipReleased;
    }
    bool verifyQuiescent(){
        auto *d=&context_;
        if((readMac(d,R_AX_PPDU_STAT)&B_AX_PPDU_STAT_RPT_EN)||
           rtw89_phy_read32_mask(d,R_ADC_FIFO,B_ADC_FIFO_RST)!=0xf||
           rtw89_phy_read32_mask(d,R_RSTB_ASYNC,B_RSTB_ASYNC_ALL)!=0||
           rtw89_phy_read32_mask(d,R_PD_CTRL,B_PD_HIT_DIS)!=1||
           rtw89_phy_read32_mask(d,R_RXCCA,B_RXCCA_DIS)!=1||
           rtw89_phy_read32_mask(d,R_S0_HW_SI_DIS,B_S0_HW_SI_DIS_W_R_TRIG)!=7||
           rtw89_phy_read32_mask(d,R_S1_HW_SI_DIS,B_S1_HW_SI_DIS_W_R_TRIG)!=7)
            fail(d,Error::readback);
        for(u8 p=0;p<2&&check(d);++p)
            if(rtw89_phy_read32_mask(d,R_P0_TSSI_TRK+(p<<13),B_P0_TSSI_TRK_EN)!=1||
               rtw89_phy_read32_mask(d,R_P0_TXPW_RSTB+(p<<13),B_P0_TXPW_RSTB_MANON)!=1)fail(d,Error::readback);
        return check(d);
    }
    bool verify(){
        auto *d=&context_;const auto &c=d->chan;const u32 bw=c.band_width==RTW89_CHANNEL_WIDTH_20?CFGCH_BW_20M:
            c.band_width==RTW89_CHANNEL_WIDTH_40?CFGCH_BW_40M:CFGCH_BW_80M;
        const u32 bits=RR_CFGCH_CH|RR_CFGCH_BAND0|RR_CFGCH_BAND1|RR_CFGCH_BW;
        const u32 expected=fieldPrep(RR_CFGCH_CH,c.channel)|fieldPrep(RR_CFGCH_BW,bw)|
            (c.band_type==RTW89_BAND_5G?fieldPrep(RR_CFGCH_BAND0,CFGCH_BAND0_5G)|fieldPrep(RR_CFGCH_BAND1,CFGCH_BAND1_5G):0);
        const u32 banks[]={RR_CFGCH,RR_CFGCH_V1};
        for(u8 p=0;p<2&&check(d);++p)for(u32 a:banks)
            if((rtw89_read_rf(d,p,a,RFREG_MASK)&bits)!=expected)fail(d,Error::readback);
        const u32 tx20=rtw89_phy_get_txsc(d,&c,RTW89_CHANNEL_WIDTH_20);
        const u32 tx40=c.band_width==RTW89_CHANNEL_WIDTH_80?rtw89_phy_get_txsc(d,&c,RTW89_CHANNEL_WIDTH_40):0;
        const u32 rateMask=B_AX_BAND_MODE|B_AX_CHECK_CCK_EN|B_AX_RTS_LIMIT_IN_OFDM6;
        const u32 rateExpected=c.band_type==RTW89_BAND_2G?B_AX_BAND_MODE:B_AX_CHECK_CCK_EN|B_AX_RTS_LIMIT_IN_OFDM6;
        if((readByte(d,R_AX_WMAC_RFMOD)&B_AX_WMAC_RFMOD_MASK)!=u32(c.band_width)||
           (readByte(d,R_AX_TXRATE_CHK)&rateMask)!=rateExpected||
           readMac(d,R_AX_TX_SUB_CARRIER_VALUE)!=(tx20|(tx40<<4))||
           rtw89_phy_read32_mask(d,R_FC0_BW_V1,B_FC0_BW_SET)!=u32(c.band_width)||
           rtw89_phy_read32_mask(d,R_CHBW_MOD_V1,B_CHBW_MOD_PRICH)!=c.pri_ch_idx||
           rtw89_phy_read32_mask(d,R_MAC_PIN_SEL,B_CH_IDX_SEG0)!=rtw89_encode_chan_idx(d,c.primary_channel,c.band_type))fail(d,Error::readback);
        return check(d);
    }
public:
    explicit ChannelProgramming(Backend &b):context_(b,result){}
    ChannelProgramming(const ChannelProgramming &)=delete;ChannelProgramming &operator=(const ChannelProgramming &)=delete;
    bool configure(const network::BasebandGain &gain,const network::BoardCalibration &board,
                   const network::PhyCalibration &phy,s8 offsetBase,s8 rssiBase,u8 antennaRx,bool monitor=false){
        if(configured_||result.stage!=Stage::idle||!board.identityValid||antennaRx<1||antennaRx>3)return false;
        context_.gain=gain;context_.hal.antenna_rx=antennaRx;context_.monitor=monitor;
        auto &g=context_.efuse_gain;g.offset_valid=board.gainOffsetValid;g.comp_valid=phy.gainCompValid;
        g.offset_base[0]=offsetBase;g.rssi_base[0]=rssiBase;
        for(unsigned p=0;p<2;++p)for(unsigned i=0;i<5;++i){g.offset[p][i]=board.gainOffset[p][i];g.comp[p][i]=phy.gainComp[p][i];}
        configured_=true;return true;
    }
    bool program(Channel c){
        if(!configured_||owned_||result.error!=Error::none||!validChannel(c))return false;
        result.channel=c;result.stage=Stage::programming;result.registersProgrammed=false;result.receiversRestored=false;
        context_.started=false;context_.base=result.operations;if(!check(&context_))return false;
        result.ownershipReleased=false;if(!context_.io.begin(rfk::Kind::channel)){
            // Native preflight can fail after acquiring the owner lease and its
            // cleanup can also fail. Adopt that outstanding lease for abort().
            owned_=context_.io.leaseActive();result.ownershipReleased=!owned_;
            fail(&context_,Error::precondition);result.requiresReset=true;return false;}
        owned_=true;const int distance=int(c.primary)-c.center;
        const u8 primaryIndex=c.width?u8(distance>0?distance/2:(-distance)/2+1):0;
        context_.chan={static_cast<rtw89_band>(c.band),static_cast<rtw89_bandwidth>(c.width),c.center,c.primary,primaryIndex,
            c.band==0?RTW89_CH_2G:c.center<=64?RTW89_CH_5G_BAND_1:c.center<=144?RTW89_CH_5G_BAND_3:RTW89_CH_5G_BAND_4};
        rtw89_channel_help_params params{};
        rtw8852b_set_channel_help(&context_,true,&params,&context_.chan,RTW89_MAC_0,RTW89_PHY_0);
        if(verifyQuiescent())rtw8852b_set_channel(&context_,&context_.chan,RTW89_MAC_0,RTW89_PHY_0);
        if(!verify()){release(false);return false;}
        result.registersProgrammed=true;result.stage=Stage::prepared;return true;
    }
    // Call only after successful power programming; this restores receivers,
    // not scheduler TX. The controller must still execute required RFK.
    bool finish(){
        if(!owned_||result.stage!=Stage::prepared)return false;auto *d=&context_;
        if(verify()){rtw89_channel_help_params params{};rtw8852b_set_channel_help(d,false,&params,&d->chan,RTW89_MAC_0,RTW89_PHY_0);}
        if(check(d)&&!d->io.drain())fail(d,Error::io);
        const u32 ppdu=B_AX_PPDU_STAT_RPT_EN|B_AX_APP_MAC_INFO_RPT|B_AX_APP_RX_CNT_RPT|B_AX_APP_PLCP_HDR_RPT|B_AX_PPDU_STAT_RPT_CRC32;
        if(check(d)&&((readMac(d,R_AX_PPDU_STAT)&ppdu)!=ppdu||
            fieldGet(B_AX_FWD_PPDU_STAT_MASK,readMac(d,R_AX_HW_RPT_FWD))!=RTW89_PRPT_DEST_HOST||
            rtw89_phy_read32_mask(d,R_ADC_FIFO,B_ADC_FIFO_RST)!=0||
            rtw89_phy_read32_mask(d,R_RSTB_ASYNC,B_RSTB_ASYNC_ALL)!=1||
            rtw89_phy_read32_mask(d,R_PD_CTRL,B_PD_HIT_DIS)!=0||
            rtw89_phy_read32_mask(d,R_RXCCA,B_RXCCA_DIS)!=u32(d->chan.band_type==RTW89_BAND_5G)||
            rtw89_phy_read32_mask(d,R_S0_HW_SI_DIS,B_S0_HW_SI_DIS_W_R_TRIG)!=0||
            rtw89_phy_read32_mask(d,R_S1_HW_SI_DIS,B_S1_HW_SI_DIS_W_R_TRIG)!=0||
            rtw89_read_rf(d,RF_PATH_A,RR_SYNFB,RR_SYNFB_LK)!=1))fail(d,Error::readback);
        for(u8 p=0;p<2&&check(d);++p)
            if(rtw89_phy_read32_mask(d,R_P0_TSSI_TRK+(p<<13),B_P0_TSSI_TRK_EN)!=0||
               rtw89_phy_read32_mask(d,R_P0_TXPW_RSTB+(p<<13),B_P0_TXPW_RSTB_MANON)!=0)fail(d,Error::readback);
        if(!release(check(d)))return false;
        result.receiversRestored=true;result.stage=Stage::complete;return true;
    }
    bool abort(){if(!owned_)return false;fail(&context_,Error::cancelled);release(false);return !owned_;}
};
} }
