// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "Rtw8852bMacConstants.hpp"
namespace rtl8852be { namespace macinit {
enum class InitError {none,precondition,read,write,timeout,clock,cancelled,upstream};
enum class InitStage {idle,system,dmac,cmac,reports};
struct InitResult {
    InitError error{InitError::none};InitStage stage{InitStage::idle};
    u32 address{},expected{},actual{};unsigned reads{},writes{},polls{},warnings{};
    bool systemReady{},packetEnginesReady{},cmacReady{},trxReady{};
};
inline bool initAddress(u32 address){
    for(auto a:macInitRegisters)if(a==address)return true;
    for(unsigned ch=0;ch<=12;++ch)if(address==rtw8852b_page_regs.ach_page_ctrl+ch*4||address==rtw8852b_page_regs.ach_page_info+ch*4)return true;
    // Preconditions and DLE/HFC readback; these are read-only in this adapter.
    switch(address){case 0x1e0:case 0x1000:case 0x1010:case 0x101c:case 0x1a0:
    case 0x10b0:case 0x13b0:case 0x8d00:case 0x9100:case 0x8a00:case 0x9008:case 0x9058:return true;default:return false;}
}
inline bool initWriteAddress(u32 address){
    // Single-CMAC RTL8852B: exclude the generic reference's CMAC1 rail path.
    if(address>=0xe000||address==R_AX_AFE_CTRL1||address==R_AX_SYS_ISO_CTRL_EXTEND)return false;
    const unsigned channels[]={0,1,2,3,8,9};
    for(auto ch:channels)if(address==rtw8852b_page_regs.ach_page_ctrl+ch*4)return true;
    for(auto a:macInitRegisters)if(a==address)return true;return false;
}
// Real reference system/packet/CMAC operations, with failure-latching I/O.
// D exposes command(), read8/16/32(address, out), write8/16/32(address,value),
// nowUs(), delayUs(us), cancelled(). Shutdown/power cycling is controller-owned:
// any failed phase leaves this one-shot object faulted and MUST prevent DMA start.
template<class D> class MacInitialization {
    struct rtw89_chip_info {rtw89_core_chip_id chip_id=RTL8852B;const rtw89_rrsr_cfgs *rrsr_cfgs=&rtw8852b_rrsr_cfgs;const rtw89_page_regs *page_regs=&rtw8852b_page_regs;const rtw89_imr_info *imr_info=&rtw8852b_imr_info;};
    struct Context {
        D &io;InitResult &result;rtw89_chip_info chipStorage{};rtw89_chip_info *chip=&chipStorage;
        struct {u32 rx_fltr=DEFAULT_AX_RX_FLTR;u8 cv=CHIP_CBV;} hal;
        struct {struct {u32 c0_rx_qta{},c1_rx_qta{},ple_pg_size{};} dle_info;rtw89_hfc_param hfc_param{};} mac;
        struct {rtw89_hci_type type=RTW89_HCI_TYPE_PCIE;} hci;
        uint64_t begin{},previous{};bool clockStarted{};u32 lastAddress{},lastValue{};
        Context(D &d,InitResult &r):io(d),result(r){}
    };
    static bool fail(Context *d,InitError error,u32 a=0,u32 expected=0,u32 actual=0){
        if(d->result.error==InitError::none){d->result.error=error;d->result.address=a;d->result.expected=expected;d->result.actual=actual;}return false;
    }
    static bool check(Context *d){
        if(d->result.error!=InitError::none)return false;
        if(d->io.cancelled())return fail(d,InitError::cancelled);
        const auto now=d->io.nowUs();
        if(!d->clockStarted){d->begin=d->previous=now;d->clockStarted=true;}
        if(now<d->previous)return fail(d,InitError::clock);d->previous=now;
        if(now-d->begin>500000||d->result.reads+d->result.writes>100000)return fail(d,InitError::timeout);
        return true;
    }
    static u8 rtw89_read8(Context *d,u32 a){u8 v=0;if(!check(d))return 0;
        ++d->result.reads;if(!initAddress(a)||!d->io.read8(a,v))fail(d,InitError::read,a);d->lastAddress=a;d->lastValue=v;return v;}
    static u16 rtw89_read16(Context *d,u32 a){u16 v=0;if(!check(d))return 0;
        ++d->result.reads;if(!initAddress(a)||!d->io.read16(a,v))fail(d,InitError::read,a);d->lastAddress=a;d->lastValue=v;return v;}
    static u32 rtw89_read32(Context *d,u32 a){u32 v=0;if(!check(d))return 0;
        ++d->result.reads;if(!initAddress(a)||!d->io.read32(a,v)||
            (v==0xffffffff&&a!=R_AX_DMAC_ERR_IMR&&a!=R_AX_CMAC_ERR_IMR)||v==0xdeadbeef){
            fail(d,InitError::read,a,0,v);}
        d->lastAddress=a;d->lastValue=v;return v;}
    static void rtw89_write8(Context *d,u32 a,u8 v){if(!check(d))return;++d->result.writes;
        if(!initWriteAddress(a)||!d->io.write8(a,v))fail(d,InitError::write,a,v);}
    static void rtw89_write16(Context *d,u32 a,u16 v){if(!check(d))return;++d->result.writes;
        if(!initWriteAddress(a)||!d->io.write16(a,v))fail(d,InitError::write,a,v);}
    static void rtw89_write32(Context *d,u32 a,u32 v){if(!check(d))return;++d->result.writes;
        if(!initWriteAddress(a)||!d->io.write32(a,v))fail(d,InitError::write,a,v);}
    static u32 rtw89_read32_mask(Context *d,u32 a,u32 m){return (rtw89_read32(d,a)&m)>>shift(m);}
    static void rtw89_write32_mask(Context *d,u32 a,u32 m,u32 v){const auto old=rtw89_read32(d,a);rtw89_write32(d,a,u32_replace_bits(old,v,m));}
    static void rtw89_write16_mask(Context *d,u32 a,u16 m,u16 v){const auto old=rtw89_read16(d,a);rtw89_write16(d,a,u16_replace_bits(old,v,m));}
    static void rtw89_write8_mask(Context *d,u32 a,u8 m,u8 v){const auto old=rtw89_read8(d,a);rtw89_write8(d,a,u8(u32_replace_bits(old,v,m)));}
    static void rtw89_write32_set(Context *d,u32 a,u32 m){const auto old=rtw89_read32(d,a);rtw89_write32(d,a,old|m);}
    static void rtw89_write32_clr(Context *d,u32 a,u32 m){const auto old=rtw89_read32(d,a);rtw89_write32(d,a,old&~m);}
    static void rtw89_write16_set(Context *d,u32 a,u16 m){const auto old=rtw89_read16(d,a);rtw89_write16(d,a,u16(old|m));}
    static void rtw89_write8_set(Context *d,u32 a,u8 m){const auto old=rtw89_read8(d,a);rtw89_write8(d,a,u8(old|m));}
    static void rtw89_write8_clr(Context *d,u32 a,u8 m){const auto old=rtw89_read8(d,a);rtw89_write8(d,a,u8(old&~m));}
    static void rtw89_err(Context *d,const char *,...){++d->result.warnings;}
    static void rtw89_warn(Context *d,const char *,...){++d->result.warnings;}
    static u32 rtw89_mac_reg_by_idx(Context *d,u32 a,u8 index){if(index)fail(d,InitError::precondition,a,0,index);return a;}
    static bool equals(Context *d,u32 a,u32 m,u32 wanted){const auto v=rtw89_read32(d,a);
        return check(d)&&((v&m)==wanted||fail(d,InitError::precondition,a,wanted,v));}
    static int rtw89_mac_check_mac_en(Context *d,u8 band,rtw89_mac_hwmod_sel sel){
        if(band||!check(d))return -macInvalid;
        return equals(d,sel==RTW89_DMAC_SEL?R_AX_DMAC_FUNC_EN:R_AX_CMAC_FUNC_EN,
            sel==RTW89_DMAC_SEL?B_AX_DMAC_FUNC_EN:B_AX_CMAC_EN,
            sel==RTW89_DMAC_SEL?B_AX_DMAC_FUNC_EN:B_AX_CMAC_EN)?0:-macInvalid;
    }
    template<class Predicate> static int poll(Context *d,unsigned delay,unsigned timeout,Predicate ready){
        const auto start=d->io.nowUs();
        for(unsigned i=0;i<=timeout/(delay?delay:1);++i){
            if(!check(d))return -macInvalid;
            if(d->previous<start){fail(d,InitError::clock);return -macInvalid;}
            const bool done=ready();++d->result.polls;if(!check(d))return -macInvalid;
            if(done)return 0;
            if(d->previous-start>=timeout)break;
            if(!d->io.delayUs(delay)){fail(d,InitError::write);return -macInvalid;}
        }
        fail(d,InitError::timeout,d->lastAddress,0,d->lastValue);return -macInvalid;
    }
#define R16_MAC_POLL(op,value,condition,delay,timeout,sleep,device,...) \
    poll(device,delay,timeout,[&](){value=op(device,__VA_ARGS__);return bool(condition);})
#include "Rtw8852bMacFunctions.inc"
#undef R16_MAC_POLL
    bool stopped(){
        if((context_.io.command()&6)!=2)return fail(&context_,InitError::precondition,4,2,context_.io.command());
        return equals(&context_,0x1000,0x2800,0)&&equals(&context_,0x1010,0x1f0f00,0x1f0f00)&&
            equals(&context_,0x101c,0x7f0f03,0)&&equals(&context_,0x1a0,0xffffffff,0)&&
            equals(&context_,0x10b0,0xffffffff,0)&&equals(&context_,0x13b0,0xffffffff,0);
    }
    bool finished(int rc){if(rc&&result.error==InitError::none)fail(&context_,InitError::upstream,0,0,u32(rc));return check(&context_);}
public:
    InitResult result{};
private:
    Context context_;
public:
    explicit MacInitialization(D &io):context_(io,result){}
    MacInitialization(const MacInitialization &)=delete;MacInitialization &operator=(const MacInitialization &)=delete;
    bool enableSystem(){
        if(result.stage!=InitStage::idle)return false;result.stage=InitStage::system;
        if(!stopped()||!equals(&context_,0x1e0,0xe0,0xe0))return false;
        result.systemReady=finished(sys_init_ax(&context_));return result.systemReady;
    }
    // RTL8852B PCIe SCC DLE quotas and HFC configuration from the pinned chip
    // tables, followed by station scheduler, MPDU and security-engine setup.
    // Preload is correctly absent for this chip (rtw89_mac_preload_init).
    bool initializeDmac(){
        if(!result.systemReady||result.stage!=InitStage::system||!check(&context_))return false;
        result.stage=InitStage::dmac;if(!stopped())return false;
        auto *d=&context_;
        const rtw89_dle_size wde{wde_size7[0],wde_size7[1],wde_size7[2]},pleSize{ple_size6[0],ple_size6[1],ple_size6[2]};
        const rtw89_dle_mem cfg{&wde,&pleSize};
        dle_func_en_ax(d,false);dle_clk_en_ax(d,true);
        if(!finished(dle_mix_cfg_ax(d,&cfg)))return false;
        const u32 wdeRegs[]={R_AX_WDE_QTA0_CFG,R_AX_WDE_QTA1_CFG,R_AX_WDE_QTA3_CFG,R_AX_WDE_QTA4_CFG};
        for(unsigned i=0;i<4;++i){const u32 value=u32_encode_bits(wde_qt7[i],B_AX_WDE_MIN_SIZE_MASK)|u32_encode_bits(wde_qt7[i],B_AX_WDE_MAX_SIZE_MASK);
            rtw89_write32(d,wdeRegs[i],value);if(!equals(d,wdeRegs[i],0xffffffff,value))return false;}
        for(unsigned i=0;i<11;++i){const u32 value=u32_encode_bits(ple_qt18[i],B_AX_PLE_MIN_SIZE_MASK)|u32_encode_bits(ple_qt58[i],B_AX_PLE_MAX_SIZE_MASK);
            rtw89_write32(d,R_AX_PLE_QTA0_CFG+i*4,value);if(!equals(d,R_AX_PLE_QTA0_CFG+i*4,0xffffffff,value))return false;}
        dle_func_en_ax(d,true);
        if(!finished(chk_dle_rdy_ax(d,true))||!finished(chk_dle_rdy_ax(d,false)))return false;
        auto &hfc=context_.mac.hfc_param;hfc={};hfc.ch_cfg=rtw8852b_hfc_chcfg_pcie;
        hfc.pub_cfg=rtw8852b_hfc_pubcfg_pcie;hfc.prec_cfg=hfcPreccfg;hfc.mode=RTW89_HCIFC_POH;
        hfc_func_en_ax(d,false,false);
        const unsigned channels[]={0,1,2,3,8,9};
        for(auto ch:channels)if(!finished(hfc_ch_ctrl(d,u8(ch))))return false;
        if(!finished(hfc_pub_ctrl(d)))return false;
        hfc_mix_cfg_ax(d);hfc_func_en_ax(d,true,true);
        if(!check(d)||!d->io.delayUs(10))return fail(d,InitError::write);
        for(auto ch:channels)if(!finished(hfc_upd_ch_info(d,u8(ch))))return false;
        hfc_get_mix_info_ax(d);if(!finished(hfc_pub_info_chk(d)))return false;
        if(hfc.pub_cfg.pub_max!=rtw8852b_hfc_pubcfg_pcie.pub_max||!hfc.en||!hfc.h2c_en||hfc.mode!=RTW89_HCIFC_POH)
            return fail(d,InitError::precondition,rtw8852b_page_regs.hci_fc_ctrl);
        if(!equals(d,0x8a00,9,9))return false;
        const auto ple=rtw89_read32(&context_,0x9008),quota=rtw89_read32(&context_,0x9058);
        if(!check(d)||(ple&3)!=1||(quota&0xfff)!=ple_qt18[6])return fail(d,InitError::precondition,0x9058,ple_qt18[6],quota);
        context_.mac.dle_info={quota&0xfff,0,ple_size6[0]};
        if(!finished(sta_sch_init_ax(&context_))||!finished(mpdu_proc_init_ax(&context_))||!finished(sec_eng_init_ax(&context_)))return false;
        // Current net80211 path encrypts/decrypts in software, with no key CAM.
        // Explicitly disable chip crypto instead of risking double transforms.
        rtw89_write32_clr(&context_,R_AX_SEC_ENG_CTRL,B_AX_SEC_TX_ENC|B_AX_SEC_RX_DEC|B_AX_MC_DEC|B_AX_BC_DEC);
        result.packetEnginesReady=check(&context_);return result.packetEnginesReady;
    }
    bool initializeCmac(){
        if(!result.packetEnginesReady||result.stage!=InitStage::dmac||!check(&context_))return false;
        result.stage=InitStage::cmac;if(!stopped())return false;
        result.cmacReady=finished(cmac_init_ax(&context_,0));return result.cmacReady;
    }
    bool finishTrx(){
        if(!result.cmacReady||result.stage!=InitStage::cmac||!check(&context_))return false;
        result.stage=InitStage::reports;if(!stopped())return false;
        if(!finished(enable_imr_ax(&context_,0,RTW89_DMAC_SEL))||
           !finished(enable_imr_ax(&context_,0,RTW89_CMAC_SEL)))return false;
        err_imr_ctrl_ax(&context_,true);
        if(!finished(set_host_rpr_ax(&context_)))return false;
        // RPQ ownership depends on reporting every release, including failures.
        if(!equals(&context_,R_AX_WDRLS_CFG,B_AX_WDRLS_MODE_MASK,fieldPrep(B_AX_WDRLS_MODE_MASK,RTW89_RPR_MODE_POH))||
           !equals(&context_,R_AX_RLSRPT0_CFG0,B_AX_RLSRPT0_FLTR_MAP_MASK,B_AX_RLSRPT0_FLTR_MAP_MASK)||
           !equals(&context_,R_AX_RLSRPT0_CFG1,B_AX_RLSRPT0_AGGNUM_MASK|B_AX_RLSRPT0_TO_MASK,
               fieldPrep(B_AX_RLSRPT0_AGGNUM_MASK,30)|fieldPrep(B_AX_RLSRPT0_TO_MASK,255))||
           !equals(&context_,R_AX_DMAC_ERR_IMR,0xffffffff,DMAC_ERR_IMR_EN)||
           !equals(&context_,R_AX_CMAC_ERR_IMR,0xffffffff,CMAC0_ERR_IMR_EN))return false;
        result.trxReady=check(&context_);return result.trxReady;
    }
};
} }
