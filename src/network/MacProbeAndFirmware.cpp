// SPDX-License-Identifier: BSD-3-Clause
#include "MacProbeAndFirmware.hpp"
#include "MacDevicePowerIo.hpp"
#include "MacFirmwareDownload.hpp"
#include "MacDeviceCalibration.hpp"
#include "MacFirmwareMailboxIo.hpp"
#include "MacInitializationIo.hpp"
#include "MacPciLinkIo.hpp"
#include "MacPciRuntimeIo.hpp"
#include "MacPciRingIo.hpp"
#include <IOKit/IOLib.h>
namespace rtl8852be { namespace network {
namespace {
// Monotonic across service objects/devices for this loaded driver incarnation.
uint64_t nextFirmwareEpoch=1;
bool reserveEpochs(uint64_t &first){
    auto old=__atomic_load_n(&nextFirmwareEpoch,__ATOMIC_RELAXED);
    do {if(old>UINT64_MAX-2)return false;}
    while(!__atomic_compare_exchange_n(&nextFirmwareEpoch,&old,old+2,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED));
    first=old;return true;
}
bool invalidWord(uint32_t v){return v==0xffffffff||v==0xdeadbeef||v==0xeaeaeaea;}
}
struct MacProbeAndFirmware::State {
    struct Cycle {
        powerseq::MacDevicePowerIo powerIo;powerseq::DevicePower<powerseq::MacDevicePowerIo> power;
        firmwareboot::MacFirmwareBootIo bootIo;firmwareboot::Preparation<firmwareboot::MacFirmwareBootIo> boot;
        firmwareboot::MacFirmwareDownload download;
        pcilink::MacPciLinkIo linkIo;pcilink::Initialization<pcilink::MacPciLinkIo> link;
        firmware::MacMailboxIo mailboxIo;firmware::Mailbox<firmware::MacMailboxIo> mailbox;
        firmware::FirmwareCapabilities<firmware::Mailbox<firmware::MacMailboxIo>> capabilities;
        bool powerTouched{},closed{},downloadAttempted{};
        Cycle(IOPCIDevice &d,IOMemoryMap &m,IOWorkLoop &w):powerIo(&d,&m,&w),power(powerIo),bootIo(&d,&m,&w),boot(bootIo),
            download(bootIo,d,w),linkIo(&d,&m,&w),link(linkIo),mailboxIo(&d,&m,&w),mailbox(mailboxIo),capabilities(mailbox){}
        bool valid(){return powerIo.valid()&&bootIo.valid()&&linkIo.valid()&&mailboxIo.valid();}
        void cancel(){powerIo.cancel();bootIo.cancel();linkIo.cancel();mailboxIo.cancel();}
    };
    IOPCIDevice &device;IOMemoryMap &map;IOWorkLoop &loop;
    Cycle *cycles[2]{};transport::Packets packets;
    MacDeviceCalibration calibration;
    macinit::MacInitializationIo macIo;macinit::MacInitialization<macinit::MacInitializationIo> mac;
    MacPciRuntimeIo stopIo;PciRuntime<MacPciRuntimeIo> dmaStop;MacPciRingIo ringIo;
    uint64_t firstEpoch{};unsigned currentCycle{};bool prepareAttempted{},cancelled{},safeToRelease=true;
    State(IOPCIDevice &d,IOMemoryMap &m,IOWorkLoop &w):device(d),map(m),loop(w),calibration(&d,&m),
        macIo(&d,&m),mac(macIo),stopIo(&d,&m),dmaStop(stopIo),ringIo(&d,&m){}
    ~State(){delete cycles[1];delete cycles[0];}
};
MacProbeAndFirmware::~MacProbeAndFirmware(){
    if(state_&&!release())IOLog("RTL8852BE: firmware service retained: shutdown/DMA release not proven\n");
}
bool MacProbeAndFirmware::fail(ProbeFirmwareError error){
    if(result_.error==ProbeFirmwareError::none){
        result_.error=error;result_.failedStage=result_.stage;
        // Capture the first failing stage BEFORE service cleanup mutates the
        // per-cycle results. Stored snapshots only: logging performs no MMIO.
        IOLog("RTL8852BE: probe failure stage=%u error=%u epoch=%llu cut=%u cycle=%u\n",
            unsigned(result_.failedStage),unsigned(error),static_cast<unsigned long long>(result_.epoch),
            unsigned(result_.cut),state_?state_->currentCycle:0);
        if(state_){
            state_->device.setProperty("R16ProbeStage",uint64_t(result_.failedStage),32);
            state_->device.setProperty("R16ProbeError",uint64_t(error),32);
            const auto &mac=state_->mac.result;
            IOLog("RTL8852BE: probe MAC stage=%u error=%u address=0x%x wanted=0x%x actual=0x%x\n",
                unsigned(mac.stage),unsigned(mac.error),mac.address,mac.expected,mac.actual);
            if(auto *cycle=state_->cycles[state_->currentCycle]){
                const auto &prep=cycle->boot.result;const auto &fw=cycle->download.result;
                const auto &pci=cycle->link.result;const auto &downloadPci=cycle->download.pciResult;
                const auto &power=cycle->power.result.on;
                state_->device.setProperty("R16ProbePciError",uint64_t(pci.error),32);
                state_->device.setProperty("R16ProbePowerError",uint64_t(power.error),32);
                state_->device.setProperty("R16ProbePowerAddress",uint64_t(power.address),32);
                state_->device.setProperty("R16ProbePowerExpected",uint64_t(power.expected),32);
                state_->device.setProperty("R16ProbePowerActual",uint64_t(power.actual),32);
                state_->device.setProperty("R16ProbePowerReads",uint64_t(power.reads),32);
                state_->device.setProperty("R16ProbePowerWrites",uint64_t(power.writes),32);
                state_->device.setProperty("R16ProbeDownloadStatus",uint64_t(fw.status),32);
                state_->device.setProperty("R16ProbePollAddress",uint64_t(downloadPci.pollFailureAddress),32);
                state_->device.setProperty("R16ProbePollMask",uint64_t(downloadPci.pollFailureMask),32);
                state_->device.setProperty("R16ProbePollWanted",uint64_t(downloadPci.pollFailureExpected),32);
                state_->device.setProperty("R16ProbePollActual",uint64_t(downloadPci.pollFailureActual),32);
                IOLog("RTL8852BE: probe cycle preparation stage=%u error=%u address=0x%x wanted=0x%x actual=0x%x "
                    "fw=%u operation=%u phase=%u failedPhase=%u lastControl=0x%x lastIndex=0x%x retained=%u "
                    "pciError=%u pciAddress=0x%x pciValue=0x%x powerError=%u powerAddress=0x%x\n",
                    unsigned(prep.stage),unsigned(prep.error),prep.address,prep.wanted,prep.actual,
                    unsigned(fw.status),unsigned(fw.operationStatus),unsigned(fw.phase),unsigned(fw.failedPhase),
                    unsigned(fw.lastControl),fw.lastIndex,unsigned(cycle->download.retained()),
                    unsigned(pci.error),pci.address,pci.value,unsigned(power.error),power.address);
                IOLog("RTL8852BE: probe download PCI stage=%u failureAddress=0x%x wanted=0x%x actual=0x%x "
                    "pollAddress=0x%x pollMask=0x%x pollWanted=0x%x pollActual=0x%x pollReason=%u\n",
                    downloadPci.stage,downloadPci.failureAddress,downloadPci.failureExpected,downloadPci.failureActual,
                    downloadPci.pollFailureAddress,downloadPci.pollFailureMask,downloadPci.pollFailureExpected,
                    downloadPci.pollFailureActual,downloadPci.pollFailureReason);
            }
        }
    }
    result_.stage=ProbeFirmwareStage::failed;result_.prepared=false;return false;
}
bool MacProbeAndFirmware::readCut(uint8_t &cut){
    cut=0;if(!state_||!state_->loop.inGate()||!state_->cycles[0])return false;
    auto &io=state_->cycles[0]->bootIo;uint32_t first=0,second=0;
    if(!io.read32(0xf0,first)||!io.read32(0xf0,second)||invalidWord(first)||invalidWord(second))return false;
    cut=uint8_t((first>>12)&15);return cut<=1&&cut==uint8_t((second>>12)&15);
}
bool MacProbeAndFirmware::allocate(IOPCIDevice &device,IOMemoryMap &map,IOWorkLoop &loop,const uint8_t *bytes,size_t length){
    if(state_||result_.stage!=ProbeFirmwareStage::idle||loop.inGate())return false;
    result_.stage=ProbeFirmwareStage::allocation;
    state_=new State(device,map,loop);if(!state_)return fail(ProbeFirmwareError::allocation);
    auto &s=*state_;
    for(unsigned i=0;i<2;++i){s.cycles[i]=new State::Cycle(device,map,loop);
        if(!s.cycles[i]||!s.cycles[i]->valid())return fail(ProbeFirmwareError::allocation);}
    if(!s.calibration.valid()||!s.macIo.valid()||!s.stopIo.valid()||!s.ringIo.valid())return fail(ProbeFirmwareError::identity);
    result_.stage=ProbeFirmwareStage::identity;
    // Public runAction performs the short gate acquisition; IOWorkLoop's
    // closeGate/openGate methods themselves are protected in the native SDK.
    const auto identity=loop.runAction([](OSObject *,void *owner,void *,void *,void *)->IOReturn {
        auto &self=*static_cast<MacProbeAndFirmware *>(owner);
        const auto command=self.state_->device.configRead16(kIOPCIConfigCommand);
        return command!=0xffff&&(command&6)==2&&self.readCut(self.result_.cut)?kIOReturnSuccess:kIOReturnError;
    },&device,this);
    if(identity!=kIOReturnSuccess)return fail(ProbeFirmwareError::identity);
    if(s.packets.initialize(bytes,length,result_.cut)!=transport::PacketStatus::ok)return fail(ProbeFirmwareError::firmware);
    if(!reserveEpochs(s.firstEpoch))return fail(ProbeFirmwareError::identity);
    for(auto *cycle:s.cycles)if(!cycle->download.allocate(s.packets))return fail(ProbeFirmwareError::allocation);
    result_.stage=ProbeFirmwareStage::allocation;result_.allocated=true;return true;
}
bool MacProbeAndFirmware::clearRings(){
    auto &s=*state_;
    // Called only after dmaStop.stop() proved every engine idle, BM-off and IRQ
    // masks zero. No runtime descriptors have been published by this service.
    if(!s.loop.inGate()||!s.dmaStop.result.stopped)return false;
    for(const auto &r:ringRegisters){
        const auto count=s.ringIo.read16(r.count);if(count==0xffff)return false;
        if(!s.ringIo.write32(r.low,0)||!s.ringIo.write32(r.high,0)||!s.ringIo.write16(r.count,uint16_t(count&0xf000)))return false;
        if(s.ringIo.read32(r.low)||s.ringIo.read32(r.high)||(s.ringIo.read16(r.count)&0xfff))return false;
    }
    if(!s.ringIo.write32(0x1014,0x70f)||!s.ringIo.write32(0x1018,3))return false;
    for(const auto &r:ringRegisters)if(s.ringIo.read32(r.index))return false;
    return true;
}
bool MacProbeAndFirmware::startCycle(unsigned index,const CalibrationSnapshot *calibration){
    auto &s=*state_;auto &c=*s.cycles[index];const bool probe=index==0;
    s.currentCycle=index;
    uint8_t observed=0;result_.stage=ProbeFirmwareStage::identity;
    if(s.cancelled)return fail(ProbeFirmwareError::cancelled);
    if(!readCut(observed)||observed!=result_.cut)return fail(ProbeFirmwareError::identity);
    result_.stage=probe?ProbeFirmwareStage::probePower:ProbeFirmwareStage::runtimePower;
    s.safeToRelease=false;
    const bool powered=c.power.start(result_.cut,calibration);c.powerTouched=c.power.result.on.writes!=0;
    if(!powered)return fail(ProbeFirmwareError::power);
    // Independent host-DMA stop proof before preinit/download and ring clearing.
    if(!s.dmaStop.stop())return fail(ProbeFirmwareError::dma);
    result_.stage=probe?ProbeFirmwareStage::probeDmac:ProbeFirmwareStage::runtimeDmac;
    if(!c.boot.prepareDmac())return fail(ProbeFirmwareError::preparation);
    result_.stage=probe?ProbeFirmwareStage::probePci:ProbeFirmwareStage::runtimePci;
    if(!c.link.preInit(result_.cut)||!clearRings())return fail(ProbeFirmwareError::pci);
    result_.stage=probe?ProbeFirmwareStage::probeCpu:ProbeFirmwareStage::runtimeCpu;
    result_.epoch=s.firstEpoch+index;
    if(!c.boot.enableCpuForDownload())return fail(ProbeFirmwareError::preparation);
    result_.stage=probe?ProbeFirmwareStage::probeDownload:ProbeFirmwareStage::runtimeDownload;c.downloadAttempted=true;
    const auto transferred=c.download.run(s.packets);
    result_.downloadRetained=c.download.retained();
    if(!c.boot.acceptDownload(transferred))return fail(ProbeFirmwareError::firmware);
    result_.cpuRunning=true;return true;
}
bool MacProbeAndFirmware::stopCycle(unsigned index,bool probe){
    auto &s=*state_;auto *c=s.cycles[index];if(!c||c->closed)return true;
    c->capabilities.invalidate();
    if(!c->powerTouched){c->closed=true;return !c->download.retained();}
    // Event sources are external and must already be disabled/drained. Even a
    // failed stop cuts BM; no later success can retroactively free retained bank.
    const bool dma=s.dmaStop.stop();bool cpu=false,power=false;
    if(dma){
        cpu=!c->boot.result.modified||c->boot.result.stage==firmwareboot::Stage::stopped||c->boot.stopCpu();
        if(cpu)power=c->power.result.returnedOff||c->power.stop(s.calibration.snapshot(),probe);
    }
    result_.downloadRetained|=c->download.retained();
    c->closed=dma&&cpu&&power&&!c->download.retained();
    return c->closed;
}
bool MacProbeAndFirmware::prepare(){
    if(!state_||!result_.allocated||state_->prepareAttempted||!state_->loop.inGate())return false;
    auto &s=*state_;s.prepareAttempted=true;
    bool ok=startCycle(0,nullptr);
    if(ok){result_.stage=ProbeFirmwareStage::calibration;ok=s.calibration.read(result_.cut);if(!ok)fail(ProbeFirmwareError::calibration);}
    if(ok){result_.stage=ProbeFirmwareStage::probeCapabilities;
        ok=s.cycles[0]->capabilities.query(s.calibration.snapshot(),s.packets.plan(),s.firstEpoch);if(!ok)fail(ProbeFirmwareError::capabilities);}
    if(ok){result_.stage=ProbeFirmwareStage::probeStop;ok=stopCycle(0,true);
        result_.probeStopped=ok;result_.cpuRunning=!ok;if(!ok)fail(ProbeFirmwareError::cleanup);}
    if(ok)ok=startCycle(1,s.calibration.snapshot());
    if(ok){result_.stage=ProbeFirmwareStage::runtimeCapabilities;
        ok=s.cycles[1]->capabilities.query(s.calibration.snapshot(),s.packets.plan(),s.firstEpoch+1);if(!ok)fail(ProbeFirmwareError::capabilities);}
    if(ok){result_.stage=ProbeFirmwareStage::macRadio;ok=s.mac.configureCut(result_.cut)&&s.mac.enableRadio();if(!ok)fail(ProbeFirmwareError::mac);}
    if(ok){result_.stage=ProbeFirmwareStage::macSystem;ok=s.mac.enableSystem();if(!ok)fail(ProbeFirmwareError::mac);}
    if(ok){result_.stage=ProbeFirmwareStage::macDmac;ok=s.mac.initializeDmac();if(!ok)fail(ProbeFirmwareError::mac);}
    if(ok){result_.stage=ProbeFirmwareStage::macCmac;ok=s.mac.initializeCmac();if(!ok)fail(ProbeFirmwareError::mac);}
    if(ok){result_.stage=ProbeFirmwareStage::macReports;ok=s.mac.finishTrx();if(!ok)fail(ProbeFirmwareError::mac);}
    if(ok){result_.stage=ProbeFirmwareStage::pciPost;ok=s.cycles[1]->link.postInit();if(!ok)fail(ProbeFirmwareError::pci);}
    if(!ok){const auto failed=result_.failedStage;const auto error=result_.error;(void)stop();
        result_.failedStage=failed;result_.error=error;result_.stage=ProbeFirmwareStage::failed;return false;}
    // No runtime ring bank or interrupts are started here. Chip feat-init is
    // a source no-op for 8852B BACAM_V0. Early H2C is a source debugfs-injected
    // list (fw.c:5572, debug.c:3328); this port has no injector, hence it is empty.
    // Offload configuration waits for real CH12/RX dispatch and its Done ACK.
    result_.earlyH2cPending=false;
    result_.stage=ProbeFirmwareStage::prepared;result_.prepared=true;return true;
}
bool MacProbeAndFirmware::stop(){
    if(!state_||!state_->loop.inGate())return false;auto &s=*state_;
    // Terminal even if called before prepare: closed one-shot cycles must never
    // be started afterwards and then mistaken for already shut down.
    s.prepareAttempted=true;
    if(invalidateCommands_){invalidateCommands_(commands_);invalidateCommands_=nullptr;commands_=nullptr;}
    result_.prepared=false;
    const bool second=stopCycle(1,false);const bool first=stopCycle(0,true);
    const bool ok=first&&second;s.safeToRelease=ok;result_.stopped=ok;result_.requiresRecovery=!ok;
    if(ok){result_.cpuRunning=false;result_.stage=ProbeFirmwareStage::stopped;}
    else fail(ProbeFirmwareError::cleanup);return ok;
}
void MacProbeAndFirmware::cancel(){
    if(!state_||!state_->loop.inGate())return;state_->cancelled=true;state_->calibration.cancel();state_->macIo.cancel();
    for(auto *c:state_->cycles)if(c)c->cancel();
}
bool MacProbeAndFirmware::release(){
    if(!state_)return true;auto &s=*state_;
    if(s.loop.inGate()||!s.safeToRelease)return false;
    for(auto *c:s.cycles)if(c&&(c->download.retained()||!c->download.releaseUnpublished()))return false;
    delete state_;state_=nullptr;return true;
}
const CalibrationSnapshot *MacProbeAndFirmware::calibration()const{return state_?state_->calibration.snapshot():nullptr;}
const firmware::CapabilitySnapshot *MacProbeAndFirmware::capabilities()const{
    return state_&&result_.prepared?state_->cycles[1]->capabilities.snapshot():nullptr;}
const firmware::Plan *MacProbeAndFirmware::firmwarePlan()const{return state_&&result_.allocated?&state_->packets.plan():nullptr;}
firmware::Mailbox<firmware::MacMailboxIo> *MacProbeAndFirmware::mailbox(){
    return state_&&result_.prepared?&state_->cycles[1]->mailbox:nullptr;
}
bool MacProbeAndFirmware::offloadCompleted(void *owner,const FirmwareEvent &event,uint64_t epoch,uint64_t token){
    auto &self=*static_cast<MacProbeAndFirmware *>(owner);FirmwareAck ack{};
    if(!self.state_||!self.state_->loop.inGate()||!self.result_.prepared||!self.offloadSubmitted_||
       epoch!=self.result_.epoch||token!=epoch||!decodeAck(event,ack)||!ack.done||
       !sameCommand(ack.command,{1,9,0x14})||ack.sequence!=self.offloadSequence_)return false;
    if(ack.returnCode)return self.fail(ProbeFirmwareError::firmware);
    self.result_.offloadConfigurationPending=false;return true;
}
} }
