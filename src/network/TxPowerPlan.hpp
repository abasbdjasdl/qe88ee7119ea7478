// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "Rtw8852bPowerTables.hpp"
#include "ChannelGeometry.hpp"
namespace rtl8852be { namespace power {
// The controller supplies an immutable policy snapshot in the workloop gate.
// query() returns a conducted-power ceiling in 0.5 dBm units after applying
// platform/SAR/user/regulatory constraints. EIRP must first subtract antenna
// gain. A missing/unknown policy is an error, not permission to use maximum power.
// This is power programming, not channel authorization (DFS/no-IR still applies).
struct Policy {
    u8 domain{};uint64_t generation{};void *context{};
    bool (*query)(void *,u8 band,u8 channel,int16_t &halfDbm){};
};
enum class PlanError {none,channel,policy,index,capacity};
struct Write {bool baseband{};u32 address{},mask{},value{};};
inline s8 rfToMac(s8 v){return s8(v>=0?int(v)/2:-((-int(v)+1)/2));}
inline int channelIndex(u8 band,u8 ch){
    if(band==0)return ch>=1&&ch<=14?ch-1:-1;
    if(band!=1)return -1;
    if(ch>=36&&ch<=64&&!(ch&1))return (ch-36)/2;
    if(ch>=100&&ch<=144&&!(ch&1))return (ch-100)/2+15;
    if(ch>=149&&ch<=177&&(ch&1))return (ch-149)/2+38;
    return -1;
}
class TxPowerPlan {
    struct Context {TxPowerPlan *plan;Policy policy;bool cached[53]{};s8 ceilings[53]{};};
    Write writes_[64]{};unsigned count_{};PlanError error_{PlanError::none};
    static s8 minimum(s8 a,s8 b){return a<b?a:b;}
    static s8 ceiling(Context *d,u8 band,u8 ch){
        const int index=channelIndex(band,ch);
        if(index<0){d->plan->error_=PlanError::index;return -64;}
        if(d->plan->error_!=PlanError::none)return -64;
        if(!d->cached[index]){int16_t v=0;
            if(!d->policy.query(d->policy.context,band,ch,v)||v<-64||v>63){d->plan->error_=PlanError::policy;return -64;}
            d->ceilings[index]=s8(v);d->cached[index]=true;}
        return d->ceilings[index];
    }
    static s8 rtw89_phy_read_txpwr_limit(Context *d,u8 band,u8 bw,u8 ntx,u8 rs,u8 bf,u8 ch){
        const int index=channelIndex(band,ch);
        if(index<0||ntx>1||rs>2||bf>1||bw>(band==0?1:3)){d->plan->error_=PlanError::index;return -64;}
        const unsigned channels=band==0?14:53;
        const auto *table=band==0?txpwr_lmt_2g:txpwr_lmt_5g;
        const unsigned base=(((bw*2+ntx)*3+rs)*2+bf)*16*channels+unsigned(index);
        auto v=table[base+d->policy.domain*channels];
        if(!v)v=table[base]; // The reference explicitly falls back to world-domain entries.
        return minimum(rfToMac(v),ceiling(d,band,ch));
    }
    static s8 rtw89_phy_read_txpwr_limit_ru(Context *d,u8 band,u8 ru,u8 ntx,u8 ch){
        const int index=channelIndex(band,ch);
        if(index<0||ru>2||ntx>1){d->plan->error_=PlanError::index;return -64;}
        const unsigned channels=band==0?14:53;
        const auto *table=band==0?txpwr_lmt_ru_2g:txpwr_lmt_ru_5g;
        const unsigned base=(ru*2+ntx)*16*channels+unsigned(index);
        auto v=table[base+d->policy.domain*channels];if(!v)v=table[base];
        return minimum(rfToMac(v),ceiling(d,band,ch));
    }
    static void fillPair(Context *d,s8 *v,u8 band,u8 bw,u8 ntx,u8 rs,u8 ch){
        for(u8 bf=0;bf<2;++bf)v[bf]=rtw89_phy_read_txpwr_limit(d,band,bw,ntx,rs,bf,ch);
    }
#include "Rtw8852bPowerFunctions.inc"
    void add(bool bb,u32 a,u32 mask,u32 v){
        if(error_!=PlanError::none)return;
        if(count_==64){error_=PlanError::capacity;return;}
        writes_[count_++]={bb,a,mask,v};
    }
    static u32 pack(const s8 *p){return u32(u8(p[0]))|(u32(u8(p[1]))<<8)|(u32(u8(p[2]))<<16)|(u32(u8(p[3]))<<24);}
    static s8 rate(u8 band,u8 nss,u8 rs,u8 index){
        if(rs==RTW89_RS_CCK)band=0;return rfToMac(byrate[((band*2+nss)*5+rs)*12+index]);
    }
public:
    PlanError error()const{return error_;}unsigned size()const{return count_;}
    const Write &operator[](unsigned i)const{return writes_[i];}
    // Entire plan, including all policy lookups, is constructed before any MMIO.
    bool build(channel::Channel ch,Policy policy){
        count_=0;error_=PlanError::none;
        if(!channel::validChannel(ch)){error_=PlanError::channel;return false;}
        if(!policy.query||policy.domain>=RTW89_REGD_NUM||!policy.generation){error_=PlanError::policy;return false;}
        Context d{this,policy};
        // rtw8852b_set_txpwr_ref uses ref_ofdm=ref_cck=0, baseCW=0x27,
        // TSSI16dBmCW=0x12c. Preserve unrelated bits in the actual registers.
        const u32 refMask=B_DPD_TSSI_CW|B_DPD_PWR_CW|B_DPD_REF;
        const u32 refValue=rfk::fieldPrep(B_DPD_TSSI_CW,0x12c-128)|rfk::fieldPrep(B_DPD_PWR_CW,0x27*8);
        add(false,R_AX_PWR_RATE_CTRL,B_AX_PWR_REF,0);
        const u32 offsets[]={4,8},bases[]={0x5800,0x7800};
        for(u32 offset:offsets)for(u32 base:bases)add(true,base+offset,refMask,refValue);
        u32 a=R_AX_PWR_BY_RATE;const u8 rateCounts[]={4,8,12,4};
        for(u8 nss=0;nss<2;++nss)for(u8 rs=0;rs<4;++rs){
            if(nss&&rs<2)continue;
            for(u8 i=0;i<rateCounts[rs];i+=4){s8 v[4];for(u8 j=0;j<4;++j)v[j]=rate(ch.band,nss,rs,i+j);
                add(false,a,0xffffffff,pack(v));a+=4;}}
        u32 offset=0;for(u8 i=0;i<5;++i)offset|=u32(u8(rate(ch.band,0,4,i))&15)<<(i*4);
        add(false,R_AX_PWR_RATE_OFST_CTRL,0xfffff,offset);
        if(ch.band==0){const unsigned shape=ch.center==14?2:tx_shape_lmt[policy.domain]?1:0;
            for(unsigned i=0;i<8;++i)add(true,R_TXFIR0+i*4,0xffffffff,dfir[shape][i]);}
        add(true,R_DCFO_OPT,B_TXSHAPE_TRIANGULAR_CFG,rfk::fieldPrep(B_TXSHAPE_TRIANGULAR_CFG,u32(tx_shape_lmt[(ch.band*2+1)*16+policy.domain])));
        static_assert(sizeof(rtw89_txpwr_limit_ax)==40,"AX limit page layout");
        static_assert(sizeof(rtw89_txpwr_limit_ru_ax)==24,"AX RU page layout");
        for(u8 ntx=0;ntx<2;++ntx){rtw89_txpwr_limit_ax page{};
            if(ch.width==0)rtw89_phy_fill_txpwr_limit_20m_ax(&d,&page,ch.band,ntx,ch.center);
            else if(ch.width==1)rtw89_phy_fill_txpwr_limit_40m_ax(&d,&page,ch.band,ntx,ch.center,ch.primary);
            else rtw89_phy_fill_txpwr_limit_80m_ax(&d,&page,ch.band,ntx,ch.center,ch.primary);
            const auto *p=reinterpret_cast<const s8 *>(&page);
            for(unsigned i=0;i<40;i+=4)add(false,R_AX_PWR_LMT+ntx*40+i,0xffffffff,pack(p+i));}
        for(u8 ntx=0;ntx<2;++ntx){rtw89_txpwr_limit_ru_ax page{};
            if(ch.width==0)rtw89_phy_fill_txpwr_limit_ru_20m_ax(&d,&page,ch.band,ntx,ch.center);
            else if(ch.width==1)rtw89_phy_fill_txpwr_limit_ru_40m_ax(&d,&page,ch.band,ntx,ch.center);
            else rtw89_phy_fill_txpwr_limit_ru_80m_ax(&d,&page,ch.band,ntx,ch.center);
            const auto *p=reinterpret_cast<const s8 *>(&page);
            for(unsigned i=0;i<24;i+=4)add(false,R_AX_PWR_RU_LMT+ntx*24+i,0xffffffff,pack(p+i));}
        if(error_!=PlanError::none){count_=0;return false;}return true;
    }
};
} }
