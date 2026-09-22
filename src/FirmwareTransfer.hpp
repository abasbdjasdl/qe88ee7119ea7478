// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "FirmwarePackets.hpp"
namespace rtl8852be { namespace transport {
enum class Phase {idle,waitH2c,header,waitDownload,clearHalt,sections,waitReady,drain,quiesce,done};
enum class TransferStatus {notRun,complete,invalidPlan,prepareFailed,startFailed,
    clockInvalid,timeout,cancelled,registerInvalid,checksumFailed,securityFailed,
    cutMismatch,submitFailed,clearFailed,indexInvalid,staleReady,quiesceFailed,releaseFailed};
struct TransferResult {
    TransferStatus status{TransferStatus::notRun},operationStatus{TransferStatus::notRun};
    Phase phase{Phase::idle},failedPhase{Phase::idle};
    unsigned submitted{},attempted{},polls{};
    uint8_t lastControl{};uint32_t lastIndex{};uint64_t elapsedUs{};
    bool cleanupAttempted{},quiesced{},buffersReleased{},retainBuffers{};
};
inline TransferStatus controlError(uint8_t c){
    if(c==0xff)return TransferStatus::registerInvalid;
    switch((c>>5)&7){
    case 2:return TransferStatus::checksumFailed;
    case 3:return TransferStatus::securityFailed;
    case 4:return TransferStatus::cutMismatch;
    default:return TransferStatus::notRun;
    }
}
// Portable firmware-transfer orchestration. The bounded PCI backend implements
// the contract in docs/firmware-transport.md, including idle proof and ownership.
// All packet buffers stay pinned for the ENTIRE transaction; no ring-index
// observation alone releases/recycles a buffer (rtw89 PCI MULTITAG=8).
template<class Backend>
TransferResult transfer(Backend &b,const Packets &packets){
    TransferResult r;
    if(!packets.count()||packets.count()>maxPackets){r.status=TransferStatus::invalidPlan;return r;}
    const auto start=b.nowUs();uint64_t previous=start;
    auto check=[&](){
        const auto now=b.nowUs();
        if(now<previous){r.status=TransferStatus::clockInvalid;return false;}
        previous=now;r.elapsedUs=now-start;
        if(b.cancelled()){r.status=TransferStatus::cancelled;return false;}
        if(r.elapsedUs>=5000000){r.status=TransferStatus::timeout;return false;}
        return true;
    };
    auto wait=[&](bool path,uint8_t bit){
        const auto begin=b.nowUs();
        for(unsigned i=0;i<8001;++i){
            if(!check())return false;
            const auto elapsed=previous-begin;
            if(previous<begin||elapsed>=400000){r.status=TransferStatus::timeout;return false;}
            r.lastControl=b.readControl();++r.polls;
            if(!check())return false;
            if(previous<begin||previous-begin>=400000){r.status=TransferStatus::timeout;return false;}
            const auto error=controlError(r.lastControl);
            if(error!=TransferStatus::notRun){r.status=error;return false;}
            if(path&&((r.lastControl>>5)&7)==7){r.status=TransferStatus::staleReady;return false;}
            if(path?(r.lastControl&bit)!=0:((r.lastControl>>5)&7)==7)return true;
            if(i<8000)b.delayUs(50);
        }
        r.status=TransferStatus::timeout;return false;
    };
    bool prepared=false,startAttempted=false;
    do{
        if(!check())break;
        // prepare must encode/map/synchronize ALL packets and the entire ring
        // without enabling DMA or publishing any producer index.
        prepared=true;
        if(!b.prepare(packets)){r.status=TransferStatus::prepareFailed;break;}
        if(!check())break;
        startAttempted=true;
        if(!b.startDownload()){r.status=TransferStatus::startFailed;break;}
        r.phase=Phase::waitH2c;if(!wait(true,2))break;
        r.phase=Phase::header;++r.attempted;
        if(!b.publish(1)){r.status=TransferStatus::submitFailed;break;}
        ++r.submitted;
        r.phase=Phase::waitDownload;if(!wait(true,4))break;
        r.phase=Phase::clearHalt;
        if(!check())break;
        if(!b.clearHaltControls()){r.status=TransferStatus::clearFailed;break;}
        r.phase=Phase::sections;
        for(unsigned i=1;i<packets.count();++i){
            if(!check())break;
            ++r.attempted;
            if(!b.publish(i+1)){r.status=TransferStatus::submitFailed;break;}
            ++r.submitted;
        }
        if(r.status!=TransferStatus::notRun)break;
        r.phase=Phase::waitReady;
        // AX firmware needs a 5 ms settling delay before its readiness poll.
        for(unsigned i=0;i<100;++i){if(!check())break;b.delayUs(50);}
        if(r.status!=TransferStatus::notRun||!wait(false,0))break;
        r.phase=Phase::drain;
        const auto begin=b.nowUs();bool drained=false;
        for(unsigned i=0;i<8001;++i){
            if(!check())break;
            if(previous<begin||previous-begin>=400000){r.status=TransferStatus::timeout;break;}
            r.lastIndex=b.readIndex();++r.polls;
            if(!check())break;
            if(previous<begin||previous-begin>=400000){r.status=TransferStatus::timeout;break;}
            const auto host=r.lastIndex&0xfff,hw=(r.lastIndex>>16)&0xfff;
            if(r.lastIndex==0xffffffff||r.lastIndex==0xdeadbeef||host!=packets.count()||hw>packets.count()){
                r.status=TransferStatus::indexInvalid;break;
            }
            if(hw==packets.count()){drained=true;break;}
            if(i<8000)b.delayUs(50);
        }
        if(!drained){if(r.status==TransferStatus::notRun)r.status=TransferStatus::timeout;break;}
        r.status=TransferStatus::complete;
    }while(false);
    r.failedPhase=r.status==TransferStatus::complete?Phase::idle:r.phase;
    r.operationStatus=r.status;
    if(prepared){
        r.cleanupAttempted=true;r.phase=Phase::quiesce;
        // Even partial start/publish can leave DMA active. If the backend
        // cannot prove it stopped, return with ownership retained, never free.
        r.quiesced=!startAttempted||b.quiesceAndProveIdle();
        if(r.quiesced){
            r.buffersReleased=b.releaseAll();
            if(!r.buffersReleased){r.retainBuffers=true;r.status=TransferStatus::releaseFailed;}
        }
        else{r.retainBuffers=true;r.status=TransferStatus::quiesceFailed;}
    }
    r.phase=Phase::done;return r;
}
} }
