// SPDX-License-Identifier: BSD-3-Clause
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/OSByteOrder.h>
#include <kern/clock.h>
#include "PciConfig.hpp"
#include "MmioProbe.hpp"
#include "XtalProbe.hpp"
#include "KernelPacketMemory.hpp"
#include "FirmwareBank.hpp"
#include "FirmwareBoot.hpp"
#include "PciFirmwareTransport.hpp"
#include <libkern/OSAtomic.h>
#include "EmbeddedFirmware.hpp"
#include "PowerSequence.hpp"


class PciMmioAccess {
    IOPCIDevice *pci;
    IODeviceMemory *memory;
    IOMemoryMap *mapping{};
    uint16_t uploadOriginalCommand{};bool uploadCommandSaved{};
public:
    explicit PciMmioAccess(IOPCIDevice *p) : pci(p), memory(p->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2)) {}
    uint16_t command() { return pci->configRead16(kIOPCIConfigCommand); }
    void writeCommand(uint16_t value) { pci->configWrite16(kIOPCIConfigCommand, value); }
    uint64_t physical() { return memory ? memory->getPhysicalAddress() : 0; }
    uint64_t length() { return memory ? memory->getLength() : 0; }
    bool map() {
        mapping = pci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress2, kIOMapInhibitCache);
        if (mapping && mapping->getVirtualAddress() &&
            mapping->getPhysicalAddress() == physical() && mapping->getLength() == length()) return true;
        unmap(); return false;
    }
    uint32_t read32(uint32_t offset) {
        return OSReadLittleInt32(reinterpret_cast<const volatile void *>(mapping->getVirtualAddress()), offset);
    }
    bool startXtalCvRead() {
        if (!mapping || !(command() & rtl8852be::memoryEnable) ||
            (command() & rtl8852be::busMasterEnable)) return false;
        // This adapter exposes no arbitrary register-write operation.
        OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping->getVirtualAddress()),
                          rtl8852be::xtalControl, rtl8852be::xtalCvReadCommand);
        return true;
    }
    uint8_t readXtalData() {
        return *reinterpret_cast<const volatile uint8_t *>(mapping->getVirtualAddress()+rtl8852be::xtalControl+1);
    }
    uint64_t nowUs() {
        uint64_t ticks=0,ns=0;
        clock_get_uptime(&ticks);absolutetime_to_nanoseconds(ticks,&ns);
        return ns/1000;
    }
    void pause50Us() { IODelay(50); }
    void pauseUs(unsigned us) { if(us<=1000) IODelay(us); }
    uint8_t read8(uint32_t offset) {
        return *reinterpret_cast<const volatile uint8_t *>(mapping->getVirtualAddress()+offset);
    }
    uint16_t read16(uint32_t offset) {
        return OSReadLittleInt16(reinterpret_cast<const volatile void *>(mapping->getVirtualAddress()),offset);
    }
    bool powerWrite32(uint32_t offset,uint32_t value) {
        if(!mapping || !rtl8852be::power::allowed32(offset) || (command()&6)!=2)return false;
        OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping->getVirtualAddress()),offset,value);
        return true;
    }
    bool powerWrite8(uint32_t offset,uint8_t value) {
        if(!mapping || !rtl8852be::power::allowed8(offset) || (command()&6)!=2)return false;
        *reinterpret_cast<volatile uint8_t *>(mapping->getVirtualAddress()+offset)=value;
        return true;
    }
    bool bootWrite32(uint32_t offset,uint32_t value) {
        if(!mapping || !rtl8852be::boot::allowed32(offset) || (command()&6)!=2)return false;
        OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping->getVirtualAddress()),offset,value);
        return true;
    }
    bool bootWrite16(uint32_t offset,uint16_t value) {
        if(!mapping || offset!=0x1e6 || (command()&6)!=2)return false;
        OSWriteLittleInt16(reinterpret_cast<volatile void *>(mapping->getVirtualAddress()),offset,value);
        return true;
    }
    bool interruptsSafe(){
        if(!(pci->configRead16(kIOPCIConfigStatus)&0x10))return true;
        uint8_t at=pci->configRead8(0x34);uint64_t seen=0;
        for(unsigned n=0;at&&n<48;++n){
            if(at<0x40||at>0xfc||(at&3)||(seen&(uint64_t(1)<<(at/4))))return false;
            seen|=uint64_t(1)<<(at/4);const auto id=pci->configRead8(at);
            if(id==5&&(pci->configRead16(at+2)&1))return false;
            if(id==0x11&&(pci->configRead16(at+2)&0x8000))return false;
            at=pci->configRead8(at+1);
        }return !at;
    }
    bool uploadBusMaster(bool enable){
        const auto current=command();if(current==0xffff||!(current&2))return false;
        if(enable){
            if((current&6)!=2||!interruptsSafe())return false;
            uploadOriginalCommand=current;uploadCommandSaved=true;
            writeCommand(current|0x404);
            return command()==static_cast<uint16_t>(current|0x404);
        }
        // Keep INTx masked until WCPU and supply are stopped by the outer owner.
        const auto wanted=static_cast<uint16_t>(current&~4u);
        writeCommand(wanted);return command()==wanted;
    }
    bool restoreUploadCommandAfterPowerOff(){
        if(!uploadCommandSaved)return true;
        const auto current=command();if(current==0xffff||(current&6)!=2)return false;
        const auto wanted=static_cast<uint16_t>((current&~0x400u)|(uploadOriginalCommand&0x400));
        writeCommand(wanted);return command()==wanted;
    }
    void uploadBarrier(){__atomic_thread_fence(__ATOMIC_SEQ_CST);OSSynchronizeIO();}
    bool uploadWrite32(uint32_t offset,uint32_t value){
        if(!mapping||!rtl8852be::transport::uploadAddress32(offset)||!(command()&2))return false;
        OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping->getVirtualAddress()),offset,value);OSSynchronizeIO();return true;
    }
    bool uploadWrite16(uint32_t offset,uint16_t value){
        if(!mapping||!rtl8852be::transport::uploadAddress16(offset)||!(command()&2))return false;
        OSWriteLittleInt16(reinterpret_cast<volatile void *>(mapping->getVirtualAddress()),offset,value);OSSynchronizeIO();return true;
    }
    void unmap() { if (mapping) { mapping->release(); mapping = nullptr; } }
    ~PciMmioAccess() { unmap(); }
};
class RTL8852BEProbe : public IOService {
    OSDeclareDefaultStructors(RTL8852BEProbe)
public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
};
OSDefineMetaClassAndStructors(RTL8852BEProbe, IOService)
IOService *RTL8852BEProbe::probe(IOService *provider, SInt32 *score) {
    auto *pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci || pci->configRead16(kIOPCIConfigVendorID) != rtl8852be::vendor ||
        pci->configRead16(kIOPCIConfigDeviceID) != rtl8852be::device) return nullptr;
    return IOService::probe(provider, score);
}
bool RTL8852BEProbe::start(IOService *provider) {
    auto *pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci || !IOService::start(provider)) return false;
    if (!pci->open(this)) { IOService::stop(provider); return false; }
    uint8_t config[rtl8852be::configSize]{};
    for (unsigned offset = 0; offset < sizeof(config); offset += 4) {
        const UInt32 value = pci->configRead32(static_cast<UInt8>(offset));
        for (unsigned byte = 0; byte < 4; ++byte)
            config[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
    }
    rtl8852be::Snapshot s{};
    if (!rtl8852be::decode(config, sizeof(config), s) || !s.isTarget()) {
        pci->close(this); IOService::stop(provider); return false;
    }
    PciMmioAccess access(pci);
    rtl8852be::XtalResult x;

    rtl8852be::MmioResult r;
    rtl8852be::power::Result supply;
    rtl8852be::boot::Result rom;
    rtl8852be::boot::RepeatedResult repeated;
    rtl8852be::transport::TransferResult transfer;
    rtl8852be::transport::PciDownloadResult pciTransfer;
    bool uploadAttempted=false,releaseProven=false,interruptRestored=true;
    rtl8852be::transport::Packets packets;
    rtl8852be::transport::BankResult bank;
    const auto packetStatus=packets.initialize(rtl8852be::image::bytes,rtl8852be::image::length,1);
    bool deviceMapper=false,bankCleanup=false;
    uint32_t bankError=0,bankCleanupError=0;
    if(packetStatus==rtl8852be::transport::PacketStatus::ok){
        KernelPacketMemory buffers(pci,packets.count()+1);
        deviceMapper=buffers.deviceMapper();
        bank=rtl8852be::transport::prepareBank(buffers,packets);
        bankError=buffers.lastError();
        if(bank.status==rtl8852be::transport::BankStatus::ready){
            r=rtl8852be::sampleMmio(access,s,[&](PciMmioAccess &d,const rtl8852be::MmioResult &base){
                x=rtl8852be::sampleXtal(d,base);
                supply=rtl8852be::power::cycle(d,base,x,[&](PciMmioAccess &active){
                    repeated.first=rtl8852be::boot::probe(active);
                    if(repeated.first.status==rtl8852be::boot::Status::ready&&repeated.first.cleanupOK){
                        repeated.secondAttempted=true;
                        repeated.second=rtl8852be::boot::probeWithAction(active,[&](PciMmioAccess &ready){
                            uploadAttempted=true;buffers.markHardwareAttempt();
                            auto release=[](void *owner){auto *memory=static_cast<KernelPacketMemory *>(owner);return memory->confirmStopped(true)&&memory->releaseUnsubmitted();};
                            rtl8852be::transport::PciDownload<PciMmioAccess> backend(ready,buffers.mapping(0),packets.count(),true,release,&buffers);
                            transfer=rtl8852be::transport::transfer(backend,packets);pciTransfer=backend.result;
                        });
                    }
                    rom=repeated.first;
                });
                if(uploadAttempted)interruptRestored=supply.returnedOff&&d.restoreUploadCommandAfterPowerOff();
            });
        }
        if(uploadAttempted)releaseProven=buffers.confirmStopped(transfer.quiesced||supply.returnedOff);
        bankCleanup=buffers.releaseUnsubmitted();bankCleanupError=buffers.lastError();
    }
    const auto memoryCount = pci->getDeviceMemoryCount();
    pci->close(this);
    bool ok = setProperty("DiagnosticOnly", true);
    ok &= setProperty("WiFiOperational", false);
    ok &= setProperty("DriverVersion", "0.0.13");
    ok &= setProperty("Experiment", "FIRMWARE-UPLOAD-02");
    ok &= setProperty("Stage", rtl8852be::statusName(r.status));
    ok &= setProperty("VendorID", s.vendorID, 16);
    ok &= setProperty("DeviceID", s.deviceID, 16);
    ok &= setProperty("SubsystemVendorID", s.subsystemVendor, 16);
    ok &= setProperty("SubsystemDeviceID", s.subsystemDevice, 16);
    ok &= setProperty("RevisionID", s.revision, 8);
    ok &= setProperty("PCICommandSnapshot", s.command, 16);
    ok &= setProperty("PCICommandBeforeProbe", r.commandBefore, 16);
    ok &= setProperty("MemoryResourceCount", memoryCount, 32);
    ok &= setProperty("CapabilityParseStatus", static_cast<unsigned>(s.capStatus), 32);
    ok &= setProperty("CapabilityCount", s.capabilityCount, 32);
    ok &= setProperty("HasPM", s.hasCapability(1));
    ok &= setProperty("HasMSI", s.hasCapability(5));
    ok &= setProperty("HasPCIExpress", s.hasCapability(0x10));
    ok &= setProperty("HasMSIX", s.hasCapability(0x11));
    ok &= setProperty("PMControlReadable", s.pmControlReadable);
    ok &= setProperty("PMControlStatus", s.pmControlStatus, 16);
    ok &= setProperty("MMIOStatus", rtl8852be::statusName(r.status));
    ok &= setProperty("MMIOReadCount", r.reads, 32);
    ok &= setProperty("MMIOBase", r.base, 64);
    ok &= setProperty("MMIOLength", r.length, 64);
    ok &= setProperty("MemoryEnableChanged", r.memoryBitChanged);
    ok &= setProperty("PCICommandDuringMMIO", r.commandDuring, 16);
    ok &= setProperty("PCICommandAfterMMIO", r.commandAfter, 16);
    ok &= setProperty("PCICommandRestored", r.commandRestored);
    ok &= setProperty("SysCfg1First", r.cfgFirst, 32);
    ok &= setProperty("SysCfg1Second", r.cfgSecond, 32);
    ok &= setProperty("SysStatus1", r.sysStatus, 32);
    ok &= setProperty("RegisterValueStable", r.stableValue());
    if (r.stableValue()) ok &= setProperty("ChipCutCandidate", (r.cfgFirst >> 12) & 15, 8);
    ok &= setProperty("XtalStatus", rtl8852be::xtalStatusName(x.status));
    ok &= setProperty("XtalWrites", x.writes, 32);
    ok &= setProperty("XtalPolls", x.polls, 32);
    ok &= setProperty("XtalElapsedUs", x.elapsedUs, 64);
    ok &= setProperty("XtalControlBefore", x.controlBefore, 32);
    ok &= setProperty("XtalControlAfter", x.controlAfter, 32);
    ok &= setProperty("AnalogRevisionValid", x.revisionValid);
    if (x.revisionValid) {
        ok &= setProperty("AnalogRevisionRaw", x.rawRevision, 8);
        ok &= setProperty("AnalogRevision", x.rawRevision & 15, 8);
    }
    ok &= setProperty("PreflightReads32", x.reads32, 32);
    ok &= setProperty("PreflightReads8", x.reads8, 32);
    ok &= setProperty("PowerStateSampled", x.powerSampled);
    ok &= setProperty("PowerAfterSampled", x.powerAfterSampled);
    if (x.powerSampled) {
        ok &= setProperty("SysIsolation", x.isolation, 32);
        ok &= setProperty("SysPowerBefore", x.powerBefore, 32);
        ok &= setProperty("SysClock", static_cast<uint64_t>(x.clock), 64);
        ok &= setProperty("FirmwareControl", x.firmware, 32);
    }
    if (x.powerAfterSampled) ok &= setProperty("SysPowerAfter", x.powerAfter, 32);
    ok &= setProperty("FirmwareUploaded",transfer.operationStatus==rtl8852be::transport::TransferStatus::complete);
    ok &= setProperty("PacketPlanStatus",static_cast<unsigned>(packetStatus),32);
    ok &= setProperty("BankStatus",rtl8852be::transport::bankStatusName(bank.status));
    ok &= setProperty("BankPackets",bank.packets,32);
    ok &= setProperty("BankAllocated",bank.allocated,32);
    ok &= setProperty("BankPrepared",bank.prepared,32);
    ok &= setProperty("BankSynchronized",bank.synced,32);
    ok &= setProperty("BankFailedSlot",bank.failedSlot,32);
    ok &= setProperty("BankRingAddress",bank.ringAddress,64);
    ok &= setProperty("BankPayloadBytes",bank.payloadBytes,64);
    ok &= setProperty("BankDeviceMapper",deviceMapper);
    ok &= setProperty("BankOSReturn",static_cast<uint64_t>(bankError),64);
    ok &= setProperty("BankCleanupReturn",static_cast<uint64_t>(bankCleanupError),64);
    ok &= setProperty("BankCleanupOK",bankCleanup);
    ok &= setProperty("DmaSubmittedToHardware",pciTransfer.published>0);
    ok &= setProperty("DmaDoorbellAttempted",pciTransfer.doorbellAttempted);
    ok &= setProperty("RomStatus",rtl8852be::boot::statusName(rom.status));
    ok &= setProperty("RomOperationStatus",rtl8852be::boot::statusName(rom.operationStatus));
    ok &= setProperty("RomPhase",rom.phase,32);
    ok &= setProperty("RomWrites",rom.writes,32);
    ok &= setProperty("RomPolls",rom.polls,32);
    ok &= setProperty("RomWdeStatus",rom.wde,32);
    ok &= setProperty("RomPleStatus",rom.ple,32);
    ok &= setProperty("RomControl",rom.control,32);
    ok &= setProperty("RomDmac",rom.dmac,32);
    ok &= setProperty("RomClock",rom.clock,32);
    ok &= setProperty("RomWdeConfig",static_cast<uint64_t>(rom.wdeConfig),64);
    ok &= setProperty("RomPleConfig",static_cast<uint64_t>(rom.pleConfig),64);
    ok &= setProperty("RomHfcControl",static_cast<uint64_t>(rom.hfcControl),64);
    ok &= setProperty("RomHfcPages",rom.hfcPages,32);
    ok &= setProperty("RomAttempted",rom.attempted);
    ok &= setProperty("RomCleanupOK",rom.cleanupOK);
    ok &= setProperty("RomCPUStopped",rom.cpuStopped);
    ok &= setProperty("RomHciBefore",rom.hciBefore,32);
    ok &= setProperty("RomStopBefore",rom.stopBefore,32);
    ok &= setProperty("RomHciPaused",rom.hciPaused,32);
    ok &= setProperty("RomStopPaused",rom.stopPaused,32);
    ok &= setProperty("RomHciAfter",rom.hciAfter,32);
    ok &= setProperty("RomStopAfter",rom.stopAfter,32);
    ok &= setProperty("RomCleanupFailures",rom.cleanupFailures,32);
    ok &= setProperty("RomCleanupClockChecked",rom.cleanupClockChecked);
    ok &= setProperty("RomInitialDmac",static_cast<uint64_t>(rom.initialDmac),64);
    ok &= setProperty("RomInitialHci",static_cast<uint64_t>(rom.initialHci),64);
    ok &= setProperty("RomAccessDmac",static_cast<uint64_t>(rom.accessDmac),64);
    ok &= setProperty("RomAccessClock",static_cast<uint64_t>(rom.accessClock),64);
    ok &= setProperty("RomAccessReady",rom.accessReady);
    ok &= setProperty("RomHciCaptured",rom.hciCaptured);
    ok &= setProperty("RomRepeatAttempted",repeated.secondAttempted);
    ok &= setProperty("RomRepeatStatus",rtl8852be::boot::statusName(repeated.second.status));
    ok &= setProperty("RomRepeatOperationStatus",rtl8852be::boot::statusName(repeated.second.operationStatus));
    ok &= setProperty("RomRepeatPhase",repeated.second.phase,32);
    ok &= setProperty("RomRepeatInitialDmac",static_cast<uint64_t>(repeated.second.initialDmac),64);
    ok &= setProperty("RomRepeatInitialHci",static_cast<uint64_t>(repeated.second.initialHci),64);
    ok &= setProperty("RomRepeatAccessReady",repeated.second.accessReady);
    ok &= setProperty("RomRepeatControl",repeated.second.control,32);
    ok &= setProperty("RomRepeatCleanupOK",repeated.second.cleanupOK);
    ok &= setProperty("RomRepeatCleanupFailures",repeated.second.cleanupFailures,32);
    ok &= setProperty("RomRepeatCleanupClockChecked",repeated.second.cleanupClockChecked);
    ok &= setProperty("RomRepeatCleanupClock",static_cast<uint64_t>(repeated.second.cleanupClock),64);
    ok &= setProperty("RomRepeatCleanupDmac",static_cast<uint64_t>(repeated.second.cleanupDmac),64);
    ok &= setProperty("RomRepeatFailureAddress",static_cast<uint64_t>(repeated.second.failureAddress),64);
    ok &= setProperty("RomRepeatFailureMask",static_cast<uint64_t>(repeated.second.failureMask),64);
    ok &= setProperty("RomRepeatFailureExpected",static_cast<uint64_t>(repeated.second.failureExpected),64);
    ok &= setProperty("RomRepeatFailureActual",static_cast<uint64_t>(repeated.second.failureActual),64);
    ok &= setProperty("UploadAttempted",uploadAttempted);
    ok &= setProperty("UploadStatus",static_cast<unsigned>(transfer.status),32);
    ok &= setProperty("UploadOperationStatus",static_cast<unsigned>(transfer.operationStatus),32);
    ok &= setProperty("UploadFailedPhase",static_cast<unsigned>(transfer.failedPhase),32);
    ok &= setProperty("UploadSubmitted",transfer.submitted,32);
    ok &= setProperty("UploadPolls",transfer.polls,32);
    ok &= setProperty("UploadLastControl",transfer.lastControl,32);
    ok &= setProperty("UploadLastIndex",transfer.lastIndex,32);
    ok &= setProperty("UploadQuiesced",transfer.quiesced);
    ok &= setProperty("UploadReleaseProven",releaseProven);
    ok &= setProperty("UploadInterruptRestored",interruptRestored);
    ok &= setProperty("UploadPciStage",pciTransfer.stage,32);
    ok &= setProperty("UploadPciGate",pciTransfer.gate,32);
    ok &= setProperty("UploadPciBusy",static_cast<uint64_t>(pciTransfer.busy),64);
    ok &= setProperty("UploadPciIndex",static_cast<uint64_t>(pciTransfer.index),64);
    ok &= setProperty("UploadPciInit",static_cast<uint64_t>(pciTransfer.init),64);
    ok &= setProperty("UploadPciStop",static_cast<uint64_t>(pciTransfer.stop),64);
    ok &= setProperty("UploadPciHci",static_cast<uint64_t>(pciTransfer.hci),64);
    ok &= setProperty("UploadPciWrites",pciTransfer.writes,32);
    ok &= setProperty("UploadPciPolls",pciTransfer.polls,32);
    ok &= setProperty("UploadResetHci",pciTransfer.resetHci,32);
    ok &= setProperty("UploadResetControl",pciTransfer.resetControl,32);
    ok &= setProperty("UploadPollFailureReason",pciTransfer.pollFailureReason,32);
    ok &= setProperty("UploadPollFailureAddress",pciTransfer.pollFailureAddress,32);
    ok &= setProperty("UploadPollFailureMask",pciTransfer.pollFailureMask,32);
    ok &= setProperty("UploadPollFailureExpected",pciTransfer.pollFailureExpected,32);
    ok &= setProperty("UploadPollFailureActual",pciTransfer.pollFailureActual,32);
    ok &= setProperty("UploadPciIdle",pciTransfer.idle);
    ok &= setProperty("UploadPciMasterOff",pciTransfer.busMasterOff);
    ok &= setProperty("UploadPciRestored",pciTransfer.restored);
    ok &= setProperty("UploadPciFailureAddress",pciTransfer.failureAddress,32);
    ok &= setProperty("UploadPciFailureExpected",static_cast<uint64_t>(pciTransfer.failureExpected),64);
    ok &= setProperty("UploadPciFailureActual",static_cast<uint64_t>(pciTransfer.failureActual),64);
    ok &= setProperty("RomCleanupClock",static_cast<uint64_t>(rom.cleanupClock),64);
    ok &= setProperty("RomCleanupDmac",static_cast<uint64_t>(rom.cleanupDmac),64);
    ok &= setProperty("RomFailureRecorded",rom.failureRecorded);
    ok &= setProperty("RomFailureAddress",rom.failureAddress,32);
    ok &= setProperty("RomFailureMask",static_cast<uint64_t>(rom.failureMask),64);
    ok &= setProperty("RomFailureExpected",static_cast<uint64_t>(rom.failureExpected),64);
    ok &= setProperty("RomFailureActual",static_cast<uint64_t>(rom.failureActual),64);
    ok &= setProperty("SupplyStatus",rtl8852be::power::statusName(supply.status));
    ok &= setProperty("SupplyOnError",static_cast<unsigned>(supply.on.error),32);
    ok &= setProperty("SupplyOffError",static_cast<unsigned>(supply.off.error),32);
    ok &= setProperty("SupplyReturnedOff",supply.returnedOff);
    ok &= setProperty("SupplyActiveObserved",supply.activeObserved);
    ok &= setProperty("SupplyCleanupAttempted",supply.cleanupAttempted);
    ok &= setProperty("SupplyInitialState",supply.initialState,32);
    ok &= setProperty("SupplyActiveState",supply.activeState,32);
    ok &= setProperty("SupplyFinalState",supply.finalState,32);
    for (unsigned i = 0; i < 6; ++i) {
        char key[16]; snprintf(key, sizeof(key), "BAR%uRaw", i);
        ok &= setProperty(key, s.bars[i], 32);
    }
    if (!ok) { IOService::stop(provider); return false; }
    IOLog("RTL8852BEProbe 0.0.13: %s reads=%u cfg=%08x/%08x command=%04x/%04x/%04x; Wi-Fi unavailable\n",
          rtl8852be::statusName(r.status), r.reads, r.cfgFirst, r.cfgSecond,
          r.commandBefore, r.commandDuring, r.commandAfter);
    IOLog("RTL8852BEProbe XTAL: %s writes=%u polls=%u raw=%02x power=%08x/%08x\n",
          rtl8852be::xtalStatusName(x.status),x.writes,x.polls,x.rawRevision,x.powerBefore,x.powerAfter);
    IOLog("RTL8852BEProbe firmware bank: %s packets=%u pages=%u cleanup=%d; no DMA submitted\n",
          rtl8852be::transport::bankStatusName(bank.status),bank.packets,bank.prepared,bankCleanup);
    IOLog("RTL8852BEProbe ROM: %s phase=%u control=%08x cleanup=%d supply=%s\n",
          rtl8852be::boot::statusName(rom.status),rom.phase,rom.control,rom.cleanupOK,rtl8852be::power::statusName(supply.status));
    registerService();
    return true;
}
