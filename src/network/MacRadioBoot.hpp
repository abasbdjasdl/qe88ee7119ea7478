// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "MacFirmwareCommands.hpp"
#include "MacFirmwareMailboxIo.hpp"
#include "FirmwareCapabilities.hpp"
#include "ChannelGeometry.hpp"
#include "TxPowerPlan.hpp"
#include "RadioBootSequence.hpp"
#include "StationController.hpp"
namespace rtl8852be { namespace network {
// One instance per physically verified firmware epoch. All operational entry
// points use the owner's gate; allocate/release are outside the interrupt path.
class MacRadioBoot {
    struct State;State *state_{};
public:
    MacRadioBoot()=default;~MacRadioBoot();
    MacRadioBoot(const MacRadioBoot&)=delete;MacRadioBoot&operator=(const MacRadioBoot&)=delete;
    bool allocate(IOPCIDevice &,IOMemoryMap &,IOWorkLoop &,const CalibrationSnapshot &,
                  const firmware::CapabilitySnapshot &,const firmware::Plan &,
                  NativeFirmwareCommands &,firmware::Mailbox<firmware::MacMailboxIo> &);
    bool begin();bool service(uint64_t now);
    bool ready()const;bool busy()const;bool tuned()const;
    const radioboot::Result *result()const;
    bool tune(channel::Channel,power::Policy,bool scanning=false);
    bool beginScan();bool endScan();
    // Scheduler masks are absolute and acknowledged. Host admission still
    // distinguishes EAPOL from ordinary BE data. No queues open during RFK.
    bool setTraffic(uint16_t mask);bool setTraffic(station::Traffic);bool schedulerPaused();
    // Root still owns mutually exclusive station register-window access.
    bool stationWindowOwned();
    bool stop();
    // Only after command callbacks are invalidated; does not assert DMA stop.
    bool release();
};
} }
