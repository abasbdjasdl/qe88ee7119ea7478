// SPDX-License-Identifier: BSD-3-Clause
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IONVRAM.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include "PciConfig.hpp"
#include "ReportBuffer.hpp"

// One bounded report for this test build; never touches boot variables.
static constexpr const char *reportKey = "RTL8852BE-AutoReport";
static constexpr const char *testToken = "r16-auto-20260921-01";

// Phase 0 diagnostic service. No IOEthernetInterface or IO80211Interface is
// published: firmware, DMA, RF and association are not implemented yet.
class RTL8852BEProbe : public IOService {
    OSDeclareDefaultStructors(RTL8852BEProbe)
public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;
private:
    uint8_t capturedConfig[rtl8852be::configSize]{};
    IOWorkLoop *reportLoop{};
    IOTimerEventSource *reportTimer{};
    unsigned reportAttempts{};
    bool timerAdded{};
    static void onReportTimer(OSObject *owner, IOTimerEventSource *timer);
    bool saveReport();
    void cleanupReportTimer();
};
OSDefineMetaClassAndStructors(RTL8852BEProbe, IOService)

IOService *RTL8852BEProbe::probe(IOService *provider, SInt32 *score) {
    auto *pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci || pci->configRead16(kIOPCIConfigVendorID) != rtl8852be::vendor ||
        pci->configRead16(kIOPCIConfigDeviceID) != rtl8852be::device)
        return nullptr;
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
    const auto memoryCount = pci->getDeviceMemoryCount();
    memcpy(capturedConfig, config, sizeof(config));
    pci->close(this);

    rtl8852be::Snapshot s{};
    if (!rtl8852be::decode(config, sizeof(config), s) || !s.isTarget()) {
        IOService::stop(provider); return false;
    }
    bool ok = setProperty("DiagnosticOnly", true);
    ok &= setProperty("WiFiOperational", false);
    ok &= setProperty("Stage", "PCI configuration captured; no radio initialization");
    ok &= setProperty("VendorID", s.vendorID, 16);
    ok &= setProperty("DeviceID", s.deviceID, 16);
    ok &= setProperty("SubsystemVendorID", s.subsystemVendor, 16);
    ok &= setProperty("SubsystemDeviceID", s.subsystemDevice, 16);
    ok &= setProperty("RevisionID", s.revision, 8);
    ok &= setProperty("PCICommandBeforeProbe", s.command, 16);
    ok &= setProperty("MemoryResourceCount", memoryCount, 32);
    ok &= setProperty("CapabilityParseStatus", static_cast<unsigned>(s.capStatus), 32);
    ok &= setProperty("CapabilityCount", s.capabilityCount, 32);
    ok &= setProperty("HasPM", s.hasCapability(0x01));
    ok &= setProperty("HasMSI", s.hasCapability(0x05));
    ok &= setProperty("HasPCIExpress", s.hasCapability(0x10));
    ok &= setProperty("HasMSIX", s.hasCapability(0x11));
    for (unsigned i = 0; i < 6; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "BAR%uRaw", i);
        ok &= setProperty(key, s.bars[i], 32);
    }
    if (!ok) { IOService::stop(provider); return false; }
    IOLog("RTL8852BEProbe: PCI %04x:%04x subsystem %04x:%04x rev %02x; diagnostic only, Wi-Fi unavailable\n",
          s.vendorID, s.deviceID, s.subsystemVendor, s.subsystemDevice, s.revision);
    registerService();
    reportLoop = IOWorkLoop::workLoop();
    reportTimer = IOTimerEventSource::timerEventSource(this, onReportTimer);
    if (reportLoop && reportTimer && reportLoop->addEventSource(reportTimer) == kIOReturnSuccess) {
        timerAdded = true;
        reportTimer->setTimeoutMS(60000);
        setProperty("AutoReportScheduled", true);
    } else {
        cleanupReportTimer();
        setProperty("AutoReportScheduled", false);
    }
    return true;
}

