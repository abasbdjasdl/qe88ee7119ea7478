// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOLocks.h>
#include "MacFirmwareCommands.hpp"
#include "StationController.hpp"
#include "StationIoCore.hpp"
#include "WirelessSelection.hpp"
#include "WirelessStatus.hpp"
#include "AuthenticationEvents.hpp"
#include "NativeScanCache.hpp"
struct ieee80211com;
class IOEthernetInterface;
namespace rtl8852be { namespace network {
// Pointer/key-free observation of the most recent successfully applied link
// status. A native frontend polls from its own queue; this is not a callback or
// a lossless association-event stream. revision==0 means no publication yet.
struct MacLinkPublication {
    uint64_t revision{},epoch{},selectionGeneration{},changedAtUs{};
    UInt32 status{};
    bool record(bool applied,UInt32 nextStatus,uint64_t nextEpoch,
                uint64_t nextSelection,uint64_t nowUs){
        if(!applied)return false;
        if(revision&&status==nextStatus&&epoch==nextEpoch&&selectionGeneration==nextSelection)return false;
        if(!++revision)++revision;
        status=nextStatus;epoch=nextEpoch;selectionGeneration=nextSelection;changedAtUs=nowUs;
        return true;
    }
};
struct MacBootIdentity {
    uint64_t epoch{}; // New real firmware/RX incarnation, not an operation counter.
    station::Interface interface{};
    struct Channel { uint8_t number{}; bool fiveGhz{},passive{}; } channels[64]{};
    size_t channelCount{};
};
class MacNetworkBootSink {
public:
    virtual void stationActionComplete(station::Token,station::Action,bool)=0;
    virtual void bootFailed(const char *reason)=0;
    virtual ~MacNetworkBootSink()=default;
};
// Required real hardware implementation. There is deliberately no success stub.
// All methods except allocate/destructor run on the controller's workloop gate.
class MacNetworkBootService {
public:
    // Allocate/prepare download DMA outside the gate; no network DMA published yet.
    virtual bool allocate(IOPCIDevice&,IOMemoryMap&,IOWorkLoop&)=0;
    // Power + MAC + firmware download. Return with firmware alive, host DMA idle,
    // bus master disabled and the 9 runtime ring registers reset to zero.
    virtual bool prepare(MacBootIdentity&)=0;
    // Runtime MSI and CH12 are now alive; perform asynchronous RFK/BT setup.
    virtual bool start(NativeFirmwareCommands&,MacNetworkBootSink&)=0;
    virtual bool service(uint64_t nowUs)=0;
    virtual bool ready()const=0;
    // Never recursively invoke sink in beginAction. Completion is deferred to service/C2H.
    virtual bool beginAction(const station::ActionRequest&,const MacProtocolPeer&)=0;
    virtual bool setTraffic(station::Traffic)=0;
    virtual bool txInfo(const TxLease&,TxInfo&,unsigned &runtimeRing)=0;
    // Actual tuned channel and most recent decoded PHY RSSI; false drops data only,
    // and MUST NOT prevent C2H processing during bootstrap.
    virtual bool rxInfo(uint8_t &channel,int &rssi)=0;
    // Optional actual decoded PHY dBm. No percent-to-dBm conversion fallback.
    virtual bool rxSignalDbm(int &out){out=0;return false;}
    virtual int phyReport(const RxPacket&)=0;
    virtual int notification(const FirmwareEvent&)=0;
    // Called after runtime IRQ/DMA stop attempt. Must silence CPU/RF; return false
    // if download/hardware resources must remain retained. Must cancel callbacks.
    virtual bool stop()=0;
    virtual ~MacNetworkBootService()=default;
};
} }
class R16NetworkController : public IOEthernetController {
    OSDeclareDefaultStructors(R16NetworkController)
    struct State; State *state_{};
    IOWorkLoop *loop_{};IOCommandGate *gate_{};IOTimerEventSource *timer_{};
    IOPCIDevice *pci_{};IOMemoryMap *bar_{};IOEthernetInterface *interface_{};
    bool providerOpened_{},providerRetained_{},superStarted_{},retainedFault_{};
    IOLock *controlLock_{};
    bool controlStopping_{true};
    IOReturn runControlAction(IOCommandGate::Action,void* = nullptr);
    void blockControlRequests();
    static IOReturn startGated(OSObject*,void*,void*,void*,void*);
    static IOReturn stopGated(OSObject*,void*,void*,void*,void*);
    static IOReturn enableGated(OSObject*,void*,void*,void*,void*);
    static IOReturn outputGated(OSObject*,void*,void*,void*,void*);
    static IOReturn selectionGated(OSObject*,void*,void*,void*,void*);
    static IOReturn wirelessStatusGated(OSObject*,void*,void*,void*,void*);
    static IOReturn linkStatusGated(OSObject*,void*,void*,void*,void*);
    static IOReturn linkPublicationGated(OSObject*,void*,void*,void*,void*);
    static IOReturn authenticationGated(OSObject*,void*,void*,void*,void*);
    static IOReturn nativeScanGated(OSObject*,void*,void*,void*,void*);
    bool applyLinkStatus(UInt32,const IONetworkMedium*,UInt64,OSData*);
    static void timer(OSObject*,IOTimerEventSource*);
    void releaseResources();
    unsigned startupStage_{};
    void recordStartup(IOService *,unsigned,bool failed=false);
protected:
    // Override in the hardware personality. Null causes a visible start failure.
    virtual rtl8852be::network::MacNetworkBootService *createBootService(){return nullptr;}
public:
    IOReturn newUserClient(task_t,void*,UInt32,OSDictionary*,IOUserClient**) override;
    bool start(IOService*) override;
    void stop(IOService*) override;
    void free() override;
    bool createWorkLoop() override;
    IOWorkLoop *getWorkLoop() const override;
    bool configureInterface(IONetworkInterface*) override;
    IOReturn getHardwareAddress(IOEthernetAddress*) override;
    IOReturn selectMedium(const IONetworkMedium*) override;
    IOReturn getPacketFilters(const OSSymbol*,UInt32*) const override;
    IOReturn enable(IONetworkInterface*) override;
    IOReturn disable(IONetworkInterface*) override;
    UInt32 outputPacket(mbuf_t,void*) override;
    // State transitions use virtual dispatch. A derived frontend must chain to
    // this implementation and defer framework notifications outside the gate.
    bool setLinkStatus(UInt32,const IONetworkMedium * = nullptr,UInt64 = 0,OSData * = nullptr) override;
    // Kernel-side native UI adapter entry points. Success means queued only.
    // They never publish credentials through IORegistry or a property setter.
    IOReturn selectWirelessNetwork(const rtl8852be::network::selection::Join&);
    IOReturn disconnectWirelessNetwork();
    IOReturn copyWirelessStatus(rtl8852be::network::wireless::Snapshot&);
    // Same external-call/stop fence as copyWirelessStatus. Never call while
    // holding the hardware gate. Failure clears out; stop rejects new readers.
    IOReturn copyLinkPublication(rtl8852be::network::MacLinkPublication&);
    // Passive snapshots of an existing net80211 mode/pass, not a new scan or
    // proof of complete coverage of both bands. External kernel callers only;
    // controlLock -> gate, never call while holding the hardware gate. Entry
    // storage belongs to the caller (prefer heap); no borrowed cache pointers.
    // Read summary first, then pass its exact epoch/generation with every index.
    // A replaced publication makes old tokens fail. Failure clears output.
    IOReturn copyNativeScanSummary(rtl8852be::network::nativescan::Summary&);
    IOReturn copyNativeScanEntry(rtl8852be::network::nativescan::Token,size_t,
                                rtl8852be::network::nativescan::Entry&);
    IOReturn copyNativeScanChannel(rtl8852be::network::nativescan::Token,size_t,
                                  rtl8852be::network::nativescan::Channel&);
    IOReturn authenticationEvents(void *client,uint32_t operation,rtl8852be::network::authevents::Event* = nullptr);
};
