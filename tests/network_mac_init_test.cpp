// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/MacInitialization.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
namespace m=rtl8852be::macinit;
struct Device {
    struct Op{unsigned width,address,value;bool write;};
    std::map<unsigned,uint8_t> bytes,xtal;std::vector<Op> trace;
    unsigned operations{},failAt{},delayCalls{},ignoreWrite=0xffffffff;uint64_t time=100;
    bool frozen{},backwards{},cancel{},delayFailure{},noDle{},noScheduler{},noCam{},noBacam{},badHfc{},xtalBusy{},badXtal{};
    uint16_t cmd=2;
    Device(){put(0x1010,0x1f0f00,4);put(0x1e0,0xe0,4);}
    void put(unsigned a,unsigned v,unsigned n){for(unsigned i=0;i<n;++i)bytes[a+i]=uint8_t(v>>(8*i));}
    unsigned get(unsigned a,unsigned n=4){unsigned v=0;for(unsigned i=0;i<n;++i)v|=unsigned(bytes[a+i])<<(8*i);return v;}
    bool read(unsigned a,unsigned n,unsigned &v){++operations;v=get(a,n);trace.push_back({n,a,v,false});return operations!=failAt;}
    bool write(unsigned a,unsigned v,unsigned n){
        ++operations;trace.push_back({n,a,v,true});if(operations==failAt)return false;if(a==ignoreWrite)return true;put(a,v,n);
        if(a==m::R_AX_DMAC_FUNC_EN&&(v&m::B_AX_DLE_WDE_EN)&&(v&m::B_AX_DLE_PLE_EN)&&!noDle){put(0x8d00,3,4);put(0x9100,3,4);}
        if(a==m::R_AX_SS_CTRL&&(v&m::B_AX_SS_EN)&&!noScheduler)put(a,get(a)|m::B_AX_SS_INIT_DONE_1,4);
        if(a==m::R_AX_ADDR_CAM_CTRL&&(v&m::B_AX_ADDR_CAM_CLR)&&!noCam)put(a,get(a)&~m::B_AX_ADDR_CAM_CLR,4);
        if(a==m::R_AX_RESPBA_CAM_CTRL&&(v&m::B_AX_BACAM_RST_MASK)&&!noBacam)put(a,get(a)&~m::B_AX_BACAM_RST_MASK,4);
        if(a==m::rtw8852b_page_regs.hci_fc_ctrl&&(v&9)==9)put(m::rtw8852b_page_regs.pub_page_info2,badHfc?445:446,4);
        if(a==m::R_AX_WLAN_XTAL_SI_CTRL&&!xtalBusy){
            const auto addr=v&255;
            if((v&m::B_AX_WL_XTAL_SI_MODE_MASK)==0){const auto mask=(v>>16)&255;
                xtal[addr]=uint8_t((xtal[addr]&~mask)|((v>>8)&mask));
            }else if((v&m::B_AX_WL_XTAL_SI_MODE_MASK)==(1u<<24)){
                v=(v&~0xff00u)|(unsigned(badXtal?0:xtal[addr])<<8);
            }
            put(a,v&~m::B_AX_WL_XTAL_SI_CMD_POLL,4);
        }
        return true;
    }
    bool read8(unsigned a,uint8_t &v){unsigned x;bool ok=read(a,1,x);v=uint8_t(x);return ok;}
    bool read16(unsigned a,uint16_t &v){unsigned x;bool ok=read(a,2,x);v=uint16_t(x);return ok;}
    bool read32(unsigned a,uint32_t &v){return read(a,4,v);}
    bool write8(unsigned a,uint8_t v){return write(a,v,1);}bool write16(unsigned a,uint16_t v){return write(a,v,2);}bool write32(unsigned a,uint32_t v){return write(a,v,4);}
    uint16_t command(){return cmd;}uint64_t nowUs(){if(backwards&&delayCalls)return --time;return time;}
    bool delayUs(unsigned us){++delayCalls;if(!frozen)time+=us;return !delayFailure;}
    bool cancelled(){return cancel;}
};
bool run(Device &d,m::InitResult &result){m::MacInitialization<Device> init(d);bool ok=init.enableRadio()&&init.enableSystem()&&init.initializeDmac()&&init.initializeCmac()&&init.finishTrx();result=init.result;return ok;}
int main(){
    {Device x;m::MacInitialization<Device> init(x);assert(!init.configureCut(2));assert(init.configureCut(0));
        assert(init.enableRadio());assert(!init.configureCut(1));}
    Device d;m::InitResult result;const bool success=run(d,result);
    if(!success)fprintf(stderr,"MAC failure stage=%u error=%u address=%x expected=%x actual=%x operations=%u\n",unsigned(result.stage),unsigned(result.error),result.address,result.expected,result.actual,d.operations);
    assert(success);const auto operations=d.operations;
    assert(result.radioEnabled&&result.systemReady&&result.packetEnginesReady&&result.cmacReady&&result.trxReady&&result.error==m::InitError::none);
    // Golden SCC hardware values independently read from pinned field layouts.
    assert(d.get(0x8c08)==0x01fe0000&&d.get(0x9008)==0x01f00401);
    assert(d.get(0x8c40)==0x01be01be&&d.get(0x8c44)==0x00300030);
    assert(d.get(0x9058)==0x00e50059);
    assert(d.get(0x8a04)==0x00280002);
    assert(d.get(m::R_AX_MGNT_FLTR)==0x55555555&&d.get(m::R_AX_CTRL_FLTR)==0x55555555&&d.get(m::R_AX_DATA_FLTR)==0x55555555);
    assert((d.get(m::R_AX_SEC_ENG_CTRL)&(m::B_AX_SEC_TX_ENC|m::B_AX_SEC_RX_DEC|m::B_AX_MC_DEC|m::B_AX_BC_DEC))==0);
    assert(m::u32_get_bits(d.get(m::R_AX_RX_FLTR_OPT),m::B_AX_RX_MPDU_MAX_LEN_MASK)==22);
    assert(m::u32_get_bits(d.get(m::R_AX_TRXPTCL_RRSR_CTL_0),m::B_AX_WMAC_RESP_RSC_MASK)==2);
    assert(!(d.get(m::R_AX_RXDMA_CTRL_0)&m::RX_FULL_MODE));
    for(const auto &op:d.trace)if(op.write){assert(op.address<0xe000);assert(op.address!=0x1000&&op.address!=0x1010&&op.address!=0x8380);}
    for(unsigned i=1;i<=operations;++i){Device f;f.failAt=i;assert(!run(f,result));
        assert(result.error!=m::InitError::none&&f.operations==i&&!result.trxReady);}
    for(unsigned mode=0;mode<8;++mode){Device f;
        if(mode==0)f.noDle=true;if(mode==1)f.noScheduler=true;if(mode==2)f.noCam=true;if(mode==3)f.noBacam=true;
        if(mode==4){f.noBacam=true;f.frozen=true;}if(mode==5)f.delayFailure=true;if(mode==6){f.noDle=true;f.backwards=true;}if(mode==7)f.badHfc=true;
        assert(!run(f,result)&&result.error!=m::InitError::none&&!result.cmacReady);
        assert(result.polls<2100);if(mode==3||mode==4)assert(result.address==m::R_AX_RESPBA_CAM_CTRL);
    }
    for(auto v:{0xffffffffu,0xdeadbeefu}){Device f;f.put(m::R_AX_CMAC_FUNC_EN,v,4);assert(!run(f,result)&&result.error==m::InitError::read);}
    // The CMAC recovery path intentionally writes ALLCKEN (all ones). It is
    // valid for this clock register, but not a general invalid-read exemption.
    {Device f;f.put(m::R_AX_CK_EN,0xffffffff,4);assert(run(f,result));}
    {Device f;f.put(m::R_AX_WDRLS_ERR_IMR,0xffffffff,4);assert(run(f,result));
        assert((f.get(m::R_AX_WDRLS_ERR_IMR)&m::B_AX_WDRLS_IMR_EN_CLR)==m::B_AX_WDRLS_IMR_SET);}
    {Device f;f.put(m::R_AX_WDRLS_ERR_IMR,0xffffffff,4);f.ignoreWrite=m::R_AX_WDRLS_ERR_IMR;
        assert(!run(f,result)&&result.error==m::InitError::precondition&&result.address==m::R_AX_WDRLS_ERR_IMR);}
    {Device f;f.put(m::R_AX_WDRLS_ERR_IMR,0xdeadbeef,4);
        assert(!run(f,result)&&result.error==m::InitError::read&&result.address==m::R_AX_WDRLS_ERR_IMR);}
    // Warm-start all-ones masks: clear/set must prove each register writable,
    // preserve reserved bits, and leave PCI interrupts and DMA disabled.
    struct ImrCase{unsigned address,clear,set;};
    const ImrCase imrs[]={
        {m::R_AX_WDRLS_ERR_IMR,m::B_AX_WDRLS_IMR_EN_CLR,m::B_AX_WDRLS_IMR_SET},
        {m::R_AX_WDE_ERR_IMR,m::B_AX_WDE_IMR_CLR,m::B_AX_WDE_IMR_SET},
        {m::R_AX_PLE_ERR_IMR,m::B_AX_PLE_IMR_CLR,m::B_AX_PLE_IMR_SET},
        {m::R_AX_HOST_DISPATCHER_ERR_IMR,m::B_AX_HOST_DISP_IMR_CLR,m::B_AX_HOST_DISP_IMR_SET},
        {m::R_AX_CPU_DISPATCHER_ERR_IMR,m::B_AX_CPU_DISP_IMR_CLR,m::B_AX_CPU_DISP_IMR_SET},
        {m::R_AX_OTHER_DISPATCHER_ERR_IMR,m::B_AX_OTHER_DISP_IMR_CLR,0},
        {m::R_AX_CPUIO_ERR_IMR,m::B_AX_CPUIO_IMR_CLR,m::B_AX_CPUIO_IMR_SET}};
    {Device f;for(const auto &imr:imrs)f.put(imr.address,0xffffffff,4);
        assert(run(f,result));
        for(const auto &imr:imrs)assert(f.get(imr.address)==((0xffffffffu&~imr.clear)|imr.set));
        assert(f.get(0x1a0)==0&&f.get(0x10b0)==0&&f.get(0x13b0)==0&&f.cmd==2);}
    for(const auto &imr:imrs){
        {Device f;f.put(imr.address,0xffffffff,4);f.ignoreWrite=imr.address;
            assert(!run(f,result)&&result.error==m::InitError::precondition&&result.address==imr.address&&!result.trxReady);
            assert(f.get(0x1a0)==0&&f.get(0x10b0)==0&&f.get(0x13b0)==0&&f.cmd==2);}
        if(imr.set){Device f;f.ignoreWrite=imr.address;
            assert(!run(f,result)&&result.error==m::InitError::precondition&&result.address==imr.address&&!result.trxReady);}
        {Device f;f.put(imr.address,0xdeadbeef,4);
            assert(!run(f,result)&&result.error==m::InitError::read&&result.address==imr.address);}
    }
    // Never extend this exception to a set-only or mixed mask/status register.
    for(auto reg:{m::R_AX_PKTIN_ERR_IMR,m::R_AX_TXPKTCTL_ERR_IMR_ISR,m::R_AX_BBRPT_CHINFO_ERR_IMR_ISR}){
        assert(!m::reportImrMask(reg));Device f;f.put(reg,0xffffffff,4);
        assert(!run(f,result)&&result.error==m::InitError::read&&result.address==reg);}
    {Device f;f.cancel=true;assert(!run(f,result)&&!f.operations);}
    {Device f;f.cmd=6;assert(!run(f,result)&&!f.operations);}
    {Device f;m::MacInitialization<Device> init(f);assert(!init.enableSystem()&&!init.initializeDmac()&&!init.initializeCmac()&&!init.finishTrx()&&!f.operations);
        assert(init.enableRadio());assert(!init.enableRadio());assert(init.enableSystem());assert(!init.enableSystem());assert(init.initializeDmac());assert(!init.initializeDmac());assert(init.initializeCmac());assert(!init.initializeCmac());assert(init.finishTrx());assert(!init.finishTrx());}
    assert((d.get(m::R_AX_WDRLS_CFG)&m::B_AX_WDRLS_MODE_MASK)==0);
    assert(m::u32_get_bits(d.get(m::R_AX_RLSRPT0_CFG1),m::B_AX_RLSRPT0_AGGNUM_MASK)==30);
    assert(m::u32_get_bits(d.get(m::R_AX_RLSRPT0_CFG1),m::B_AX_RLSRPT0_TO_MASK)==255);
    assert((d.get(m::R_AX_RLSRPT0_CFG0)&m::B_AX_RLSRPT0_FLTR_MAP_MASK)==m::B_AX_RLSRPT0_FLTR_MAP_MASK);
    assert(d.get(m::R_AX_DMAC_ERR_IMR)==m::DMAC_ERR_IMR_EN&&d.get(m::R_AX_CMAC_ERR_IMR)==m::CMAC0_ERR_IMR_EN);
    // Configuring internal causes must not unmask the PCI interrupt or start DMA.
    assert(d.get(0x1a0)==0&&d.get(0x10b0)==0&&d.get(0x13b0)==0&&d.cmd==2);
    for(auto reg:{m::R_AX_WDRLS_CFG,m::R_AX_RLSRPT0_CFG0,m::R_AX_RLSRPT0_CFG1,m::R_AX_DMAC_ERR_IMR,m::R_AX_CMAC_ERR_IMR}){
        Device f;f.ignoreWrite=reg;if(reg==m::R_AX_WDRLS_CFG)f.put(reg,m::B_AX_WDRLS_MODE_MASK,4);
        assert(!run(f,result)&&result.error==m::InitError::precondition&&result.address==reg&&!result.trxReady);
    }
    assert(!m::initWriteAddress(m::R_AX_AFE_CTRL1)&&!m::initWriteAddress(m::R_AX_SYS_ISO_CTRL_EXTEND));
    assert(d.xtal[0x80]==0xc7&&d.xtal[0x81]==0xc7);
    for(unsigned mode=0;mode<4;++mode){Device f;
        if(mode==0)f.xtalBusy=true;if(mode==1){f.xtalBusy=true;f.frozen=true;}if(mode==2)f.badXtal=true;
        if(mode==3)f.put(m::R_AX_WLAN_XTAL_SI_CTRL,m::B_AX_WL_XTAL_SI_CMD_POLL,4);
        assert(!run(f,result)&&!result.radioEnabled&&!result.systemReady&&result.address==m::R_AX_WLAN_XTAL_SI_CTRL);
        assert(result.polls<=1001);
    }
    printf("PASS: 8852B SCC MAC register model; BB/RF enable/system/DLE/HFC/CMAC/internal IMR/host reports, %u read/write failures, XTAL/scheduler/CAM/BA-CAM timeouts, frozen/backward clocks, software crypto policy\n",operations);
}
