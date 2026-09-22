// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/IOWorkLoop.h>
#include "MacRadioIo.hpp"
#include "FirmwareCapabilities.hpp"
#include "RfkInitialization.hpp"
namespace rtl8852be { namespace network {
enum class PhyInitStage {idle,powerUnit,reset,beforeRfk,afterRfk};
enum class PhyInitError {none,order,identity,ownership,cancelled,io,readback,clock,timeout};
struct PhyInitResult {
    PhyInitStage stage{};PhyInitError error{};uint32_t address{},expected{},actual{};unsigned operations{};
    bool powerUnitReady{},resetDone{},beforeRfkDone{},powerReferenceReady{},afterRfkDone{},requiresReset{};
    int8_t offsetBase{},rssiBase{};uint8_t crystalCap{},thermal[2]{};bool thermalPresent[2]{},defaultBandedge{};
};
// Real 8852B-only initial PHY programming. Mandatory guard proves exclusive
// scheduler/BT/radio ownership before/after MMIO and during RF/SI polling.
// Does not tune a channel, apply regulatory power limits or perform RFK.
class MacPhyInitialization {
    IOPCIDevice *device_{};IOMemoryMap *map_{};IOWorkLoop *loop_{};RadioAccessGuard guard_{};
    MacRadioIo radioIo_;RadioAccess<MacRadioIo> radio_;
    bool cancelled_{},clockStarted_{};uint64_t first_{},previous_{};unsigned phaseOps_{};
    PhyInitResult result_{};BoardCalibration board_{};PhyCalibration phy_{};firmware::CapabilitySnapshot caps_{};
    bool fail(PhyInitError,uint32_t=0,uint32_t=0,uint32_t=0);bool check();bool enter(PhyInitStage);
    bool read(uint32_t,uint32_t &);bool write(uint32_t,uint32_t);bool update(uint32_t,uint32_t,uint32_t,bool bb=true);
    bool delay(unsigned);bool siIdle();bool crystal(uint8_t,uint8_t);bool bbReset();bool gainOffset(uint8_t);
    bool environment();bool phyStatus();bool dig();bool cfo();bool receivePaths(rfk::Channel);
public:
    MacPhyInitialization(IOPCIDevice *,IOMemoryMap *,IOWorkLoop *,RadioAccessGuard);
    MacPhyInitialization(const MacPhyInitialization&)=delete;MacPhyInitialization&operator=(const MacPhyInitialization&)=delete;
    bool valid()const{return device_&&map_&&loop_&&guard_.owner&&guard_.check&&radioIo_.valid();}
    void cancel(){cancelled_=true;radioIo_.cancel();}
    const PhyInitResult &result()const{return result_;}
    bool powerUnit(); // after BB register table, before gain table
    bool reset(); // after gain table
    bool beforeRfk(const CalibrationSnapshot &,const firmware::CapabilitySnapshot &); // after RF tables
    bool powerReference(); // after initial RFK, BEFORE source powerTrim
    // AFTER powerTrim. Initial source entity is {2GHz,20MHz,1}; this programs
    // band/path prerequisites only, never declares a channel tuned or permitted.
    bool afterRfk(rfk::Channel);
    bool receivePath(rfk::Channel channel){return afterRfk(channel);}
};
} }
