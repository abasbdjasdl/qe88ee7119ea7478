// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2020 Realtek Corporation (upstream BSD option)
#pragma once
#include "FirmwareMailbox.hpp"
#include "DeviceCalibration.hpp"
#include "../FirmwarePlan.hpp"
namespace rtl8852be { namespace firmware {
constexpr uint8_t getFeatureCommand=3,phyCapabilityReply=3;
enum class CapabilityError {none,identity,mailbox,length,antenna,invalidated};
struct CapabilitySnapshot {
    uint32_t words[4]{};uint64_t epoch{};uint8_t cut{},mac[6]{},rfe{};
    uint8_t reportedTxNss{},reportedRxNss{},reportedTxAntennas{},reportedRxAntennas{};
    uint8_t bandwidthRaw{},protocolRaw{},nicRaw{},wirelessFunctionRaw{},hardwareTypeRaw{};
    uint8_t sequence{},txNss{},rxNss{},antennaTx{},antennaRx{};bool ack{};
    bool txNssFallback{},rxNssFallback{},txPathDiversity{},supportCckpd{},supportIgi{};
    // The pinned firmware ABI gives byte positions, NOT the BW/PROT encoding.
    // These fields are evidence only, never a decoded firmware capability mask.
    bool bandwidthEncodingKnown{},protocolEncodingKnown{};
    // antennaTx/Rx are upstream HAL overrides (0 means chip's normal paths).
    uint8_t effectiveTxPaths()const{return antennaTx?antennaTx:3;}
    uint8_t effectiveRxPaths()const{return antennaRx?antennaRx:3;}
};
struct CapabilityResult {CapabilityError error{CapabilityError::none};MailResult mail{};bool complete{};};
// One query per loaded firmware epoch, on the existing mailbox owner's workloop.
// No new mailbox is created: stale replies cannot be silently consumed as ours.
// Controller must invalidate before CPU reset/power-off or changing the device.
template<class Mail> class FirmwareCapabilities {
    Mail &mail_;CapabilitySnapshot snapshot_{};CapabilityResult result_{};bool attempted_{};
    bool fail(CapabilityError error){snapshot_={};result_.complete=false;result_.error=error;return false;}
public:
    explicit FirmwareCapabilities(Mail &mail):mail_(mail){}
    FirmwareCapabilities(const FirmwareCapabilities&)=delete;
    FirmwareCapabilities &operator=(const FirmwareCapabilities&)=delete;
    const CapabilityResult &result()const{return result_;}
    const CapabilitySnapshot *snapshot()const{return result_.complete?&snapshot_:nullptr;}
    void invalidate(){attempted_=true;fail(CapabilityError::invalidated);}
    bool query(const network::CalibrationSnapshot *calibration,const Plan &loaded,uint64_t epoch){
        if(attempted_)return false;attempted_=true;
        // loaded is the successfully downloaded, parser-validated plan, not a
        // proposed image. Native MacMailboxIo separately checks PCI 10ec:b852.
        if(!calibration||!calibration->board.identityValid||calibration->cut>1||!epoch||
           !loaded.valid||loaded.cut!=calibration->cut||(loaded.type!=1&&loaded.type!=5))
            return fail(CapabilityError::identity);
        Request q{};q.function=getFeatureCommand;
        if(!mail_.exchange(q,phyCapabilityReply)){result_.mail=mail_.result;return fail(CapabilityError::mailbox);}
        result_.mail=mail_.result;const auto &reply=result_.mail.reply;
        if(!result_.mail.triggerAttempted||!result_.mail.replyCaptured||!result_.mail.replyAcknowledged||
           result_.mail.error!=MailError::none||reply.function!=phyCapabilityReply)
            return fail(CapabilityError::mailbox);
        if(reply.contentBytes!=14)return fail(CapabilityError::length);
        CapabilitySnapshot s{};for(unsigned i=0;i<4;++i)s.words[i]=reply.words[i];
        s.reportedRxNss=uint8_t(s.words[0]>>16);s.bandwidthRaw=uint8_t(s.words[0]>>24);
        s.reportedTxNss=uint8_t(s.words[1]);s.protocolRaw=uint8_t(s.words[1]>>8);
        s.nicRaw=uint8_t(s.words[1]>>16);s.wirelessFunctionRaw=uint8_t(s.words[1]>>24);
        s.hardwareTypeRaw=uint8_t(s.words[2]);
        s.reportedTxAntennas=uint8_t(s.words[3]>>8);s.reportedRxAntennas=uint8_t(s.words[3]>>16);
        // 8852B has two RF paths. Zero retains upstream's default-path behavior;
        // unknown larger counts cannot silently select usable calibration paths.
        if(s.reportedTxAntennas>2||s.reportedRxAntennas>2)return fail(CapabilityError::antenna);
        s.txNssFallback=!s.reportedTxNss;s.rxNssFallback=!s.reportedRxNss;
        s.txNss=s.reportedTxNss==1?1:2;s.rxNss=s.reportedRxNss==1?1:2;
        if(s.reportedTxAntennas==1)s.antennaTx=2;
        if(s.reportedRxAntennas==1)s.antennaRx=2;
        if(s.reportedTxNss==1&&s.reportedTxAntennas==2&&s.reportedRxAntennas==2){s.antennaTx=2;s.txPathDiversity=true;}
        s.epoch=epoch;s.cut=calibration->cut;s.rfe=calibration->board.rfe;
        for(unsigned i=0;i<6;++i)s.mac[i]=calibration->board.mac[i];
        s.sequence=reply.sequence;s.ack=reply.ack;
        s.supportCckpd=s.cut>0;s.supportIgi=false;
        snapshot_=s;result_.complete=true;return true;
    }
};
} }
