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
    // Kind::channel must keep scheduler TX paused even on success: finishing
    // channel registers/receivers is not proof of power or RFK readiness.
    bool (*end)(void *,Kind,bool success){};
    // IQK needs a nested per-path oneshot notification in addition to its lease.
    bool (*oneshot)(void *,Kind,u8 phyMap,bool start){};
    // On partially programmed calibration failure, stop/reset the calibration
    // engines and RF hardware while retaining BT/TX ownership. Return true only
    // after verifying quiescence/reset. This is not the ordinary release hook.
    bool (*recover)(void *,Kind){};
};
// Device, mapping and callback owner are borrowed and must outlive this adapter
// and any outstanding lease. All calls run on the controller's serialized lane.
class MacRfkIo {
    IOPCIDevice *device_{};IOMemoryMap *mapping_{};
    network::MacRadioIo radioIo_;network::RadioAccess<network::MacRadioIo> radio_;
    CalibrationControl control_;bool active_{},oneshotActive_{},txArmed_{},modified_{};Kind kind_{};u8 oneshotMap_{};
    bool macRead(uint32_t,uint32_t &);bool accessible()const;
public:
    MacRfkIo(IOPCIDevice *,IOMemoryMap *,CalibrationControl);
    bool valid()const{return radioIo_.valid()&&device_&&mapping_;}
    bool leaseActive()const{return active_;}
    void cancel(){radioIo_.cancel();}bool cancelled()const{return radioIo_.cancelled();}
    uint64_t nowUs();bool delayUs(unsigned);
    bool begin(Kind);bool end(Kind,bool);bool drain();
    bool oneshot(Kind,u8 phyMap,bool start);
    bool armCalibrationTx();bool stopCalibrationTx();bool calibrationTxArmed()const{return txArmed_;}
    bool readRf(u8,u32,u32,u32 &);bool writeRf(u8,u32,u32,u32);
    bool readBb(u32,u32 &);bool writeBb(u32,u32);bool writeMac(u32,u32);
    // Narrow channel MAC interface, available only inside Kind::channel.
    bool readChannelMac8(u32,u8 &);bool writeChannelMac8(u32,u8);
    bool readChannelMac32(u32,u32 &);bool writeChannelMac32(u32,u32);
    bool readPowerMac32(u32,u32 &);bool writePowerMac32(u32,u32);
};
extern template class Initialization<MacRfkIo>;
} }
#include "ChannelProgramming.hpp"
extern template class rtl8852be::channel::ChannelProgramming<rtl8852be::rfk::MacRfkIo>;
