// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Net80211PacketBridge.hpp"
#include "PciDataPath.hpp"
#include "PciRxAssembly.hpp"
#include "FirmwareProtocol.hpp"
#include "FirmwareDmaQueue.hpp"
namespace rtl8852be { namespace network {
// These mappings must be wired, contiguous, prepared for DMA and retained by
// the controller until DMA stop is proven. No physical addresses are invented.
struct DataMapping {uint8_t *bytes{};uint64_t physical{};size_t capacity{};};
struct TxPageMapping {DataMapping descriptor{},frame{};};
struct TxCounters {uint64_t completed{},acked{},retryLimit{},expired{},dropped{},polluted{},rejectedReports{};};
class Net80211PciQueue {
public:
    static constexpr size_t count=64;
private:
    ieee80211com *ic_{};
    DataMapping ring_{};
    TxPageMapping pages_[count]{};
    TxLease leases_[count]{};
    TxOwnership<count> ownership_{};
    TxCounters counters_{};
    uint8_t channel_{};
    void drainCompletions();
public:
    Net80211PciQueue()=default;
    Net80211PciQueue(const Net80211PciQueue &)=delete;
    Net80211PciQueue &operator=(const Net80211PciQueue &)=delete;
    // Initialize only with DMA stopped, before making the ring device-visible.
    // External mappings and ic must outlive this queue and all leases.
    int initialize(ieee80211com *ic,uint8_t channel,DataMapping ring,const TxPageMapping (&pages)[count]);
    // On success consumes lease and returns the producer to publish. Caller
    // must cache-sync frame/WD/BD, issue a DMA write barrier, then ring the
    // doorbell. If publishing fails, retain ownership until proven shutdown.
    // Descriptor policy (rate, MACID, sequence, port) comes from chip/sta state.
    int stage(TxLease &lease,TxInfo info,uint16_t &nextProducer);
    int consumeTo(uint16_t hardwareConsumer);
    int releaseReport(const ReleaseReport &report);
    bool full()const;
    size_t outstanding()const{return ownership_.outstanding();}
    const TxCounters &counters()const{return counters_;}
    // Only after interrupts are masked, workloop callbacks drained, bus master
    // disabled and DMA idle confirmed. Also drain stale RPQ before reinitializing.
    void reclaimAfterDmaStopped();
};
class Net80211FirmwareQueue {
public:
    static constexpr size_t count=64;
private:
    DataMapping ring_{},packets_[count]{};
    FirmwareDmaOwnership<count> ownership_{};
    uint64_t retired_{};
public:
    // Same stopped-DMA and mapping-lifetime contract as the data queue.
    int initialize(DataMapping ring,const DataMapping (&packets)[count]);
    int stage(const uint8_t *command,size_t bytes,uint16_t &nextProducer);
    int consumeTo(uint16_t hardwareConsumer);
    void reclaimAfterDmaStopped();
    uint64_t retired()const{return retired_;}
};
// RXQ and RPQ are separate channels; fragmented state must not be shared.
// Callbacks run synchronously under the controller gate and must not retain
// pointers into the assembler's scratch buffer.
struct ReceiveCallbacks {
    void *context{};
    int (*firmwareEvent)(void *,const FirmwareEvent &){};
    int (*phyReport)(void *,const RxPacket &){};
};
int receiveRxq(ieee80211com *ic,PciRxAssembly &assembly,const uint8_t *dma,size_t bytes,
               uint8_t channel,int rssi,const ReceiveCallbacks &callbacks);
int receiveRpq(PciRxAssembly &assembly,const uint8_t *dma,size_t bytes,
               Net80211PciQueue *const (&queues)[13]);
} }
