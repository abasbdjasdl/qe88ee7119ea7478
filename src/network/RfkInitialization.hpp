// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "Rtw8852bRfkConstants.hpp"
#include "EfuseCalibration.hpp"
namespace rtl8852be { namespace rfk {
enum class Error {none,precondition,io,timeout,clock,cancelled,calibration};
enum class Stage {idle,rck,dack,rxDc,complete,iqk,tssi,dpk,track,scan};
enum class Kind {rck,dack,rxDc,iqk,tssi,dpk,track,scan,channel};
enum class Space {none,mac,baseband,radio};
// Center channel and hardware bandwidth encoding (20/40/80 = 0/1/2).
// This checks chip geometry only; the controller must enforce regulatory rules.
struct Channel {u8 band{},width{},center{};};
inline bool sameChannel(Channel a,Channel b){return a.band==b.band&&a.width==b.width&&a.center==b.center;}
inline bool validChannel(Channel c){
    if(c.band==RTW89_BAND_2G)return c.width==0?(c.center>=1&&c.center<=14):(c.width==1&&c.center>=3&&c.center<=11);
    if(c.band!=RTW89_BAND_5G)return false;
    if(c.width==0)return (c.center>=36&&c.center<=64&&c.center%4==0)||
        (c.center>=100&&c.center<=144&&c.center%4==0)||(c.center>=149&&c.center<=177&&(c.center-149)%4==0);
    const u8 centers40[]={38,46,54,62,102,110,118,126,134,142,151,159,167,175};
    const u8 centers80[]={42,58,106,122,138,155,171};
    if(c.width==1){for(auto n:centers40)if(c.center==n)return true;}
    if(c.width==2){for(auto n:centers80)if(c.center==n)return true;}
    return false;
}
struct Result {
    Error error{Error::none};Stage stage{Stage::idle};Space space{Space::none};
    u32 address{},mask{},value{};u8 path{};unsigned operations{},polls{},messages{};
    bool rckReady{},dackReady{},rxDcReady{},iqReady{},tssiReady{},dpkReady{},requiresReset{},ownershipReleased{};
    Channel iqChannel{},scanHome{},scanChannel{};
    bool scanActive{},scanReady{};
};
// Backend owns validated I/O and the calibration lease. begin(kind) must quiesce
// DMA/TX and coordinate firmware/BT; end(kind,success) must keep TX stopped on
// failure. Neither is optional. The native adapter rejects absent callbacks.
// The controller must allocate this state off the kernel stack: TSSI retains
// per-channel measured coefficients. Controller/channel integration remains.
template<class Backend> class Initialization {
    struct rtw89_chan {rtw89_band band_type{};rtw89_bandwidth band_width{};u8 channel{};rtw89_subband subband_type{};};
    struct rtw89_hal {u8 cv{},antenna_rx{};};
    // Same numerical EWMA parameters as core.h DECLARE_EWMA(thermal,4,4):
    // four fractional bits, new sample weight 1/4. Owner serializes updates.
    struct ThermalAverage {u32 fixed{};void add(u8 value){if(value)fixed=fixed?(3*fixed+u32(value)*16)/4:u32(value)*16;}};
    static u8 ewma_thermal_read(const ThermalAverage *average){return u8(average->fixed/16);}
    struct Context {
        Backend &io;Result &result;rtw89_dack_info dack{};rtw89_dpk_info dpk{};
        rtw89_iqk_info iqk{};rtw89_chan channel{};bool restoreFailed[2]{},oneshotActive{};
        rtw89_tssi_info tssi{};rtw89_phy_efuse_gain efuse_gain{};bool calibrationConfigured{};
        // Pinned 8852B has fem_setup=NULL; unlike 8852A it sets no EPA flags.
        // Preserve that per-chip default: perform DPK, never invent a bypass.
        rtw89_fem_info fem{};struct {ThermalAverage avg_thermal[2];} phystat{};
        bool dbcc_en=false,is_tssi_mode[2]{},homeDpkReady{};rtw89_hal hal;
        uint64_t first{},previous{};unsigned operationBase{};bool started{};Space lastSpace{Space::none};
        u32 lastAddress{},lastMask{},lastValue{};u8 lastPath{};
        Context(Backend &i,Result &r,u8 cut):io(i),result(r),hal{cut,0}{}
    };
    static constexpr unsigned RTW89_DBG_RFK=0; // local diagnostic category
    static constexpr unsigned RTW89_DBG_TSSI=1;
    static constexpr unsigned RTW89_DBG_RFK_TRACK=2;
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
        if(t-d->first>2000000||d->result.operations-d->operationBase>=200000)return fail(d,Error::timeout);
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
    static void rtw89_phy_write32(Context *d,u32 a,u32 v){rtw89_phy_write32_mask(d,a,0xffffffff,v);}
    static void rtw89_phy_write32_set(Context *d,u32 a,u32 m){rtw89_phy_write32_mask(d,a,m,m>>shift(m));}
    static void rtw89_phy_write32_clr(Context *d,u32 a,u32 m){rtw89_phy_write32_mask(d,a,m,0);}
    static void rtw89_phy_write32_idx(Context *d,u32 a,u32 m,u32 v,rtw89_phy_idx p){
        if(p!=RTW89_PHY_0){fail(d,Error::precondition);return;}rtw89_phy_write32_mask(d,a,m,v);}
    static u32 rtw89_phy_read32_idx(Context *d,u32 a,u32 m,rtw89_phy_idx p){
        if(p!=RTW89_PHY_0){fail(d,Error::precondition);return 0;}return rtw89_phy_read32_mask(d,a,m);}
    static void rtw89_write32(Context *d,u32 a,u32 v){
        if(!op(d,Space::mac,a,0xffffffff,v))return;
        if(!d->io.writeMac(a,v))fail(d,Error::io);
    }
    static void delay(Context *d,unsigned us){if(!check(d))return;
        if(us>50000||!d->io.delayUs(us))fail(d,Error::io);}
    static void rtw89_debug(Context *d,unsigned,const char *,...){++d->result.messages;}
    static const rtw89_chan *rtw89_chan_get(Context *d,rtw89_sub_entity_idx entity){
        if(entity!=RTW89_SUB_ENTITY_0)fail(d,Error::precondition);return &d->channel;
    }
    static u8 rtw89_btc_phymap(Context *d,rtw89_phy_idx phy,rtw89_rf_path_bit paths){
        if(phy!=RTW89_PHY_0||paths!=RF_AB){fail(d,Error::precondition);return 0;}
        return u8((u32(paths)&BTC_RFK_PATH_MAP)|((bit(phy)<<shift(BTC_RFK_PHY_MAP))&BTC_RFK_PHY_MAP)|
            ((u32(d->channel.band_type)<<shift(BTC_RFK_BAND_MAP))&BTC_RFK_BAND_MAP));
    }
    static void rtw89_btc_ntfy_wl_rfk(Context *d,u8 phyMap,btc_wl_rfk_type type,btc_wl_rfk_state state){
        if(type!=BTC_WRFKT_IQK||(state!=BTC_WRFK_ONESHOT_START&&state!=BTC_WRFK_ONESHOT_STOP)){
            fail(d,Error::precondition);return;}
        const auto kind=d->result.stage==Stage::tssi?Kind::tssi:Kind::iqk;
        if(state==BTC_WRFK_ONESHOT_START){
            if(!check(d))return;if(d->oneshotActive){fail(d,Error::precondition);return;}
            if(!d->io.oneshot(kind,phyMap,true)){fail(d,Error::io);return;}d->oneshotActive=true;
        }else if(d->oneshotActive){
            // A fault may leave NCTL/KIP or RF partially programmed. Defer STOP
            // to native end(), which verifies recovery before releasing it.
            if(!check(d))return;
            if(!d->io.oneshot(kind,phyMap,false)){fail(d,Error::io);return;}d->oneshotActive=false;
        }
    }
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
        if(result.ownershipReleased)context_.oneshotActive=false;
        if(!result.ownershipReleased){fail(&context_,Error::io);result.requiresReset=true;}
        return ok&&result.ownershipReleased;
    }
public:
    Result result{};
private:
    Context context_;Channel rxDcChannel_{};bool preparedRxDc_{};
    void setChannel(Channel c){
        context_.channel={static_cast<rtw89_band>(c.band),static_cast<rtw89_bandwidth>(c.width),c.center,
            c.band==0?RTW89_CH_2G:c.center<=64?RTW89_CH_5G_BAND_1:c.center<=144?RTW89_CH_5G_BAND_3:RTW89_CH_5G_BAND_4};
    }
    void freshBudget(){context_.started=false;context_.operationBase=result.operations;}
public:
    Initialization(Backend &io,u8 cut):context_(io,result,cut){}
    Initialization(const Initialization &)=delete;Initialization &operator=(const Initialization &)=delete;
    const rtw89_dack_info &dack()const{return context_.dack;}
    const rtw89_iqk_info &iqk()const{return context_.iqk;}
    const rtw89_tssi_info &tssi()const{return context_.tssi;}
    const rtw89_dpk_info &dpk()const{return context_.dpk;}
    u8 averageThermal(u8 path)const{return path<2?ewma_thermal_read(&context_.phystat.avg_thermal[path]):0;}
    // Bases are the signed BB gain values captured before channel gain writes.
    // Inputs must come from this device's decoded eFuse/PHY map, not defaults.
    bool configureCalibration(const network::BoardCalibration &board,const network::PhyCalibration &phy,
                              s8 offsetBase,s8 rssiBase,u8 antennaRx){
        if(result.stage!=Stage::idle||context_.calibrationConfigured||!board.identityValid||antennaRx>3)return false;
        auto &g=context_.efuse_gain;g.offset_valid=board.gainOffsetValid;g.comp_valid=phy.gainCompValid;
        g.offset_base[0]=offsetBase;g.rssi_base[0]=rssiBase;context_.hal.antenna_rx=antennaRx;
        for(unsigned p=0;p<2;++p){context_.tssi.thermal[p]=board.thermal[p];
            for(unsigned i=0;i<6;++i)context_.tssi.tssi_cck[p][i]=board.tssiCck[p][i];
            for(unsigned i=0;i<19;++i)context_.tssi.tssi_mcs[p][i]=board.tssiMcs[p][i];
            for(unsigned i=0;i<8;++i)context_.tssi.tssi_trim[p][i]=phy.tssiTrim[p][i];
            for(unsigned i=0;i<5;++i){g.offset[p][i]=board.gainOffset[p][i];g.comp[p][i]=phy.gainComp[p][i];}}
        context_.calibrationConfigured=true;return true;
    }
    u8 dpdBackoff()const{return context_.dpk.dpk_gs[0];}
    // Each step consumes ONE pre-acquired calibration lease and returns to the
    // controller so it can service C2H and asynchronously restore/acquire the
    // next BT policy. Never wait for a workloop-delivered ACK inside begin/end.
    bool initializeRck(){
        if(result.stage!=Stage::idle||result.error!=Error::none)return false;
        freshBudget();if(!begin(Kind::rck,Stage::rck))return false;
        _set_dpd_backoff(&context_,RTW89_PHY_0);
        for(u8 path=0;path<2&&check(&context_);++path)_rck(&context_,path);
        result.rckReady=end(Kind::rck);return result.rckReady;
    }
    bool initializeDack(){
        if(result.stage!=Stage::rck||!result.rckReady||result.error!=Error::none)return false;
        freshBudget();if(!begin(Kind::dack,Stage::dack))return false;
        _dac_cal(&context_,false);
        result.dackReady=end(Kind::dack)&&context_.dack.dack_done;
        // Upstream records completion even after timeout; never expose that as valid.
        if(!result.dackReady){context_.dack.dack_done=false;return false;}
        return true;
    }
    bool initializeRxDc(){
        if(result.stage!=Stage::dack||!result.dackReady||result.error!=Error::none)return false;
        freshBudget();if(!begin(Kind::rxDc,Stage::rxDc))return false;
        _wait_rx_mode(&context_,RF_AB);_rx_dck(&context_,RTW89_PHY_0);
        result.rxDcReady=end(Kind::rxDc);if(!result.rxDcReady)return false;
        result.stage=Stage::complete;return true;
    }
    // Convenience for synchronous/test backends. Native asynchronous owners use
    // the individual steps above and the channel steps below across ACK events.
    bool initialize(){return initializeRck()&&initializeDack()&&initializeRxDc();}
    bool calibrateRxDc(Channel channel){
        if(result.stage!=Stage::complete||result.error!=Error::none||result.scanActive||!result.rxDcReady||!validChannel(channel))return false;
        result.iqReady=false;result.tssiReady=false;result.dpkReady=false;preparedRxDc_=false;
        setChannel(channel);
        freshBudget();
        // RXDCK must run on every newly programmed channel, not just at boot.
        result.rxDcReady=false;
        if(!begin(Kind::rxDc,Stage::rxDc))return false;
        _wait_rx_mode(&context_,RF_AB);_rx_dck(&context_,RTW89_PHY_0);
        result.rxDcReady=end(Kind::rxDc);if(!result.rxDcReady)return false;
        rxDcChannel_=channel;preparedRxDc_=true;result.stage=Stage::complete;return true;
    }
    bool calibrateIqOnly(Channel channel){
        if(result.stage!=Stage::complete||result.error!=Error::none||result.scanActive||
           !preparedRxDc_||!result.rxDcReady||!sameChannel(channel,rxDcChannel_))return false;
        // Consume once: a later IQK (including after scan) needs fresh RXDCK.
        preparedRxDc_=false;context_.restoreFailed[0]=context_.restoreFailed[1]=false;
        freshBudget();
        if(!begin(Kind::iqk,Stage::iqk))return false;
        _wait_rx_mode(&context_,RF_AB);_iqk_init(&context_);_iqk(&context_,RTW89_PHY_0,false);
        if(check(&context_))for(unsigned p=0;p<2;++p){const auto &q=context_.iqk;
            if(q.lok_fail[p]||q.lok_cor_fail[0][p]||q.lok_fin_fail[0][p]||q.iqk_tx_fail[0][p]||q.iqk_rx_fail[0][p]||context_.restoreFailed[p])
                fail(&context_,Error::calibration);
        }
        if(!end(Kind::iqk))return false;
        result.iqChannel=channel;result.iqReady=true;result.stage=Stage::complete;return true;
    }
    bool calibrateIq(Channel channel){return calibrateRxDc(channel)&&calibrateIqOnly(channel);}
    bool calibrateTssi(){
        if(result.stage!=Stage::complete||result.error!=Error::none||result.scanActive||!result.iqReady||!context_.calibrationConfigured)return false;
        result.tssiReady=false;result.dpkReady=false;context_.started=false;context_.operationBase=result.operations;
        if(!begin(Kind::tssi,Stage::tssi))return false;
        auto *d=&context_;
        rtw8852b_tssi(d,RTW89_PHY_0,true);
        if(!end(Kind::tssi))return false;
        result.tssiReady=true;result.stage=Stage::complete;return true;
    }
    bool calibrateDpk(){
        if(result.stage!=Stage::complete||result.error!=Error::none||result.scanActive||!result.iqReady||!result.tssiReady)return false;
        result.dpkReady=false;context_.started=false;context_.operationBase=result.operations;
        if(!begin(Kind::dpk,Stage::dpk))return false;
        auto *d=&context_;d->dpk.is_dpk_enable=true;d->dpk.is_dpk_reload_en=false;
        for(unsigned p=0;p<2;++p)d->dpk.bp[p][0].path_ok=false;
        _wait_rx_mode(d,RF_AB);_dpk(d,RTW89_PHY_0,false);
        if(check(d))for(unsigned p=0;p<2;++p){const auto &b=d->dpk.bp[p][d->dpk.cur_idx[p]];
            if(!b.path_ok||!b.ther_dpk||b.ch!=d->channel.channel||b.band!=d->channel.band_type||b.bw!=d->channel.band_width)
                fail(d,Error::calibration);
        }
        if(!end(Kind::dpk))return false;
        result.dpkReady=true;result.stage=Stage::complete;return true;
    }
    bool trackDpk(){
        if(result.stage!=Stage::complete||result.error!=Error::none||result.scanActive||!result.dpkReady)return false;
        result.dpkReady=false;context_.started=false;context_.operationBase=result.operations;
        if(!begin(Kind::track,Stage::track))return false;
        auto *d=&context_;
        // Reads real chip temperature; zero is absent data, as in phy.c.
        for(u8 p=0;p<2&&check(d);++p){const auto sample=rtw8852b_get_thermal(d,p);
            if(check(d))d->phystat.avg_thermal[p].add(sample);}
        _dpk_track(d);
        if(!end(Kind::track))return false;
        result.dpkReady=true;result.stage=Stage::complete;return true;
    }
    // Controller has tuned the IQ-calibrated home channel and must separately
    // manage scan BT notifications, CAM/MAC identity and regulatory TX limits.
    bool beginScan(){
        if(result.stage!=Stage::complete||result.error!=Error::none||result.scanActive||!result.iqReady||!context_.calibrationConfigured)return false;
        if(!result.tssiReady&&!calibrateTssi())return false;
        if(!context_.is_tssi_mode[0]||!context_.is_tssi_mode[1])return false;
        result.scanHome=result.iqChannel;result.scanChannel=result.iqChannel;
        context_.homeDpkReady=result.dpkReady;
        result.iqReady=false;result.tssiReady=false;result.dpkReady=false;result.scanReady=false;
        freshBudget();if(!begin(Kind::scan,Stage::scan))return false;
        // TSSI is now enabled on both paths, so source START does not generate
        // PMAC traffic under a scan lease. Cold setup used the full TSSI lease.
        rtw8852b_wifi_scan_notify(&context_,true,RTW89_PHY_0);
        if(!end(Kind::scan))return false;
        result.scanActive=true;result.scanReady=true;result.stage=Stage::complete;return true;
    }
    // Called after the controller programs this channel/bandwidth/power. The
    // scan coefficients are not evidence of full IQK/DPK on the visited channel.
    bool prepareScanChannel(Channel channel){
        if(result.stage!=Stage::complete||result.error!=Error::none||!result.scanActive||!validChannel(channel))return false;
        result.scanReady=false;setChannel(channel);freshBudget();
        if(!begin(Kind::scan,Stage::scan))return false;
        rtw8852b_tssi_scan(&context_,RTW89_PHY_0);
        if(!end(Kind::scan))return false;
        result.scanChannel=channel;result.scanReady=true;result.stage=Stage::complete;return true;
    }
    // Caller must first retune the saved home channel. Never accept a different
    // channel and expose its stale IQK/DPK coefficients as normal-TX readiness.
    bool finishScan(Channel restored){
        if(result.stage!=Stage::complete||result.error!=Error::none||!result.scanActive||!sameChannel(restored,result.scanHome))return false;
        result.scanReady=false;setChannel(restored);freshBudget();
        if(!begin(Kind::scan,Stage::scan))return false;
        rtw8852b_tssi_scan(&context_,RTW89_PHY_0);
        rtw8852b_wifi_scan_notify(&context_,false,RTW89_PHY_0);
        if(!end(Kind::scan))return false;
        result.scanActive=false;result.scanChannel=restored;result.iqChannel=restored;
        result.iqReady=true;result.tssiReady=true;result.dpkReady=context_.homeDpkReady;
        result.stage=Stage::complete;return true;
    }
};
} }
