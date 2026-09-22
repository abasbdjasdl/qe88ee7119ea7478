// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "Rtw8852bRfkConstants.hpp"
namespace rtl8852be { namespace rfk {
enum class Error {none,precondition,io,timeout,clock,cancelled,calibration};
enum class Stage {idle,rck,dack,rxDc,complete};
enum class Kind {rck,dack,rxDc};
enum class Space {none,mac,baseband,radio};
struct Result {
    Error error{Error::none};Stage stage{Stage::idle};Space space{Space::none};
    u32 address{},mask{},value{};u8 path{};unsigned operations{},polls{},messages{};
    bool rckReady{},dackReady{},rxDcReady{},requiresReset{},ownershipReleased{};
};
// Backend owns validated I/O and the calibration lease. begin(kind) must quiesce
// DMA/TX and coordinate firmware/BT; end(kind,success) must keep TX stopped on
// failure. Neither is optional. The native adapter rejects absent callbacks.
// This object performs initial RCK/DACK/RXDCK, not channel IQK/TSSI/DPK.
template<class Backend> class Initialization {
    struct rtw89_dpk_info {u8 dpk_gs[2]{};};
    struct Context {
        Backend &io;Result &result;rtw89_dack_info dack{};rtw89_dpk_info dpk{};
        bool dbcc_en=false,is_tssi_mode[2]{};struct {u8 cv;} hal;
        uint64_t first{},previous{};bool started{};Space lastSpace{Space::none};
        u32 lastAddress{},lastMask{},lastValue{};u8 lastPath{};
        Context(Backend &i,Result &r,u8 cut):io(i),result(r),hal{cut}{}
    };
    static constexpr unsigned RTW89_DBG_RFK=0; // local diagnostic category
    static bool fail(Context *d,Error e){
        auto &r=d->result;if(r.error==Error::none){r.error=e;r.space=d->lastSpace;r.address=d->lastAddress;
            r.mask=d->lastMask;r.value=d->lastValue;r.path=d->lastPath;r.requiresReset=r.operations!=0;}
        return false;
    }
    static bool check(Context *d){
        if(d->result.error!=Error::none)return false;
        if(d->io.cancelled())return fail(d,Error::cancelled);
        const auto t=d->io.nowUs();if(!d->started){d->first=d->previous=t;d->started=true;}
        if(t<d->previous)return fail(d,Error::clock);d->previous=t;
        if(t-d->first>2000000||d->result.operations>=200000)return fail(d,Error::timeout);
        return true;
    }
    static bool op(Context *d,Space s,u32 a,u32 m,u32 v=0,u8 path=0){
        if(!check(d))return false;++d->result.operations;
        d->lastSpace=s;d->lastAddress=a;d->lastMask=m;d->lastValue=v;d->lastPath=path;return true;
    }
    static u32 rtw89_read_rf(Context *d,u8 path,u32 a,u32 m){
        u32 v=0;if(!op(d,Space::radio,a,m,0,path))return 0;
        if(path>1||!m||(m&~0xfffffu)||!d->io.readRf(path,a,m,v))fail(d,Error::io);
        d->lastValue=v;return v;
    }
    static void rtw89_write_rf(Context *d,u8 path,u32 a,u32 m,u32 v){
        if(!op(d,Space::radio,a,m,v,path))return;
        if(path>1||!m||(m&~0xfffffu)||!d->io.writeRf(path,a,m,v))fail(d,Error::io);
    }
    static u32 rtw89_phy_read32_mask(Context *d,u32 a,u32 m){
        u32 v=0;if(!op(d,Space::baseband,a,m))return 0;
        if(!m||!d->io.readBb(a,v)||v==0xffffffff||v==0xdeadbeef){fail(d,Error::io);return 0;}
        d->lastValue=fieldGet(m,v);return d->lastValue;
    }
    static void rtw89_phy_write32_mask(Context *d,u32 a,u32 m,u32 v){
        if(!m){fail(d,Error::precondition);return;}
        const auto old=m==0xffffffff?0:rtw89_phy_read32_mask(d,a,0xffffffff);
        if(!op(d,Space::baseband,a,m,v))return;
        if(!d->io.writeBb(a,(old&~m)|((v<<shift(m))&m)))fail(d,Error::io);
    }
    static void rtw89_write32(Context *d,u32 a,u32 v){
        if(!op(d,Space::mac,a,0xffffffff,v))return;
        if(!d->io.writeMac(a,v))fail(d,Error::io);
    }
    static void delay(Context *d,unsigned us){if(!check(d))return;
        if(us>50000||!d->io.delayUs(us))fail(d,Error::io);}
    static void rtw89_debug(Context *d,unsigned,const char *,...){++d->result.messages;}
    static void rtw89_rfk_parser(Context *d,const rtw89_rfk_tbl *table){
        if(!table||!table->defs||table->size>4096){fail(d,Error::precondition);return;}
        for(u32 i=0;i<table->size&&check(d);++i){const auto &r=table->defs[i];
            switch(r.flag){
            case 0:rtw89_write_rf(d,r.path,r.addr,r.mask,r.data);break;
            case 1:rtw89_phy_write32_mask(d,r.addr,r.mask,r.data);break;
            case 2:rtw89_phy_write32_mask(d,r.addr,r.mask,r.mask>>shift(r.mask));break;
            case 3:rtw89_phy_write32_mask(d,r.addr,r.mask,0);break;
            case 4:delay(d,r.data);break;
            default:fail(d,Error::precondition);break;
            }
        }
    }
    static void rtw89_rfk_parser_by_cond(Context *d,bool condition,const rtw89_rfk_tbl *a,const rtw89_rfk_tbl *b){rtw89_rfk_parser(d,condition?a:b);}
    template<class Predicate> static int poll(Context *d,unsigned step,unsigned timeout,Predicate ready){
        const auto start=d->io.nowUs();
        for(unsigned i=0;i<=timeout/(step?step:1);++i){
            if(!check(d))return -1;
            if(d->previous<start){fail(d,Error::clock);return -1;}
            const bool done=ready();++d->result.polls;if(!check(d))return -1;if(done)return 0;
            if(d->previous-start>=timeout)break;delay(d,step);
        }
        fail(d,Error::timeout);return -1;
    }
#define R16_RFK_POLL(op,value,condition,step,timeout,sleep,device,...) \
    poll(device,step,timeout,[&](){value=op(device,__VA_ARGS__);return bool(condition);})
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "Rtw8852bRfkFunctions.inc"
#pragma clang diagnostic pop
#undef R16_RFK_POLL
    bool begin(Kind kind,Stage stage){
        result.stage=stage;if(!check(&context_))return false;result.ownershipReleased=false;
        if(!context_.io.begin(kind)){fail(&context_,Error::precondition);result.requiresReset=true;return false;}return true;
    }
    bool end(Kind kind){
        bool ok=check(&context_);
        if(ok&&!context_.io.drain()){fail(&context_,Error::io);ok=false;}
        // Cleanup/notification is attempted even when normal I/O is latched off.
        result.ownershipReleased=context_.io.end(kind,ok);
        if(!result.ownershipReleased){fail(&context_,Error::io);result.requiresReset=true;}
        return ok&&result.ownershipReleased;
    }
public:
    Result result{};
private:
    Context context_;
public:
    Initialization(Backend &io,u8 cut):context_(io,result,cut){}
    Initialization(const Initialization &)=delete;Initialization &operator=(const Initialization &)=delete;
    const rtw89_dack_info &dack()const{return context_.dack;}
    u8 dpdBackoff()const{return context_.dpk.dpk_gs[0];}
    bool initialize(){
        if(result.stage!=Stage::idle||!begin(Kind::rck,Stage::rck))return false;
        _set_dpd_backoff(&context_,RTW89_PHY_0);
        for(u8 path=0;path<2&&check(&context_);++path)_rck(&context_,path);
        result.rckReady=end(Kind::rck);if(!result.rckReady)return false;
        if(!begin(Kind::dack,Stage::dack))return false;
        _dac_cal(&context_,false);
        result.dackReady=end(Kind::dack)&&context_.dack.dack_done;
        // Upstream records completion even after timeout; never expose that as valid.
        if(!result.dackReady){context_.dack.dack_done=false;return false;}
        if(!begin(Kind::rxDc,Stage::rxDc))return false;
        _wait_rx_mode(&context_,RF_AB);_rx_dck(&context_,RTW89_PHY_0);
        result.rxDcReady=end(Kind::rxDc);if(!result.rxDcReady)return false;
        result.stage=Stage::complete;return true;
    }
};
} }
