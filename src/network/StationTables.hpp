// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation (upstream BSD option).
// AX CMAC/CAM layout and algorithms adapted from pinned rtw89 fw.c/cam.c/mac.c.
#pragma once
#include "FirmwareCommands.hpp"
#include "StationController.hpp"
namespace rtl8852be { namespace station { namespace tables { namespace wire {
inline void replace(uint8_t *bytes,unsigned word,uint32_t value,uint32_t mask){
    unsigned shift=0;while(!(mask&(uint32_t(1)<<shift)))++shift;
    network::store32(bytes+word*4,(network::little32(bytes+word*4)&~mask)|((value<<shift)&mask));
}
} } } }
#include "Rtw8852bStationTableFields.hpp"
namespace rtl8852be { namespace station { namespace tables {
constexpr network::CommandId cmacCommand{1,5,2},camCommand{1,6,0};
struct Payload {network::CommandId id{};size_t length{};uint8_t bytes[68]{};};
struct DefaultCmac {uint8_t macid{},antennaTx{};};
struct HeCapabilities {
    bool present{};uint8_t rxNss{},txNss{},phy[11]{},ppe[25]{};size_t ppeLength{};
};
struct AssociationCmac {uint8_t macid{},port{},band{};bool stationPresent{};HeCapabilities he{};};
struct Cam {
    uint8_t macid{},port{},addressIndex{},bssidIndex{};Address local{},bssid{};
    bool valid{true},connected{},nontransmitted{},trigger{},lsigTxop{};
    uint16_t aid{};uint8_t bssColor{},beaconHit{},hitRule{},addressMask{},maskSelect{},target{},frameTarget{};
};
inline bool defaultCmac(const DefaultCmac &config,Payload &out){
    out={};if(config.macid>=128||config.antennaTx>3)return false;
    out.id=cmacCommand;out.length=68;auto *p=out.bytes;
    wire::SET_CTRL_INFO_MACID(p,config.macid);wire::SET_CTRL_INFO_OPERATION(p,1);
    wire::SET_CMC_TBL_TXPWR_MODE(p,0);
    wire::SET_CMC_TBL_NTX_PATH_EN(p,config.antennaTx?config.antennaTx:2);
    wire::SET_CMC_TBL_PATH_MAP_A(p,0);wire::SET_CMC_TBL_PATH_MAP_B(p,config.antennaTx==3?1:0);
    wire::SET_CMC_TBL_PATH_MAP_C(p,0);wire::SET_CMC_TBL_PATH_MAP_D(p,0);
    wire::SET_CMC_TBL_ANTSEL_A(p,0);wire::SET_CMC_TBL_ANTSEL_B(p,0);
    wire::SET_CMC_TBL_ANTSEL_C(p,0);wire::SET_CMC_TBL_ANTSEL_D(p,0);
    wire::SET_CMC_TBL_DOPPLER_CTRL(p,0);wire::SET_CMC_TBL_TXPWR_TOLERENCE(p,0);
    return true;
}
// Equivalent to __get_sta_he_pkt_padding, but bounds checked and byte-safe.
// The selected stream is min(peer RX NSS, device TX NSS)-1, NOT the last NSS
// advertised in the PPE header. All four RU/BW padding slots are encoded for AX.
inline bool packetPadding(const HeCapabilities &he,uint8_t (&pads)[4]){
    for(auto &p:pads)p=0;
    if(!he.present)return true;
    if(!he.rxNss||he.rxNss>8||!he.txNss||he.txNss>2||he.ppeLength>25)return false;
    if(!(he.phy[6]&0x80)){for(auto &p:pads)p=uint8_t((he.phy[9]>>6)&3);return true;}
    if(!he.ppeLength)return false;
    const unsigned bitmap=(he.ppe[0]>>3)&15,streams=(he.ppe[0]&7)+1;
    const unsigned selected=(he.rxNss<he.txNss?he.rxNss:he.txNss)-1;
    unsigned count=0;for(unsigned i=0;i<4;++i)count+=(bitmap>>i)&1;
    if(!count||selected>=streams||he.ppeLength<(7+streams*count*6+7)/8)return false;
    unsigned bit=7+selected*count*6;
    for(unsigned i=0;i<4;++i){
        if(!(bitmap&(1U<<i))){pads[i]=1;continue;}
        uint8_t pair=0;for(unsigned j=0;j<6;++j)pair|=((he.ppe[(bit+j)/8]>>((bit+j)%8))&1U)<<j;
        bit+=6;const unsigned ppe16=pair&7,ppe8=(pair>>3)&7;
        pads[i]=ppe16!=7&&ppe8==7?2:(ppe8!=7?1:0);
    }
    return true;
}
inline bool associationCmac(const AssociationCmac &config,Payload &out){
    out={};if(config.macid>=128||config.port>4||config.band>1||(!config.stationPresent&&config.he.present))return false;
    uint8_t pads[4]{};if(!packetPadding(config.he,pads))return false;
    out.id=cmacCommand;out.length=68;auto *p=out.bytes;
    wire::SET_CTRL_INFO_MACID(p,config.macid);wire::SET_CTRL_INFO_OPERATION(p,1);
    wire::SET_CMC_TBL_DISRTSFB(p,1);wire::SET_CMC_TBL_DISDATAFB(p,1);
    wire::SET_CMC_TBL_RTS_RTY_LOWEST_RATE(p,config.band?4:0);
    wire::SET_CMC_TBL_RTS_TXCNT_LMT_SEL(p,0);wire::SET_CMC_TBL_DATA_TXCNT_LMT_SEL(p,0);
    wire::SET_CMC_TBL_ULDL(p,1);wire::SET_CMC_TBL_MULTI_PORT_ID(p,config.port);
    wire::SET_CMC_TBL_NOMINAL_PKT_PADDING(p,pads[0]);wire::SET_CMC_TBL_NOMINAL_PKT_PADDING40(p,pads[1]);
    wire::SET_CMC_TBL_NOMINAL_PKT_PADDING80(p,pads[2]);wire::SET_CMC_TBL_NOMINAL_PKT_PADDING160(p,pads[3]);
    if(config.stationPresent)wire::SET_CMC_TBL_BSR_QUEUE_SIZE_FORMAT(p,config.he.present);
    return true;
}
inline uint8_t addressHash(Address address,unsigned start){uint8_t hash=0;for(unsigned i=start;i<6;++i)hash^=address.bytes[i];return hash;}
// Full 60-byte address+BSSID CAM update, including invalidation. Software crypto
// is deliberate: NORMAL security mode, zero security-valid bitmap/key slots.
// Hardware key CAM is not secretly enabled by association or this command.
inline bool addressCam(const Cam &config,Payload &out){
    out={};if(config.macid>=128||config.port>4||config.addressIndex>=128||config.bssidIndex>=10||
        !unicast(config.local)||config.bssColor>63||config.beaconHit>3||config.hitRule>3||
        config.addressMask>63||config.maskSelect>2||config.target>7||config.frameTarget>7||
        (config.connected&&(!unicast(config.bssid)||!config.aid||config.aid>2007))||
        (!config.connected&&config.aid))return false;
    out.id=camCommand;out.length=60;auto *p=out.bytes;
    unsigned start=0;if(config.addressMask)while(!(config.addressMask&(1U<<start)))++start;
    wire::FWCMD_SET_ADDR_IDX(p,config.addressIndex);wire::FWCMD_SET_ADDR_OFFSET(p,0);
    wire::FWCMD_SET_ADDR_LEN(p,wire::ADDR_CAM_ENT_SIZE);wire::FWCMD_SET_ADDR_VALID(p,config.valid);
    wire::FWCMD_SET_ADDR_NET_TYPE(p,config.connected?2:0);wire::FWCMD_SET_ADDR_BCN_HIT_COND(p,config.beaconHit);
    wire::FWCMD_SET_ADDR_HIT_RULE(p,config.hitRule);wire::FWCMD_SET_ADDR_BB_SEL(p,0);
    wire::FWCMD_SET_ADDR_ADDR_MASK(p,config.addressMask);wire::FWCMD_SET_ADDR_MASK_SEL(p,config.maskSelect);
    wire::FWCMD_SET_ADDR_SMA_HASH(p,addressHash(config.local,config.maskSelect==1?start:0));
    wire::FWCMD_SET_ADDR_TMA_HASH(p,addressHash(config.bssid,config.maskSelect==2?start:0));
    wire::FWCMD_SET_ADDR_BSSID_CAM_IDX(p,config.bssidIndex);
    // These fields are contiguous even across words; copy bytewise to avoid
    // alignment/endian assumptions. Offsets are checked against imported setters.
    for(unsigned i=0;i<6;++i){p[16+i]=config.local.bytes[i];p[22+i]=config.bssid.bytes[i];}
    wire::FWCMD_SET_ADDR_PORT_INT(p,config.port);wire::FWCMD_SET_ADDR_TSF_SYNC(p,config.port);
    wire::FWCMD_SET_ADDR_TF_TRS(p,config.trigger);wire::FWCMD_SET_ADDR_LSIG_TXOP(p,config.lsigTxop);
    wire::FWCMD_SET_ADDR_TGT_IND(p,config.target);wire::FWCMD_SET_ADDR_FRM_TGT_IND(p,config.frameTarget);
    wire::FWCMD_SET_ADDR_MACID(p,config.macid);wire::FWCMD_SET_ADDR_AID12(p,config.connected?config.aid:0);
    wire::FWCMD_SET_ADDR_SEC_ENT_MODE(p,2);
    wire::FWCMD_SET_ADDR_BSSID_IDX(p,config.bssidIndex);wire::FWCMD_SET_ADDR_BSSID_OFFSET(p,0);
    wire::FWCMD_SET_ADDR_BSSID_LEN(p,wire::BSSID_CAM_ENT_SIZE);wire::FWCMD_SET_ADDR_BSSID_VALID(p,config.valid);
    wire::FWCMD_SET_ADDR_BSSID_MASK(p,config.nontransmitted?0x1f:0x3f);
    wire::FWCMD_SET_ADDR_BSSID_BB_SEL(p,0);wire::FWCMD_SET_ADDR_BSSID_BSS_COLOR(p,config.bssColor);
    for(unsigned i=0;i<6;++i)p[54+i]=config.bssid.bytes[i];
    return true;
}
struct Plan {Payload commands[2]{};unsigned count{};};
inline bool idlePlan(const Cam &cam,const DefaultCmac &cmac,Plan &out){
    out={};if(!cam.valid||cam.connected||cam.macid!=cmac.macid)return false;
    if(!addressCam(cam,out.commands[0])||!defaultCmac(cmac,out.commands[1])){out={};return false;}
    out.count=2;return true;
}
inline bool cmacPlan(const AssociationCmac &cmac,Plan &out){out={};if(!associationCmac(cmac,out.commands[0]))return false;out.count=1;return true;}
inline bool camPlan(const Cam &cam,Plan &out){out={};if(!addressCam(cam,out.commands[0]))return false;out.count=1;return true;}
enum class ProgramState {idle,pending,complete,failed};
enum class ProgramError {none,bus,firmware,completion,cancelled};
struct Completion {void *owner{};bool (*finished)(void *,Token,bool success){};};
// Uses the actual shared FirmwareCommands allocator/ACK router. No local wire
// sequence allocator and no independent raw-C2H success path. The caller binds
// the SAME bus instance used by role/join/BT and native CH12 transport.
template<class Transport> class Programmer {
    network::FirmwareCommands<Transport> &bus_;Plan plan_{};unsigned index_{};
    Token token_{};Completion completion_{};uint8_t sequence_{};bool notifying_{};
    ProgramState state_{ProgramState::idle};ProgramError error_{ProgramError::none};
    bool notify(bool success){
        notifying_=true;const bool ok=completion_.finished(completion_.owner,token_,success);notifying_=false;
        if(!ok){error_=ProgramError::completion;state_=ProgramState::failed;bus_.invalidate();}
        return ok;
    }
    bool fail(ProgramError e,bool deliver){
        if(state_==ProgramState::failed)return false;
        error_=e;state_=ProgramState::failed;bus_.invalidate();if(deliver)(void)notify(false);return false;
    }
    bool submit(){const auto &p=plan_.commands[index_];return bus_.submit(p.id,false,true,p.bytes,p.length,{this,event},token_.operation,2000000,sequence_);}
    static bool event(void *owner,const network::FirmwareEvent &event,uint64_t epoch,uint64_t operation){
        auto &p=*static_cast<Programmer *>(owner);network::FirmwareAck ack{};
        if(p.state_!=ProgramState::pending||epoch!=p.token_.epoch||operation!=p.token_.operation||
            !network::decodeAck(event,ack)||!ack.done||ack.sequence!=p.sequence_||
            !network::sameCommand(ack.command,p.plan_.commands[p.index_].id))return p.fail(ProgramError::bus,true);
        if(ack.returnCode)return p.fail(ProgramError::firmware,true);
        if(++p.index_<p.plan_.count)return p.submit()||p.fail(ProgramError::bus,true);
        p.state_=ProgramState::complete;return p.notify(true);
    }
public:
    explicit Programmer(network::FirmwareCommands<Transport> &bus):bus_(bus){}
    Programmer(const Programmer&)=delete;Programmer&operator=(const Programmer&)=delete;
    ProgramState state()const{return state_;}ProgramError error()const{return error_;}
    unsigned acknowledged()const{return index_;}
    bool begin(const Plan &plan,Token token,Completion completion){
        if(notifying_||state_==ProgramState::pending||state_==ProgramState::failed||!token.operation||
           token.epoch!=bus_.epoch()||!completion.owner||!completion.finished||!plan.count||plan.count>2)return false;
        for(unsigned i=0;i<plan.count;++i){const auto &p=plan.commands[i];
            if(!((network::sameCommand(p.id,camCommand)&&p.length==60)||(network::sameCommand(p.id,cmacCommand)&&p.length==68)))return false;}
        if(!bus_.service())return fail(ProgramError::bus,false);
        plan_=plan;token_=token;completion_=completion;index_=0;error_=ProgramError::none;state_=ProgramState::pending;
        return submit()||fail(ProgramError::bus,false);
    }
    bool service(){if(state_!=ProgramState::pending)return state_!=ProgramState::failed;
        return bus_.service()||fail(ProgramError::bus,true);}
    bool cancel(){return state_==ProgramState::pending?fail(ProgramError::cancelled,true):false;}
};
enum class SeedError {none,precondition,clock,timeout,write,drain};
struct SeedResult {SeedError error{};unsigned attempted{},written{};bool requiresReset{},complete{};};
// Exactly the pinned AX indirect DMAC/CMAC initialization writes. Io must own
// the shared FILTER_MODEL_ADDR window and keep scheduler TX paused throughout.
// No lease is released and no station/data-ready flag is changed here.
template<class Io> SeedResult seedMacTables(Io &io,uint8_t macid){
    SeedResult result{};
    if(macid>=128||!io.inGate()||!io.stationTableWindowOwned()||!io.schedulerPaused()){
        result.error=SeedError::precondition;return result;}
    uint64_t last=io.nowUs();if(UINT64_MAX-last<100000){result.error=SeedError::clock;return result;}
    const uint64_t deadline=last+100000;
    auto check=[&](){const auto now=io.nowUs();
        if(now<last)result.error=SeedError::clock;
        else if(now>=deadline)result.error=SeedError::timeout;
        else if(!io.inGate()||!io.stationTableWindowOwned()||!io.schedulerPaused())result.error=SeedError::precondition;
        last=now;return result.error==SeedError::none;};
    auto write=[&](uint32_t reg,uint32_t value){
        if(!check())return false;++result.attempted;result.requiresReset=true;
        if(!io.write32(reg,value)){result.error=SeedError::write;return false;}
        ++result.written;return check();};
    for(unsigned i=0;i<4;++i)if(!write(wire::R_AX_FILTER_MODEL_ADDR,wire::DMAC_TBL_BASE_ADDR+(uint32_t(macid)<<4)+(i<<2))||
        !write(wire::R_AX_INDIR_ACCESS_ENTRY,0))return result;
    if(!write(wire::R_AX_FILTER_MODEL_ADDR,wire::CMAC_TBL_BASE_ADDR+uint32_t(macid)*wire::CCTL_INFO_SIZE))return result;
    const uint32_t cmac[8]={0x4,0x400a0004,0,0,0,0x0e43000b,0,0x000b8109};
    for(unsigned i=0;i<8;++i)if(!write(wire::R_AX_INDIR_ACCESS_ENTRY+4*i,cmac[i]))return result;
    if(!io.drainWrites()){result.error=SeedError::drain;return result;}
    if(!check())return result;
    result.complete=true;result.requiresReset=false;return result;
}
} } }
namespace rtl8852be { namespace network { class MacCommandTransport; } }
namespace rtl8852be { namespace station { namespace tables {
using NativeProgrammer=Programmer<network::MacCommandTransport>;
extern template class Programmer<network::MacCommandTransport>;
} } }
