// SPDX-License-Identifier: BSD-3-Clause
#include "../src/network/RadioTables.hpp"
#include "../src/network/EfuseCalibration.hpp"
#include "../src/network/RadioAccess.hpp"
#include "../src/network/RadioFirmware.hpp"
#include "../src/network/RadioInitialization.hpp"
#include "../src/network/BasebandGain.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cerrno>
using namespace rtl8852be::network;
namespace oracle {
using u8=uint8_t;using u32=uint32_t;using s8=int8_t;
struct rtw89_reg2_def {u32 addr,data;};
enum rtw89_rf_path {pathA,pathB};
struct rtw89_phy_table {const rtw89_reg2_def *regs;size_t n_regs;rtw89_rf_path rf_path;};
struct rtw89_chip_info {u8 rf_path_num=2;};
const rtw89_chip_info chip2{};
struct rtw89_efuse {u8 rfe_type;};
struct rtw89_phy_bb_gain_info:BasebandGain{};
struct rtw89_dev {rtw89_efuse efuse;struct {u8 cv;}hal;bool error{};
    const rtw89_chip_info *chip=&chip2;struct {rtw89_phy_bb_gain_info ax;}bb_gain{};
};
constexpr unsigned RTW89_BB_GAIN_BAND_NR=8,RTW89_CHANNEL_WIDTH_20=0,RTW89_CHANNEL_WIDTH_40=1,RTW89_CHANNEL_WIDTH_80=2,RTW89_CHANNEL_WIDTH_160=3;
constexpr u32 PHY_HEADLINE_VALID=15,PHY_COND_DONT_CARE=255;
constexpr u32 PHY_COND_BRANCH_IF=8,PHY_COND_BRANCH_ELIF=9,PHY_COND_BRANCH_ELSE=10,PHY_COND_BRANCH_END=11,PHY_COND_CHECK=4;
u32 get_phy_headline(u32 a){return a>>28;}u32 get_phy_cond(u32 a){return a>>28;}
u32 get_phy_target(u32 a){return a&0x0fffffff;}u32 get_phy_compare(u8 r,u8 c){return (u32(r)<<16)|c;}
u8 get_phy_cond_rfe(u32 a){return u8(a>>16);}u8 get_phy_cond_cv(u32 a){return u8(a);}
template<class... Args>void rtw89_err(rtw89_dev *d,const char *,Args...){d->error=true;}
template<class... Args>void rtw89_warn(rtw89_dev *d,const char *,Args...){d->error=true;}
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif
#define fallthrough (void)0
#include "network_radio_reference.inc"
#undef fallthrough
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}
struct Trace {uint32_t kind,address,value;bool operator==(const Trace &v)const{return kind==v.kind&&address==v.address&&value==v.value;}};
struct Sink {
    std::vector<Trace> trace;size_t failAt=size_t(-1),cancelAt=size_t(-1);
    bool cancelled(){return trace.size()==cancelAt;}
    bool emit(uint32_t k,uint32_t a,uint32_t v){if(trace.size()==failAt)return false;trace.push_back({k,a,v});return true;}
    bool bbWrite(uint32_t a,uint32_t v){return emit(0,a,v);}
    bool rfWrite(uint8_t p,uint32_t a,uint32_t v){return emit(1+p,a,v);}
    bool gainRecord(uint32_t a,uint32_t v){return emit(3,a,v);}
    bool delayUs(unsigned v){return emit(4,0,v);}
};
struct OracleSink {Sink sink;RadioTableKind kind;};
void capture(oracle::rtw89_dev *,const oracle::rtw89_reg2_def *r,oracle::rtw89_rf_path p,void *opaque){
    auto &o=*static_cast<OracleSink*>(opaque);
    if(o.kind==RadioTableKind::radio){o.sink.rfWrite(uint8_t(p),r->addr,r->data);return;}
    if(o.kind==RadioTableKind::gain){o.sink.gainRecord(r->addr,r->data);return;}
    switch(r->addr){
        case 0xfe:o.sink.delayUs(50000);return;case 0xfd:o.sink.delayUs(5000);return;
        case 0xfc:o.sink.delayUs(1000);return;case 0xfb:o.sink.delayUs(50);return;
        case 0xfa:o.sink.delayUs(5);return;case 0xf9:o.sink.delayUs(1);return;
    }
    if(r->data!=0xbabecafe)o.sink.bbWrite(r->addr,r->data);
}
void differential(){
    size_t comparisons=0;
    for(const auto *table:{&bbTable,&radioATable,&radioBTable,&nctlTable,&gainTable}){
        std::vector<oracle::rtw89_reg2_def> regs;for(size_t i=0;i<table->count;++i)regs.push_back({table->registers[i].address,table->registers[i].value});
        oracle::rtw89_phy_table ot{regs.data(),regs.size(),oracle::rtw89_rf_path(table->path)};
        // All RFE identifiers, chip cuts 0..3 and wildcard cut, full write traces.
        for(unsigned rfe=0;rfe<256;++rfe)for(unsigned cut:{0u,1u,2u,3u,255u}){
            oracle::rtw89_dev dev{{uint8_t(rfe)},{uint8_t(cut)},false};OracleSink original{{},table->kind};
            oracle::rtw89_phy_init_reg(&dev,&ot,capture,&original);
            Sink actual;auto result=applyRadioTable(*table,uint8_t(rfe),uint8_t(cut),actual);
            if(dev.error){assert(result.status!=RadioTableStatus::ok);assert(actual.trace.empty());}
            else {assert(result.status==RadioTableStatus::ok);assert(original.sink.trace==actual.trace);}
            ++comparisons;
        }
    }
    std::printf("Radio: %zu pinned upstream trace comparisons passed\n",comparisons);
}
void branchFailures(){
    RadioRegister rows[]={{0xf0010000,0},{0x80010000,0},{0x40000000,0},{0x100,1},{0xa0000000,0},{0xb0000000,0}};
    RadioTable table{rows,6,RadioTableKind::baseband,0};Sink s;
    assert(applyRadioTable(table,1,0,s).status==RadioTableStatus::ok&&s.trace.size()==1);
    rows[5].address=0x100;s.trace.clear();assert(applyRadioTable(table,1,0,s).status==RadioTableStatus::malformed&&s.trace.empty());
    rows[5].address=0xb0000000;rows[1].address=0x80020000;assert(applyRadioTable(table,1,0,s).status==RadioTableStatus::unmatched&&s.trace.empty());
    rows[1].address=0x80010000;s.failAt=0;assert(applyRadioTable(table,1,0,s).status==RadioTableStatus::ioError);
    s.failAt=size_t(-1);s.cancelAt=0;assert(applyRadioTable(table,1,0,s).status==RadioTableStatus::cancelled);
    RadioRegister rf[]={{0xf9,0x12345},{0xfe,0x6789}};RadioTable rt{rf,2,RadioTableKind::radio,1};Sink r;
    assert(applyRadioTable(rt,0,0,r).writes==2&&r.trace[0].kind==2&&r.trace[1].address==0xfe);
}
void efuse(){
    uint8_t physical[96],logical[16];std::memset(physical,0xff,sizeof physical);
    // Header block 1, word 0 enabled, later entry overwrites the same word.
    physical[4]=0x30;physical[5]=0x1e;physical[6]=0x12;physical[7]=0x34;
    physical[8]=0x30;physical[9]=0x1e;physical[10]=0xab;physical[11]=0xcd;
    assert(decodeEfuse(physical,sizeof physical,logical,sizeof logical)==EfuseStatus::ok);
    for(unsigned i=0;i<16;++i)assert(logical[i]==(i==8?0xab:i==9?0xcd:0xff));
    physical[9]=0x2e;std::memset(logical,0x55,sizeof logical);
    assert(decodeEfuse(physical,sizeof physical,logical,sizeof logical)==EfuseStatus::outOfRange);
    for(auto v:logical)assert(v==0x55);
    physical[9]=0x1e;assert(decodeEfuse(physical,15,logical,sizeof logical)==EfuseStatus::truncated);
    assert(decodeEfuse(physical,sizeof physical,physical+8,16)==EfuseStatus::overlap);
    uint8_t bank[2048];std::memset(bank,0xff,sizeof bank);BoardCalibration b;
    assert(parseBoardCalibration(bank,sizeof bank,b)&&!b.identityValid&&!b.xtalValid&&!b.gainOffsetValid);
    bank[0x400]=2;bank[0x401]=3;bank[0x2ca]=5;bank[0x2b9]=0x20;
    const size_t gain=offsetof(rtl8852be::reference::rtw8852b_efuse,rx_gain_2g_cck);bank[gain]=0x8f;
    assert(parseBoardCalibration(bank,sizeof bank,b)&&b.identityValid&&b.xtalValid&&b.gainOffsetValid);
    assert(b.mac[0]==2&&b.rfe==5&&b.gainOffset[0][0]==-8&&b.gainOffset[1][0]==-1);
    assert(!parseBoardCalibration(bank,0x400,b)&&!b.identityValid);
    uint8_t phy[128];std::memset(phy,0xff,sizeof phy);PhyCalibration p;
    assert(parsePhyCalibration(phy,sizeof phy,p)&&!p.tssiValid&&!p.thermalValid);
    for(auto &path:p.tssiTrim)for(auto v:path)assert(v==0);
    phy[0x5e9-0x580]=0xaa;phy[0x5d6-0x580]=0x80;phy[0x5bb-0x580]=0x18;
    assert(parsePhyCalibration(phy,sizeof phy,p)&&p.powerValid&&p.tssiTrim[0][0]==-128&&p.gainComp[0][0]==-8);
    // Short/random banks: guards intact, invalid bank leaves all output unchanged.
    uint32_t rng=0x8852be;uint8_t fuzz[128],guard[66];
    for(unsigned n=0;n<100000;++n){
        for(auto &v:fuzz){rng=rng*1664525+1013904223;v=uint8_t(rng>>24);}
        std::memset(guard,0x5a,sizeof guard);const auto status=decodeEfuse(fuzz,n%129,guard+1,64,n%9);
        assert(guard[0]==0x5a&&guard[65]==0x5a);
        if(status!=EfuseStatus::ok)for(auto v:guard)assert(v==0x5a);
    }
    std::puts("eFuse: bank bounds, calibration and 100000 malformed inputs passed");
}
struct RadioIo {
    uint32_t registers[0x20000/4]{};std::vector<Trace> trace;
    bool cancel{},fail{},busy{},noDone{},nctlStuck{},busyAfterWrite{};unsigned delayed{};
    size_t failAt=size_t(-1),calls{};
    bool cancelled()const{return cancel;}
    bool read32(uint32_t address,uint32_t &v){
        if(fail||calls++==failAt||address>=sizeof registers||(address&3))return false;
        v=registers[address/4];if(address==0x1174c&&busy)v|=0x03000000;
        if(address==0x18080&&nctlStuck)v=0;
        trace.push_back({0,address,v});return true;
    }
    bool write32(uint32_t address,uint32_t v){
        if(fail||calls++==failAt||address>=sizeof registers||(address&3))return false;
        registers[address/4]=v;trace.push_back({1,address,v});
        if(address==0x10378&&!noDone)registers[0x1174c/4]=0x040abcde;
        if(address==0x10370&&busyAfterWrite)busy=true;
        return true;
    }
    bool delayUs(unsigned us){delayed+=us;return !fail;}
};
void radioAccess(){
    RadioIo io;RadioAccess<RadioIo> rf(io);uint32_t value;
    io.registers[0x1e048/4]=0xfff12345;
    assert(rf.readRf(0,0x10012,0xff0,value)&&value==0x34);
    assert(rf.writeRf(0,0x10012,0xff0,0x67)&&io.registers[0x1e048/4]==0xfff12675&&io.delayed==1);
    assert(rf.writeRf(1,0x55,0xfffff,0xabcde)&&io.registers[0x10370/4]==0x155abcde);
    assert(rf.writeRf(0,0x12,0xff0,0x67)&&io.registers[0x10374/4]==0xff0&&io.registers[0x10370/4]==0x81200670);
    assert(rf.readRf(1,0x22,0xff0,value)&&value==0xcd&&io.registers[0x10378/4]==0x122);
    for(uint32_t a:{0x10000u,0x10370u,0x1174cu})assert(io.registers[a/4]==(a==0x10370?0x81200670u:a==0x1174c?0x040abcdeu:0));
    size_t before=io.trace.size();assert(!rf.writeRf(2,0,0xfffff,0)&&rf.status()==RadioIoStatus::invalid);
    assert(!rf.writeRf(0,0x20000,0xfffff,0));assert(!rf.readRf(0,0,0,value));assert(io.trace.size()==before);
    io.busy=true;unsigned d=io.delayed;assert(!rf.writeRf(0,0,0xfffff,0)&&rf.status()==RadioIoStatus::timeout&&io.delayed-d==30);
    io.busy=false;io.noDone=true;io.registers[0x1174c/4]=0;d=io.delayed;
    assert(!rf.readRf(0,1,0xfffff,value)&&rf.status()==RadioIoStatus::timeout&&io.delayed-d==32);
    io.cancel=true;before=io.trace.size();assert(!rf.drain()&&rf.status()==RadioIoStatus::cancelled&&io.trace.size()==before);
    io.cancel=false;io.fail=true;assert(!rf.drain()&&rf.status()==RadioIoStatus::ioError);
    io.fail=false;io.registers[0x1174c/4]=0xffffffff;assert(!rf.drain()&&rf.status()==RadioIoStatus::ioError);
    assert(rf.writeBaseband(0x8080,4)&&io.registers[0x18080/4]==4);
    assert(!rf.writeBaseband(0x10000,0)&&!rf.writeBaseband(0x8081,0));
    std::puts("RF v1: direct/serial addressing, masks, timeout, cancellation and missing device passed");
}
void firmwarePages(){
    uint8_t storage[6000],packet[2010];size_t written=0;
    for(auto *t:{&radioATable,&radioBTable})for(unsigned rfe=0;rfe<256;++rfe){
        RadioFirmwarePages pages;const auto status=prepareRadioFirmware(*t,uint8_t(rfe),0,storage,sizeof storage,pages);
        Sink selected;const auto r=applyRadioTable(*t,uint8_t(rfe),0,selected);
        if(r.status!=RadioTableStatus::ok){assert(status==r.status);continue;}
        assert(status==RadioTableStatus::ok);size_t n=0;
        for(auto &v:selected.trace)if(v.address>=0x100){assert(little32(storage+n*4)==((v.address<<20)|v.value));++n;}
        assert(n==pages.words&&n<=1500);
        for(unsigned page=0;page<pages.pages();++page){
            assert(pages.encode(page,1,packet,sizeof packet,written));
            assert(little32(packet)==(2u|(uint32_t(t->path?9:8)<<2)|(page<<8)|(1u<<24)));
            const size_t len=(n-page*500>500?500:n-page*500)*4;
            assert(written==len+8&&little32(packet+4)==written&&!std::memcmp(packet+8,storage+page*2000,len));
        }
        assert(!pages.encode(pages.pages(),1,packet,sizeof packet,written));
        assert(!pages.encode(0,1,storage,sizeof storage,written));
    }
    std::vector<RadioRegister> rows(1501,{0x10012,0xabcde});RadioTable large{rows.data(),rows.size(),RadioTableKind::radio,0};
    std::memset(storage,0xa5,sizeof storage);RadioFirmwarePages pages;
    assert(prepareRadioFirmware(large,0,0,storage,sizeof storage,pages)!=RadioTableStatus::ok&&pages.words==0);
    for(auto v:storage)assert(v==0xa5);
    large.count=1500;assert(prepareRadioFirmware(large,0,0,storage,sizeof storage,pages)==RadioTableStatus::ok&&pages.pages()==3);
    assert(pages.encode(2,4,packet,sizeof packet,written)&&written==2008&&(little32(packet+4)&0x4000));
    assert(!pages.encode(0,1,packet,2007,written));
    std::puts("RF firmware: both paths, all RFE packages, exact page bytes and overflow/overlap passed");
}
void initialization(){
    uint8_t scratch[6000];RadioFirmwarePages pages;
    RadioIo io;RadioInitialization<RadioIo> program(io);
    assert(program.baseband(bbTable,1,0).status==RadioTableStatus::ok);
    assert(program.nctl(1,0)==RadioIoStatus::ok);
    assert(program.radio(radioATable,1,0,scratch,sizeof scratch,pages).status==RadioTableStatus::ok&&pages.words);
    assert(program.radio(radioBTable,1,0,scratch,sizeof scratch,pages).status==RadioTableStatus::ok&&pages.path==1);
    for(size_t failure:{size_t(0),size_t(1),size_t(10),size_t(800)}){
        RadioIo broken;broken.failAt=failure;RadioInitialization<RadioIo> p(broken);
        auto result=p.radio(radioATable,1,0,scratch,sizeof scratch,pages);
        assert(result.status==RadioTableStatus::ioError&&!pages.data&&!pages.words&&broken.calls==failure+1);
    }
    RadioIo stuck;stuck.nctlStuck=true;RadioInitialization<RadioIo> n(stuck);
    assert(n.nctl(1,0)==RadioIoStatus::timeout&&stuck.delayed==1101);
    // The NCTL table starts only after the handshake succeeds.
    for(auto &event:stuck.trace)assert(event.address!=0x18004);
    RadioIo drain;drain.busyAfterWrite=true;RadioInitialization<RadioIo> d(drain);
    RadioRegister reg[]={{0x12,3}};RadioTable table{reg,1,RadioTableKind::radio,0};
    assert(d.radio(table,1,0,scratch,sizeof scratch,pages).status==RadioTableStatus::ioError&&!pages.data);
    RadioRegister bad[]={{0x10012,1},{0x20000,2}};RadioTable bt{bad,2,RadioTableKind::radio,0};
    RadioIo noWrites;RadioInitialization<RadioIo> clean(noWrites);
    assert(clean.radio(bt,0,0,scratch,sizeof scratch,pages).status!=RadioTableStatus::ok&&noWrites.trace.empty());
    PhyCalibration trim{};assert(clean.powerTrim(trim)==RadioIoStatus::ok&&noWrites.trace.empty());
    trim.thermalValid=true;trim.paBiasValid=true;trim.thermalTrim[0]=0x13;trim.thermalTrim[1]=0x04;
    trim.paBiasTrim[0]=0xab;trim.paBiasTrim[1]=0x12;
    assert(clean.powerTrim(trim)==RadioIoStatus::ok);
    std::vector<uint32_t> writes;for(auto &e:noWrites.trace)if(e.kind==1&&e.address==0x10370)writes.push_back(e.value);
    const std::vector<uint32_t> expected={0x84390000,0x94320000,0x8600b000,0x860a0000,0x96002000,0x96010000};
    assert(writes==expected);
    std::puts("Radio stages: real tables, NCTL handshake, trim bytes, interrupted writes and final drain passed");
}
void gainState(){
    std::vector<oracle::rtw89_reg2_def> regs;for(size_t i=0;i<gainTable.count;++i)regs.push_back({gainTable.registers[i].address,gainTable.registers[i].value});
    oracle::rtw89_phy_table table{regs.data(),regs.size(),oracle::pathA};
    for(unsigned rfe=0;rfe<256;++rfe){
        oracle::rtw89_dev dev{{uint8_t(rfe)},{0},false};BasebandGain actual;
        oracle::rtw89_phy_init_reg(&dev,&table,oracle::rtw89_phy_config_bb_gain_ax,nullptr);
        assert(loadBasebandGain(gainTable,uint8_t(rfe),0,actual).status==RadioTableStatus::ok&&!dev.error);
        assert(!std::memcmp(&actual,static_cast<BasebandGain*>(&dev.bb_gain.ax),sizeof actual));
    }
    // Differential all supported gain record shapes, including signed bytes.
    uint32_t rng=0x8852b;
    for(unsigned config=0;config<4;++config)for(unsigned type=0;type<64;++type)
        for(unsigned path=0;path<2;++path)for(unsigned band=0;band<8;++band){
            const uint32_t address=(config<<24)|(band<<16)|(path<<8)|type;
            if(!applyGainRecord(nullptr,0,address,0))continue;
            rng=rng*1664525+1013904223;
            oracle::rtw89_dev dev{{0},{0},false};BasebandGain actual{};oracle::rtw89_reg2_def reg{address,rng};
            oracle::rtw89_phy_config_bb_gain_ax(&dev,&reg,oracle::pathA,nullptr);
            assert(applyGainRecord(&actual,0,address,rng)&&!dev.error);
            assert(!std::memcmp(&actual,static_cast<BasebandGain*>(&dev.bb_gain.ax),sizeof actual));
        }
    BasebandGain valid;assert(loadBasebandGain(gainTable,0,0,valid).status==RadioTableStatus::ok);
    const BasebandGain before=valid;RadioRegister invalid[]={{0,0},{0x80000,0}};RadioTable bad{invalid,2,RadioTableKind::gain,0};
    assert(loadBasebandGain(bad,0,0,valid).status==RadioTableStatus::ioError&&!std::memcmp(&valid,&before,sizeof valid));
    assert(!applyGainRecord(&valid,0,0x200,0)&&!applyGainRecord(&valid,0,0xf9,0));
    std::puts("Gain state: all RFE tables and supported record forms match pinned upstream; invalid table leaves state intact");
}
int main(){differential();branchFailures();efuse();radioAccess();firmwarePages();initialization();gainState();}
