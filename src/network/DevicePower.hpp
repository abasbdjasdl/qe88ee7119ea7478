// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "Rtw8852bDevicePowerConstants.hpp"
#include "DeviceCalibration.hpp"
namespace rtl8852be { namespace powerseq {
enum class Error {none,precondition,io,readback,timeout,clock,cancelled,upstream};
struct ActionResult {
    Error error{};u32 address{},expected{},actual{};unsigned reads{},writes{},polls{},delays{};
    bool complete{},pmcClosed{};
};
struct PowerResult {ActionResult on{},off{};bool onAttempted{},offAttempted{},powered{},returnedOff{},requiresRecovery{};};
inline bool powerAddress(u32 a,unsigned width,bool write){
    if(width==1)return a==R_AX_SYS_FUNC_EN||a==R_AX_PLATFORM_ENABLE||a==R_AX_SCOREBOARD+3||
        (!write&&a==R_AX_WLAN_XTAL_SI_CTRL+1);
    if(width==2)return a==R_AX_HCI_LDO_CTRL||a==R_AX_SPS_ANA_ON_CTRL2;
    if(width!=4)return false;
    switch(a){case R_AX_SYS_ISO_CTRL:case R_AX_SYS_PW_CTRL:case R_AX_SYS_SWR_CTRL1:
    case R_AX_SYS_ADIE_PAD_PWR_CTRL:case R_AX_AFE_LDO_CTRL:case R_AX_SYS_SDIO_CTRL:
    case R_AX_WLLPS_CTRL:case R_AX_PMC_DBG_CTRL2:case R_AX_SPS_DIG_ON_CTRL0:
    case R_AX_WLAN_XTAL_SI_CTRL:case R_AX_EECS_EESK_FUNC_SEL:case R_AX_WLRF_CTRL:
    case R_AX_SPS_DIG_OFF_CTRL0:case R_AX_DMAC_FUNC_EN:case R_AX_CMAC_FUNC_EN:return true;
    case R_AX_IC_PWR_STATE:case 0x1000:case 0x101c:case 0x1a0:case 0x10b0:case 0x13b0:return !write;
    default:return false;}
}
inline bool powerSiCommand(u32 value){
    const u8 a=u8(value),v=u8(value>>8),m=u8(value>>16);const auto mode=(value>>24)&3;
    if((value&0x7c000000)||!(value&0x80000000))return false;
    const bool address=a==XTAL_SI_ANAPAR_WL||a==XTAL_SI_SRAM_CTRL||a==XTAL_SI_XTAL_XMD_2||
        a==XTAL_SI_XTAL_XMD_4||a==XTAL_SI_WL_RFC_S0||a==XTAL_SI_WL_RFC_S1;
    if(!address)return false;if(mode==XTAL_SI_NORMAL_READ)return !v&&!m;
    if(mode!=XTAL_SI_NORMAL_WRITE||(v&~m))return false;
    if(a==XTAL_SI_ANAPAR_WL)return m&&!(m&(m-1))&&(v==0||v==m);
    if(a==XTAL_SI_SRAM_CTRL)return m==2&&!v;
    if(a==XTAL_SI_XTAL_XMD_2)return m==0x70&&!v;
    if(a==XTAL_SI_XTAL_XMD_4)return m==0x0f&&!v;
    return m==1&&!v;
}
// One power-on/off pair for an exclusively owned physical device. Power-on
// requires observed MAC-off; power-off requires stopped PCI DMA and masked IRQs.
// The controller must also drain its host interrupt/workloop callbacks and leave
// firmware power-save before shutdown. No DMA allocation is freed here.
template<class D> class DevicePower {
    struct Efuse {bool valid{},power_k_valid{};u8 rfe_type=0xff;};
    struct Context {
        D &io;ActionResult result{};Efuse efuse{};struct {u8 cv{};} hal;
        bool probeDone{},cleanup{},clockStarted{},pmcOwned{};uint64_t first{},previous{};
        explicit Context(D &d):io(d){}
    } context_;
    static bool fail(Context *d,Error e,u32 a=0,u32 wanted=0,u32 actual=0){
        if(d->result.error==Error::none){d->result.error=e;d->result.address=a;d->result.expected=wanted;d->result.actual=actual;}return false;
    }
    static bool check(Context *d){
        if(d->result.error!=Error::none)return false;
        if(!d->io.inGate())return fail(d,Error::precondition);
        if(!d->cleanup&&d->io.cancelled())return fail(d,Error::cancelled);
        const auto command=d->io.command();if(command==0xffff||(command&6)!=2)return fail(d,Error::precondition,4,2,command);
        const auto now=d->io.nowUs();if(!d->clockStarted){d->clockStarted=true;d->first=d->previous=now;}
        if(now<d->previous)return fail(d,Error::clock);d->previous=now;
        if(now-d->first>=1000000||d->result.reads+d->result.writes>=40000)return fail(d,Error::timeout);
        return true;
    }
    static u32 read(Context *d,u32 a,unsigned width){
        if(!check(d))return 0;u32 value=0;++d->result.reads;bool ok=false;
        if(width==1){u8 v=0;ok=d->io.read8(a,v);value=v;}
        if(width==2){u16 v=0;ok=d->io.read16(a,v);value=v;}
        if(width==4)ok=d->io.read32(a,value);
        if(!ok||(width==4&&(value==0xffffffff||value==0xdeadbeef)))fail(d,Error::io,a,0,value);
        (void)check(d);return value;
    }
    static u8 rtw89_read8(Context *d,u32 a){return u8(read(d,a,1));}
    static u16 rtw89_read16(Context *d,u32 a){return u16(read(d,a,2));}
    static u32 rtw89_read32(Context *d,u32 a){return read(d,a,4);}
    static bool write(Context *d,u32 a,u32 v,unsigned width,u32 verifyMask){
        if(!check(d))return false;++d->result.writes;
        if(a==R_AX_PMC_DBG_CTRL2&&(v&B_AX_SYSON_DIS_PMCR_AX_WRMSK))d->pmcOwned=true;
        const bool ok=width==1?d->io.write8(a,u8(v)):width==2?d->io.write16(a,u16(v)):d->io.write32(a,v);
        if(!ok)return fail(d,Error::io,a,v);
        if(a==R_AX_SYS_PW_CTRL)verifyMask&=~(B_AX_APFN_ONMAC|B_AX_APFM_OFFMAC); // self-clearing, polled by source
        if(!check(d))return false;
        if(verifyMask){const auto got=read(d,a,width);if(!check(d))return false;
            if((got&verifyMask)!=(v&verifyMask))return fail(d,Error::readback,a,v&verifyMask,got&verifyMask);}
        return true;
    }
    static void rtw89_write32(Context *d,u32 a,u32 v){(void)write(d,a,v,4,0xffffffff);}
    static void rtw89_write16(Context *d,u32 a,u16 v){(void)write(d,a,v,2,0xffff);}
    static void update(Context *d,u32 a,u32 m,u32 v,unsigned width){
        const auto old=read(d,a,width);(void)write(d,a,(old&~m)|(v&m),width,m);
    }
    static void rtw89_write32_set(Context *d,u32 a,u32 m){update(d,a,m,m,4);}
    static void rtw89_write32_clr(Context *d,u32 a,u32 m){update(d,a,m,0,4);}
    static void rtw89_write8_set(Context *d,u32 a,u8 m){update(d,a,m,m,1);}
    static void rtw89_write8_clr(Context *d,u32 a,u8 m){update(d,a,m,0,1);}
    static void rtw89_write32_mask(Context *d,u32 a,u32 m,u32 v){update(d,a,m,v<<shift(m),4);}
    static void rtw89_write16_mask(Context *d,u32 a,u16 m,u16 v){update(d,a,m,u32(v)<<shift(m),2);}
    static bool delay(Context *d,unsigned us){if(!check(d))return false;++d->result.delays;
        return (d->io.delayUs(us)||fail(d,Error::io))&&check(d);}
    template<class Predicate> static int poll(Context *d,unsigned step,unsigned timeout,Predicate predicate){
        if(!check(d))return -1;const auto start=d->previous;
        for(unsigned i=0;i<=timeout/(step?step:1);++i){
            if(!check(d))return -1;const bool ready=predicate();++d->result.polls;
            if(!check(d))return -1;
            if(d->previous-start>timeout)break;
            if(ready)return 0;
            if(d->previous-start>=timeout||i==timeout/(step?step:1))break;
            if(!delay(d,step))return -1;
        }
        fail(d,Error::timeout);return -1;
    }
    static bool siIdle(Context *d){return poll(d,50,50000,[&](){return !(rtw89_read32(d,R_AX_WLAN_XTAL_SI_CTRL)&B_AX_WL_XTAL_SI_CMD_POLL);})==0;}
    static int rtw89_mac_write_xtal_si(Context *d,u8 offset,u8 value,u8 bits){
        // Shared serial port: drain before each command, never overwrite busy.
        const u32 command=0x80000000u|(u32(bits)<<16)|(u32(value)<<8)|offset;
        if(!powerSiCommand(command)||!siIdle(d)||!write(d,R_AX_WLAN_XTAL_SI_CTRL,command,4,0)||!siIdle(d))return -1;
        const u32 query=0x81000000u|offset;
        if(!write(d,R_AX_WLAN_XTAL_SI_CTRL,query,4,0)||!siIdle(d))return -1;
        const u8 got=rtw89_read8(d,R_AX_WLAN_XTAL_SI_CTRL+1);
        if(!check(d))return -1;
        return (got&bits)==(value&bits)?0:(fail(d,Error::readback,R_AX_WLAN_XTAL_SI_CTRL,value&bits,got&bits),-1);
    }
#define R16_POWER_POLL(op,value,condition,step,timeout,sleep,device,...) \
    poll(device,step,timeout,[&](){value=op(device,__VA_ARGS__);return bool(condition);})
#include "Rtw8852bDevicePowerFunctions.inc"
#undef R16_POWER_POLL
    bool equal(u32 address,u32 bits,u32 wanted){auto *d=&context_;const auto got=read(d,address,4);
        return check(d)&&((got&bits)==wanted||fail(d,Error::precondition,address,wanted,got&bits));}
    bool calibration(const network::CalibrationSnapshot *snapshot){
        context_.efuse={};
        if(!snapshot)return true;
        if(!snapshot->board.identityValid||snapshot->cut!=context_.hal.cv)return false;
        context_.efuse.valid=true;context_.efuse.rfe_type=snapshot->board.rfe;
        context_.efuse.power_k_valid=snapshot->phy.powerValid;return true;
    }
    bool closePmc(){
        // Narrow cleanup bypasses cancellation/error, but not native gate/PCI
        // checks. Never close a dirty write mask that this object did not open.
        auto &d=context_;u32 v=0;
        const auto command=d.io.command();
        if(!d.io.inGate()||command==0xffff||(command&6)!=2)return false;
        if(!d.io.read32(R_AX_PMC_DBG_CTRL2,v)||v==0xffffffff||v==0xdeadbeef)return false;
        if(v&B_AX_SYSON_DIS_PMCR_AX_WRMSK){
            if(!d.pmcOwned||!d.io.write32(R_AX_PMC_DBG_CTRL2,v&~B_AX_SYSON_DIS_PMCR_AX_WRMSK))return false;
            if(!d.io.read32(R_AX_PMC_DBG_CTRL2,v)||v==0xffffffff||v==0xdeadbeef||(v&B_AX_SYSON_DIS_PMCR_AX_WRMSK))return false;
        }
        d.pmcOwned=false;return true;
    }
    bool finish(int rc,bool on){
        auto *d=&context_;if(rc&&d->result.error==Error::none)fail(d,Error::upstream);
        d->result.pmcClosed=closePmc();if(!d->result.pmcClosed)fail(d,Error::readback,R_AX_PMC_DBG_CTRL2,0);
        d->result.complete=check(d);if(on)result.on=d->result;else result.off=d->result;
        if(!d->result.complete)result.requiresRecovery|=d->result.writes!=0||!d->result.pmcClosed;
        return d->result.complete;
    }
public:
    PowerResult result{};
    explicit DevicePower(D &io):context_(io){}
    DevicePower(const DevicePower&)=delete;DevicePower&operator=(const DevicePower&)=delete;
    bool start(u8 cut,const network::CalibrationSnapshot *snapshot=nullptr){
        if(result.onAttempted||cut>1)return false;context_.hal.cv=cut;
        if(!calibration(snapshot))return false;result.onAttempted=true;auto *d=&context_;
        if(!check(d)||!equal(R_AX_IC_PWR_STATE,B_AX_WLMAC_PWR_STE_MASK,0)||
           !equal(R_AX_PMC_DBG_CTRL2,B_AX_SYSON_DIS_PMCR_AX_WRMSK,0)){result.on=d->result;return false;}
        const int rc=rtw8852b_pwr_on_func(d);
        if(!rc&&check(d)&&equal(R_AX_IC_PWR_STATE,B_AX_WLMAC_PWR_STE_MASK,0x100)){
            // Source power-switch notification is a directional scoreboard byte;
            // it has no same-value readback and is not a firmware ACK.
            (void)write(d,R_AX_SCOREBOARD+3,MAC_AX_NOTIFY_TP_MAJOR,1,0);
        }
        result.powered=finish(rc,true);return result.powered;
    }
    bool stop(const network::CalibrationSnapshot *snapshot,bool probeStage){
        if(!result.onAttempted||!result.on.writes||result.offAttempted)return false;
        if(!calibration(snapshot))return false;
        result.offAttempted=true;auto *d=&context_;d->result={};d->cleanup=true;d->clockStarted=false;d->probeDone=!probeStage;
        if(!check(d)){result.off=d->result;result.requiresRecovery=true;return false;}
        const auto state=rtw89_read32(d,R_AX_IC_PWR_STATE)&B_AX_WLMAC_PWR_STE_MASK;
        if(check(d)&&state!=0&&state!=0x100)fail(d,Error::precondition,R_AX_IC_PWR_STATE,0x100,state);
        if(check(d)&&state==0x100){
            // Stop/mask/drain is a real controller prerequisite, never performed
            // by freeing buffers or simply asserting a software readiness flag.
            equal(0x1000,0x2800,0);equal(0x101c,0x007f0f03,0);
            const u32 interruptMasks[]={0x1a0u,0x10b0u,0x13b0u};
            for(auto a:interruptMasks)equal(a,0xffffffff,0);
        }
        int rc=-1;if(check(d))rc=rtw8852b_pwr_off_func(d);
        if(!rc&&check(d)&&equal(R_AX_IC_PWR_STATE,B_AX_WLMAC_PWR_STE_MASK,0)&&
           equal(R_AX_SYS_PW_CTRL,B_AX_APFM_SWLPS,B_AX_APFM_SWLPS)&&siIdle(d))
            (void)write(d,R_AX_SCOREBOARD+3,MAC_AX_NOTIFY_PWR_MAJOR,1,0);
        result.returnedOff=finish(rc,false);
        if(result.returnedOff){result.powered=false;result.requiresRecovery=false;}
        else result.requiresRecovery=true;
        return result.returnedOff;
    }
};
} }
