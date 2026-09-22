// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOWorkLoop.h>
#include "StationTables.hpp"
#include "StationIoCore.hpp"
#include "Net80211PacketBridge.hpp"
namespace rtl8852be { namespace network {
struct MacStationAuthority {
    void *owner{};bool (*schedulerPaused)(void*){};bool (*stationWindowOwned)(void*){};
};
struct MacPortConfig {uint8_t port{};bool connected{};uint16_t beaconInterval{};uint8_t dtimPeriod{};};
enum class MacPortState {idle,waiting,complete,fault};
// Actual checked BAR2 IO. Borrowed provider/map/workloop/authority must outlive it.
// CAM MAC/BSSID/AID fields are written by NativeProgrammer, not invented MMIO.
class MacStationIo {
    IOPCIDevice *device_{};IOMemoryMap *map_{};IOWorkLoop *loop_{};MacStationAuthority authority_{};
    MacPortConfig port_{};MacPortState portState_{MacPortState::idle};
    uint64_t waitUntil_{},portDeadline_{},lastNow_{};bool previouslyEnabled_{},seedActive_{},faulted_{},scanSaved_{};
    uint32_t selectedWindow_{},scanFilter_{};
    bool valid()const;bool safe();bool read(uint32_t,uint32_t&);bool write(uint32_t,uint32_t);
    bool masked(uint32_t,uint32_t,uint32_t);bool byteMasked(uint32_t,uint8_t,uint8_t);
    bool halfMasked(uint32_t,uint16_t,uint16_t);bool portProgram();
    bool fault(){faulted_=true;portState_=MacPortState::fault;return false;}
public:
    MacStationIo(IOPCIDevice&,IOMemoryMap&,IOWorkLoop&,MacStationAuthority);
    bool inGate()const;bool stationTableWindowOwned();bool schedulerPaused();uint64_t nowUs();
    // Restricted seedMacTables adapter, no arbitrary register caller access.
    bool write32(uint32_t,uint32_t);bool drainWrites();
    station::tables::SeedResult seed(uint8_t macid);
    // Two-phase source wait when replacing a live port; never sleeps for a beacon interval.
    bool beginPort(MacPortConfig);MacPortState servicePort();
    MacPortState portState()const{return portState_;}bool faulted()const{return faulted_;}
    bool scanFilter(bool enable); // preserve RX MPDU length and restore exact saved config
    bool txInfo(const TxLease&,uint8_t macid,channel::Channel,uint16_t basicRates,TxInfo&,unsigned&);
    static bool phyReport(const RxPacket &packet,stationio::PhySample &sample){return stationio::phySample(packet,sample);}
};
} }
