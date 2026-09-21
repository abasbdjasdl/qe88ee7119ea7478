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
#include "KernelDma.hpp"
#include "PowerSequence.hpp"
#include "RingProbe.hpp"

class PciMmioAccess {
    IOPCIDevice *pci;
    IODeviceMemory *memory;
    IOMemoryMap *mapping{};
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
    bool ringWrite32(uint32_t offset,uint32_t value) {
        if(!mapping || !rtl8852be::ring::allowed32(offset) || (command()&6)!=2)return false;
        OSWriteLittleInt32(reinterpret_cast<volatile void *>(mapping->getVirtualAddress()),offset,value);
        return true;
    }
    bool ringWrite16(uint32_t offset,uint16_t value) {
        if(!mapping || !rtl8852be::ring::allowed16(offset) || (command()&6)!=2)return false;
        OSWriteLittleInt16(reinterpret_cast<volatile void *>(mapping->getVirtualAddress()),offset,value);
        return true;
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
    rtl8852be::ring::Result queue;
    rtl8852be::dma::Result dmaResult;
    bool deviceMapper=false;
    {
        KernelDma buffers(pci);
        deviceMapper=buffers.deviceMapper();
        dmaResult=rtl8852be::dma::probe(buffers,[&](KernelDma &,const rtl8852be::dma::Result &prepared){
            // Buffers remain owned until ring restoration, supply shutdown and
            // MMIO unmapping have all returned. PCI bus mastering stays off.
            r=rtl8852be::sampleMmio(access,s,[&](PciMmioAccess &d,const rtl8852be::MmioResult &base){
                x=rtl8852be::sampleXtal(d,base);
                supply=rtl8852be::power::cycle(d,base,x,[&](PciMmioAccess &active){
                    queue=rtl8852be::ring::probe(active,prepared.ring);
                });
            });
        });
    }
    const auto memoryCount = pci->getDeviceMemoryCount();
    pci->close(this);
    bool ok = setProperty("DiagnosticOnly", true);
    ok &= setProperty("WiFiOperational", false);
    ok &= setProperty("DriverVersion", "0.0.7");
    ok &= setProperty("Experiment", "FWCMD-RING-CONFIG-01");
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
    ok &= setProperty("FirmwareUploaded", false);
    ok &= setProperty("DmaStatus", rtl8852be::dma::statusName(dmaResult.status));
    ok &= setProperty("DmaOperationStatus", rtl8852be::dma::statusName(dmaResult.operationStatus));
    ok &= setProperty("DmaDeviceMapper", deviceMapper);
    ok &= setProperty("DmaAllocations", dmaResult.allocations,32);
    ok &= setProperty("DmaPrepared", dmaResult.prepared,32);
    ok &= setProperty("DmaSynchronized", dmaResult.synchronized,32);
    ok &= setProperty("DmaFailedBuffer", dmaResult.failedBuffer,32);
    ok &= setProperty("DmaOSReturn", static_cast<uint64_t>(dmaResult.osError),64);
    ok &= setProperty("DmaCleanupOK", dmaResult.cleanupOk);
    ok &= setProperty("DmaCPUVerified", dmaResult.cpuVerified);
    ok &= setProperty("DmaBusMasterBefore", dmaResult.busMasterBefore);
    ok &= setProperty("DmaBusMasterAfter", dmaResult.busMasterAfter);
    ok &= setProperty("DmaRingAddress", dmaResult.ring.address,64);
    ok &= setProperty("DmaRingLength", dmaResult.ring.length,64);
    ok &= setProperty("DmaRingSegments", dmaResult.ring.segments,32);
    ok &= setProperty("DmaPacketAddress", dmaResult.packet.address,64);
    ok &= setProperty("DmaPacketLength", dmaResult.packet.length,64);
    ok &= setProperty("DmaPacketSegments", dmaResult.packet.segments,32);
    ok &= setProperty("DmaSubmittedToHardware", false);
    ok &= setProperty("SupplyStatus",rtl8852be::power::statusName(supply.status));
    ok &= setProperty("SupplyOnError",static_cast<unsigned>(supply.on.error),32);
    ok &= setProperty("SupplyOffError",static_cast<unsigned>(supply.off.error),32);
    ok &= setProperty("SupplyReturnedOff",supply.returnedOff);
    ok &= setProperty("SupplyActiveObserved",supply.activeObserved);
    ok &= setProperty("SupplyCleanupAttempted",supply.cleanupAttempted);
    ok &= setProperty("SupplyInitialState",supply.initialState,32);
    ok &= setProperty("SupplyActiveState",supply.activeState,32);
    ok &= setProperty("SupplyFinalState",supply.finalState,32);
    ok &= setProperty("RingStatus",rtl8852be::ring::statusName(queue.status));
    ok &= setProperty("RingOperationStatus",rtl8852be::ring::statusName(queue.operationStatus));
    ok &= setProperty("RingAttempted",queue.attempted);
    ok &= setProperty("RingReadbackOK",queue.readbackOK);
    ok &= setProperty("RingRestored",queue.restored);
    ok &= setProperty("RingWriteAttempts",queue.writeAttempts,32);
    ok &= setProperty("RingRestoreAttempts",queue.restoreAttempts,32);
    ok &= setProperty("RingBusMasterAfter",queue.busMasterAfter);
    ok &= setProperty("RingDoorbellWritten",false);
    const rtl8852be::ring::Snapshot snapshots[]={queue.before,queue.configured,queue.after};
    const char *phases[]={"Before","Configured","After"};
    for(unsigned i=0;i<3;++i){
        const auto &snap=snapshots[i];
        const uint32_t values[]={snap.init,snap.stop,snap.busy,snap.index,snap.low,snap.high,snap.ram,snap.num};
        const char *names[]={"Init","Stop","Busy","Index","Low","High","Ram","Count"};
        for(unsigned j=0;j<8;++j){
            char key[48];snprintf(key,sizeof(key),"Ring%s%s",phases[i],names[j]);
            ok &= setProperty(key,static_cast<uint64_t>(values[j]),64);
        }
    }
    for (unsigned i = 0; i < 6; ++i) {
        char key[16]; snprintf(key, sizeof(key), "BAR%uRaw", i);
        ok &= setProperty(key, s.bars[i], 32);
    }
    if (!ok) { IOService::stop(provider); return false; }
    IOLog("RTL8852BEProbe 0.0.7: %s reads=%u cfg=%08x/%08x command=%04x/%04x/%04x; Wi-Fi unavailable\n",
          rtl8852be::statusName(r.status), r.reads, r.cfgFirst, r.cfgSecond,
          r.commandBefore, r.commandDuring, r.commandAfter);
    IOLog("RTL8852BEProbe XTAL: %s writes=%u polls=%u raw=%02x power=%08x/%08x\n",
          rtl8852be::xtalStatusName(x.status),x.writes,x.polls,x.rawRevision,x.powerBefore,x.powerAfter);
    IOLog("RTL8852BEProbe DMA memory: %s allocations=%u prepared=%u cleanup=%d; nothing submitted\n",
          rtl8852be::dma::statusName(dmaResult.status),dmaResult.allocations,dmaResult.prepared,dmaResult.cleanupOk);
    IOLog("RTL8852BEProbe ring: %s restored=%d supply=%s; no doorbell or DMA\n",
          rtl8852be::ring::statusName(queue.status),queue.restored,rtl8852be::power::statusName(supply.status));
    registerService();
    return true;
}
