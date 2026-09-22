// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation (upstream BSD option)
// RTL8852B PTA/RF and initial firmware coexistence setup, adapted from rtw89
// d1fced1b8a741dc9f92b47c69489c24385945f6e, rtw8852b.c/mac.c/coex.c/fw.c.
#pragma once
#include "BtRfkCoordination.hpp"

namespace rtl8852be { namespace bt { namespace initialization {
struct Board {
    uint32_t firmwareVersion{};uint8_t rfe{},cut{};bool identityValid{},wlanOnly{};
    bool valid()const{return identityValid&&rfe!=0xff&&cut<6&&Protocol::select(firmwareVersion).valid();}
    bool dedicated()const{return rfe>0&&!(rfe&1);}
    uint8_t antennas()const{return dedicated()?3:2;}
    uint8_t monitorVersion()const{return firmwareVersion>=0x001d1d00?2:1;}
};
struct Slot {uint16_t duration;uint32_t table;uint16_t type;};
// coex.c s_def[], in core.h CXST_* order. SLOT_MIX=0, SLOT_ISO=1.
constexpr Slot slots[18]={
    {100,0x55555555,0},{5,0xea5a5a5a,1},{70,0xea5a5a5a,1},{15,0xea5a5a5a,1},
    {15,0xea5a5a5a,1},{250,0xe5555555,0},{7,0xea5a5a5a,0},{5,0xe5555555,0},
    {50,0xe5555555,0},{20,0xea5a5a5a,1},{500,0x55555555,0},{0,0xea5a5a5a,0},
    {0,0xffffffff,1},{0,0xe5555555,0},{0,0xaaaaaaaa,1},{250,0xea5a5a5a,0},
    {50,0xffffffff,1},{50,0xffffdfff,1}};
constexpr uint32_t monitorAddresses[16]={0xda24,0xda28,0xda2c,0xda30,0xda4c,0xda10,
    0xda20,0xda34,0xcef4,0x8424,0xd200,0xd220,0x980,0x4738,0x4688,0x4694};
constexpr network::CommandId monitorCommand{2,0x10,2},slotsCommand{2,0x10,1},driverCommand{2,0x10,5};
constexpr uint32_t initialScoreboard=(1U<<0)|(1U<<1)|(1U<<14);
inline void encodeSlots(uint8_t (&out)[146]){
    out[0]=1;out[1]=18;
    for(unsigned i=0;i<18;++i){auto *p=out+2+i*8;
        network::store16(p,slots[i].duration);network::store32(p+2,slots[i].table);
        network::store16(p+6,slots[i].type);}
}
inline bool encodeMonitor(const Board &board,uint8_t (&out)[130]){
    if(!board.valid())return false;out[0]=board.monitorVersion();out[1]=16;
    for(unsigned i=0;i<16;++i){auto *p=out+2+i*8;
        network::store16(p,uint16_t(i<12?0:1));network::store16(p+2,4);
        network::store32(p+4,monitorAddresses[i]);}return true;
}
inline bool encodeInit(const Board &board,uint8_t (&out)[14]){
    if(!board.valid())return false;for(auto &v:out)v=0;
    out[0]=0;out[1]=12; // CXDRVINFO_INIT, packed payload excluding cxhdr.
    out[2]=board.dedicated()?1:0;out[3]=board.antennas();out[4]=10;
    // ant_info, bt_solo, diversity, kt_ver_adie, WA and unused fields start at
    // zero in _reset_btc_var and are not assigned by 8852B's btc_set_rfe.
    out[6]=board.rfe;out[7]=board.cut;out[8]=board.dedicated()?0:2;
    out[10]=6;out[11]=uint8_t(2|(board.wlanOnly?1:0)); // WL_INITOK + source BTC_MODE_WL, non-DBCC.
    return true;
}
inline void encodeControl(uint8_t (&out)[6]){
    // fcxctrl=1: ignore BT during initialization; trace_step is NOT on wire.
    out[0]=6;out[1]=4;out[2]=2;out[3]=out[4]=out[5]=0;
}
inline PolicySnapshot initialPolicy(){
    // _action_wl_init -> BTC_CXP_OFF_BT: TDMA OFF + cxtbl[2], MIX slot.
    PolicySnapshot p{};p.offDuration=100;p.offTable=0xe5555555;p.valid=true;return p;
}
enum class Error {none,invalid,ownership,cancelled,clock,timeout,io,readback,firmware,
    txActive,btBusy,lteTimeout,command,sequenceReuse,rejected};
enum class Stage {idle,hardware,monitor,slots,driverInit,driverControl,policy,complete,fault};
struct Result {
    Error error{Error::none};Stage stage{Stage::idle};uint32_t address{},value{};
    unsigned operations{},polls{},commands{},acknowledged{};
    bool modified{},requiresRecovery{},hardwareProgrammed{},policyAcknowledged{},scoreboardWritten{};
};

// Io adds readRf/writeRf(path,address,mask,value) to the BT RFK I/O contract.
// Native RF operations must use real RadioAccess with RF clocks established.
// H2C link is the shared non-reusing FirmwareCommands queue; source packets are
// copied before submit returns. C2H delivery is asynchronous, never inline.
// A fresh firmware/device epoch, MAC+RF tables and an acknowledged firmware TX
// pause are preconditions. Caller holds awake/exclusive radio and coex leases.
template<class Io> class Initialization {
    Io &io_;Scoreboard &outShadow_;PolicySnapshot &outPolicy_;Board board_{};
    uint64_t start_{},last_{},commandAt_{};uint32_t used_[8]{};
    bool started_{},busy_{},pending_{},commandAttempted_{},ownsOutputs_{};uint8_t sequence_{};
    network::CommandId pendingId_{};
    bool fail(Error e,uint32_t address=0,uint32_t value=0){
        if(result.error==Error::none){result.error=e;result.address=address;result.value=value;}
        result.stage=Stage::fault;result.requiresRecovery=result.modified||commandAttempted_;
        pending_=false;if(ownsOutputs_){outShadow_.valid=false;outPolicy_.valid=false;}
        if(commandAttempted_)io_.invalidateFirmwareEpoch();return false;
    }
    bool check(){
        if(result.error!=Error::none)return false;
        if(!io_.inGate())return fail(Error::ownership);
        if(io_.cancelled())return fail(Error::cancelled);
        const auto now=io_.nowUs();if(started_&&now<last_)return fail(Error::clock);
        if(!started_){start_=now;started_=true;}last_=now;
        if(result.stage!=Stage::complete&&(last_-start_>=2000000||result.operations>=100000))return fail(Error::timeout);
        if(pending_&&last_-commandAt_>=300000)return fail(Error::timeout);
        return true;
    }
    bool enter(){if(busy_)return fail(Error::ownership);if(!check())return false;busy_=true;return true;}
    struct Guard {bool &busy;~Guard(){busy=false;}};
    bool r8(uint32_t a,uint8_t &v){v=0;if(!check())return false;++result.operations;
        return (io_.read8(a,v)||fail(Error::io,a))&&check();}
    bool r16(uint32_t a,uint16_t &v){v=0;if(!check())return false;++result.operations;
        return (io_.read16(a,v)||fail(Error::io,a))&&check();}
    bool r32(uint32_t a,uint32_t &v){v=0;if(!check())return false;++result.operations;
        return (io_.read32(a,v)||fail(Error::io,a))&&check();}
    bool w8(uint32_t a,uint8_t v){if(!check())return false;++result.operations;result.modified=true;
        return (io_.write8(a,v)||fail(Error::io,a,v))&&check();}
    bool w16(uint32_t a,uint16_t v){if(!check())return false;++result.operations;result.modified=true;
        return (io_.write16(a,v)||fail(Error::io,a,v))&&check();}
    bool w32(uint32_t a,uint32_t v){if(!check())return false;++result.operations;result.modified=true;
        return (io_.write32(a,v)||fail(Error::io,a,v))&&check();}
    bool delay(unsigned us){return check()&&(io_.delayUs(us)||fail(Error::io))&&check();}
    bool m8(uint32_t a,uint8_t clear,uint8_t set,uint8_t verify=0xff){
        uint8_t v=0,got=0;if(!r8(a,v))return false;v=uint8_t((v&~clear)|set);
        if(!w8(a,v)||!r8(a,got))return false;
        return !((got^v)&verify)||fail(Error::readback,a,got);
    }
    bool m16(uint32_t a,uint16_t clear,uint16_t set){
        uint16_t v=0,got=0;if(!r16(a,v))return false;v=uint16_t((v&~clear)|set);
        if(!w16(a,v)||!r16(a,got))return false;return v==got||fail(Error::readback,a,got);
    }
    bool m32(uint32_t a,uint32_t clear,uint32_t set,uint32_t verify=0xffffffff){
        uint32_t v=0,got=0;if(!r32(a,v))return false;v=(v&~clear)|set;
        if(!w32(a,v)||!r32(a,got))return false;return !((got^v)&verify)||fail(Error::readback,a,got);
    }
    bool txStopped(){uint16_t tx=0;if(!r16(schedulerRegister,tx))return false;
        return !tx||fail(Error::txActive,schedulerRegister,tx);}
    bool lteReady(){
        const auto t=last_;
        for(unsigned i=0;i<=1000;++i){uint8_t v=0;if(!r8(lteControl+3,v))return false;++result.polls;
            if(last_-t>50000)break;if(v&0x20)return true;
            if(last_-t>=50000||i==1000)break;if(!delay(50))return false;}
        return fail(Error::lteTimeout,lteControl);
    }
    bool readLte(uint32_t a,uint32_t &v){return lteReady()&&w32(lteControl,0x800f0000|a)&&lteReady()&&r32(lteReadData,v);}
    bool writeLte(uint32_t a,uint32_t v){
        if(!lteReady()||!w32(lteWriteData,v)||!w32(lteControl,0xc00f0000|a))return false;
        uint32_t got=0;if(!readLte(a,got))return false;return got==v||fail(Error::readback,a,got);
    }
    bool wrf(uint8_t path,uint32_t a,uint32_t v,bool verify){
        if(!check())return false;++result.operations;result.modified=true;
        if(!io_.writeRf(path,a,0xfffff,v))return fail(Error::io,a,v);
        if(!check())return false;
        if(verify){uint32_t got=0;++result.operations;
            if(!io_.readRf(path,a,0xfffff,got))return fail(Error::io,a);
            if(!check())return false;if(got!=v)return fail(Error::readback,a,got);}
        return true;
    }
    bool trxMask(uint8_t path,uint8_t group,uint32_t value){
        // RF table data port semantics are not invented as ordinary MMIO
        // readback. Verify WE enable/disable; actual LUT data goes through the
        // hardware RF write-completion protocol supplied by the native adapter.
        return wrf(path,0xef,0x20000,true)&&wrf(path,0x33,group,false)&&
            wrf(path,0x3f,value,false)&&wrf(path,0xef,0,true);
    }
    bool hardware(){
        uint32_t lte=0;
        if(!m8(0x40,0,0x20)||!m8(0xda20,0,2)||!m8(0xda35,0,1)||
           !m8(0xda40,0,0x0c)||!m8(0xda42,0,1,0xfe)|| // reset bit can self-clear
           !m8(0xcc07,2,0)||!m16(0xc340,0x200,0x20)||
           !readLte(0x3c,lte)||!writeLte(0x3c,lte&0x100)||
           !m8(0x40,0xc0,0)||!m8(0xda4c,0,1)||!m8(0xda6c,0x3f,5)||
           !m8(0x41,4,2)||!m32(0xda30,0,8)||!m32(0xda10,0,0x100)||
           !wrf(0,2,0,true)||!wrf(1,2,0,true))return false;
        const uint32_t ss=board_.dedicated()?0x5df:0x5ff;
        if(!trxMask(0,0,ss)||!trxMask(1,0,ss)||!trxMask(0,2,0x5ff)||
           !trxMask(1,2,board_.dedicated()?0x5ff:0x55f)||
           !m32(0xda2c,0xffffffff,0xf0ffffff)||
           !m32(0xda40,0,0x10004,0xfffeffff)||
           !m32(0xd200,0x3ff,0)||!m32(0xd220,0xffa,0))return false;
        result.hardwareProgrammed=true;return txStopped();
    }
    bool btState(uint32_t &bt){
        if(!r32(scoreboardRegister,bt))return false;
        return (bt!=0xffffffff&&bt!=0xdeadbeef)||fail(Error::io,scoreboardRegister,bt);
    }
    bool winInit(){
        uint32_t bt=0,grant=0;if(!btState(bt))return false;
        // Do not override a BT calibration that is already running/requested.
        if(bt&(btRfkRun|btRfkRequest))return fail(Error::btBusy,scoreboardRegister,bt);
        if(!readLte(grantRegister,grant))return false;
        const uint32_t desired=(grant&~grantMask)|((!board_.wlanOnly&&(bt&2))?0xdd00dd00:calibrationGrants);
        // WINIT: enabled BT owns high grants. Explicit BTC_MODE_WL instead
        // selects source ANT_WONLY: WL high, BT low, PLT_NONE, same OFF_BT policy.
        return writeLte(grantRegister,desired)&&m8(controlPathRegister,0,4)&&
            m16(priorityRegister,0xffff,board_.wlanOnly?0x100:0x166); // WONLY: PLT_NONE; WINIT: PLT_BT
    }
    bool submit(Stage next,network::CommandId id,const uint8_t *bytes,size_t length){
        commandAttempted_=true;uint8_t seq=0;
        if(!io_.submitH2c(id,true,bytes,length,seq))return fail(Error::command);
        if(!check())return false;
        const uint32_t bit=1U<<(seq&31);if(used_[seq>>5]&bit)return fail(Error::sequenceReuse);
        used_[seq>>5]|=bit;sequence_=seq;pendingId_=id;pending_=true;commandAt_=last_;
        result.stage=next;++result.commands;return true;
    }
    bool next(){
        switch(result.stage){
        case Stage::hardware:{uint8_t bytes[130]{};if(!encodeMonitor(board_,bytes))return fail(Error::invalid);
            return submit(Stage::monitor,monitorCommand,bytes,sizeof(bytes));}
        case Stage::monitor:{uint8_t bytes[146]{};encodeSlots(bytes);return submit(Stage::slots,slotsCommand,bytes,sizeof(bytes));}
        case Stage::slots:{uint8_t bytes[14]{};if(!encodeInit(board_,bytes))return fail(Error::invalid);
            return submit(Stage::driverInit,driverCommand,bytes,sizeof(bytes));}
        case Stage::driverInit:{uint8_t bytes[6]{};encodeControl(bytes);return submit(Stage::driverControl,driverCommand,bytes,sizeof(bytes));}
        case Stage::driverControl:{if(!winInit())return false;uint8_t bytes[26]{};
            if(!encodePolicy(Protocol::select(board_.firmwareVersion),initialPolicy(),bytes))return fail(Error::invalid);
            return submit(Stage::policy,policyCommand,bytes,sizeof(bytes));}
        case Stage::policy:{
            result.policyAcknowledged=true;uint32_t bt=0;if(!txStopped()||!btState(bt))return false;
            // W2B starts from _reset_btc_var zero shadow, not the B2W read value.
            const uint32_t word=0x81000000|(bt&0x7f000000)|initialScoreboard;
            if(!w32(scoreboardRegister,word)||!delay(1000)||!txStopped())return false;
            result.scoreboardWritten=true;outShadow_={initialScoreboard,true};outPolicy_=initialPolicy();
            result.stage=Stage::complete;return true;}
        default:return fail(Error::ownership);
        }
    }
public:
    Result result{};
    Initialization(Io &io,Scoreboard &shadow,PolicySnapshot &policy):io_(io),outShadow_(shadow),outPolicy_(policy){}
    Initialization(const Initialization &)=delete;Initialization &operator=(const Initialization &)=delete;
    bool begin(Board board){
        // Successful outputs may already belong to the RFK/controller owner.
        // Reject an accidental second invocation without poisoning that owner,
        // performing I/O, or revisiting this init adapter's expired deadline.
        // In-flight/reentrant invocations still go through the fault path.
        if(result.stage==Stage::complete)return false;
        if(!enter())return false;Guard guard{busy_};
        if(result.stage!=Stage::idle||!board.valid()||outShadow_.valid||outPolicy_.valid)return fail(Error::invalid);
        ownsOutputs_=true;board_=board;uint32_t fw=0,mac=0,cmac1=0,bt=0;
        if(!r32(firmwareControl,fw)||!r32(cmacFunctionRegister,mac)||!r32(0x80,cmac1)||!txStopped()||!btState(bt))return false;
        if(fw==0xffffffff||fw==0xdeadbeef||(fw&0xe0)!=0xe0||mac==0xffffffff||mac==0xdeadbeef||
           !(mac&0x40000000)||cmac1==0xffffffff||(cmac1&0x40000000))return fail(Error::firmware);
        if(bt&(btRfkRun|btRfkRequest))return fail(Error::btBusy,scoreboardRegister,bt);
        result.stage=Stage::hardware;return hardware()&&next();
    }
    bool service(){if(!enter())return false;Guard guard{busy_};return txStopped();}
    bool ready(){
        if(result.stage!=Stage::complete)return false;
        if(!enter())return false;Guard guard{busy_};return outShadow_.valid&&outPolicy_.valid&&txStopped();
    }
    EventResult acceptEvent(const network::FirmwareEvent &event){
        network::FirmwareAck ack{};
        if(!network::decodeAck(event,ack)||!ack.done||!pending_||ack.sequence!=sequence_||
           !network::sameCommand(ack.command,pendingId_))return EventResult::unrelated;
        if(!enter())return EventResult::fault;Guard guard{busy_};
        pending_=false;if(ack.returnCode){fail(Error::rejected,0,ack.returnCode);return EventResult::fault;}
        if(!txStopped())return EventResult::fault;++result.acknowledged;
        return next()?EventResult::consumed:EventResult::fault;
    }
};
} } }