void RTL8852BEProbe::cleanupReportTimer() {
    if (reportTimer) {
        reportTimer->cancelTimeout();
        if (reportLoop && timerAdded) reportLoop->removeEventSource(reportTimer);
        reportTimer->release();
        reportTimer = nullptr;
    }
    timerAdded = false;
    if (reportLoop) { reportLoop->release(); reportLoop = nullptr; }
}

void RTL8852BEProbe::stop(IOService *provider) {
    cleanupReportTimer();
    IOService::stop(provider);
}

void RTL8852BEProbe::free() {
    cleanupReportTimer();
    IOService::free();
}

void RTL8852BEProbe::onReportTimer(OSObject *owner, IOTimerEventSource *timer) {
    auto *self = OSDynamicCast(RTL8852BEProbe, owner);
    if (!self) return;
    ++self->reportAttempts;
    if (!self->saveReport() && self->reportAttempts < 5) timer->setTimeoutMS(15000);
}

bool RTL8852BEProbe::saveReport() {
    auto *entry = IORegistryEntry::fromPath("/options", gIODTPlane);
    if (!entry) return false;
    auto *nvram = OSDynamicCast(IODTNVRAM, entry);
    if (!nvram) { entry->release(); return false; }
    // Skip all later boots of this same build after a successful report.
    auto *existingObject = entry->copyProperty(reportKey);
    auto *existing = OSDynamicCast(OSData, existingObject);
    bool alreadySaved = false;
    if (existing && existing->getLength() >= strlen(testToken))
        alreadySaved = memcmp(existing->getBytesNoCopy(), testToken, strlen(testToken)) == 0;
    if (existingObject) existingObject->release();
    if (alreadySaved) { entry->release(); return true; }
    if (!nvram->safeToSync()) { entry->release(); return false; }

    rtl8852be::ReportBuffer<2048> report;
    report.append(testToken);
    report.append("\nschema=1\ndriver=0.0.2\nsource=macOS-kernel\nprobe=matched\nwifi=not-operational\npci256=");
    report.hex(capturedConfig, sizeof(capturedConfig));
    report.append("\n");
    const char *classes[] = {"ApplePS2Keyboard", "ApplePS2Controller", "IONVMeController", "IOMedia"};
    for (const char *className : classes) {
        auto *matching = IOService::serviceMatching(className);
        if (!matching) continue;
        auto *iterator = IOService::getMatchingServices(matching);
        matching->release();
        unsigned count = 0;
        if (iterator) {
            while (auto *object = iterator->getNextObject()) {
                ++count;
                if (!strcmp(className, "IOMedia") && count <= 8) {
                    auto *media = OSDynamicCast(IORegistryEntry, object);
                    if (!media) continue;
                    auto *nameObject = media->copyProperty("BSD Name");
                    auto *uuidObject = media->copyProperty("UUID");
                    auto *sizeObject = media->copyProperty("Size");
                    auto *name = OSDynamicCast(OSString, nameObject);
                    auto *uuid = OSDynamicCast(OSString, uuidObject);
                    auto *size = OSDynamicCast(OSNumber, sizeObject);
                    char line[128];
                    snprintf(line, sizeof(line), "media=%.16s,size=%llu,uuid=%.36s\n",
                             name ? name->getCStringNoCopy() : "?",
                             size ? size->unsigned64BitValue() : 0ULL,
                             uuid ? uuid->getCStringNoCopy() : "?");
                    report.append(line);
                    if (nameObject) nameObject->release();
                    if (uuidObject) uuidObject->release();
                    if (sizeObject) sizeObject->release();
                }
                if (count >= 128) break;
            }
            iterator->release();
        }
        char line[96];
        snprintf(line, sizeof(line), "services.%s=%u\n", className, count);
        report.append(line);
    }
    report.append("END\n");
    if (report.clipped) { entry->release(); return false; }
    auto *data = OSData::withBytes(report.bytes, static_cast<unsigned>(report.used));
    bool saved = data && entry->setProperty(reportKey, data);
    if (data) data->release();
    if (saved) nvram->sync();
    entry->release();
    setProperty("AutoReportSaved", saved);
    IOLog("RTL8852BEProbe: automatic NVRAM report %s; %lu bytes\n", saved ? "submitted" : "failed", report.used);
    return saved;
}
