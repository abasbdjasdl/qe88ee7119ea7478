// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "MacRadioIo.hpp"
#include "RfkInitialization.hpp"
namespace rtl8852be { namespace rfk {
struct CalibrationControl {
    void *owner{};
    // Must coordinate firmware/BT, stop scheduler TX via firmware's acknowledged
    // pause protocol, and serialize all radio/channel changes. Never optional.
    bool (*begin)(void *,Kind){};
    // Release bookkeeping/BT coordination; success=false must keep TX stopped
    // until the controller power-cycles the partially calibrated device.
    bool (*end)(void *,Kind,bool success){};
    // IQK needs a nested per-path oneshot notification in addition to its lease.
    bool (*oneshot)(void *,Kind,u8 phyMap,bool start){};
};
// Device, mapping and callback owner are borrowed and must outlive this adapter
// and any outstanding lease. All calls run on the controller's serialized lane.
class MacRfkIo {
    IOPCIDevice *device_{};IOMemoryMap *mapping_{};
    network::MacRadioIo radioIo_;network::RadioAccess<network::MacRadioIo> radio_;
    CalibrationControl control_;bool active_{},oneshotActive_{};Kind kind_{};u8 oneshotMap_{};
    bool macRead(uint32_t,uint32_t &);bool accessible()const;
public:
    MacRfkIo(IOPCIDevice *,IOMemoryMap *,CalibrationControl);
    bool valid()const{return radioIo_.valid()&&device_&&mapping_;}
    bool leaseActive()const{return active_;}
    void cancel(){radioIo_.cancel();}bool cancelled()const{return radioIo_.cancelled();}
    uint64_t nowUs();bool delayUs(unsigned);
    bool begin(Kind);bool end(Kind,bool);bool drain();
    bool oneshot(Kind,u8 phyMap,bool start);
    bool readRf(u8,u32,u32,u32 &);bool writeRf(u8,u32,u32,u32);
    bool readBb(u32,u32 &);bool writeBb(u32,u32);bool writeMac(u32,u32);
};
extern template class Initialization<MacRfkIo>;
} }
