// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOWorkLoop.h>
#include "FirmwareCapabilities.hpp"
#include "FirmwareCommands.hpp"
namespace rtl8852be { namespace firmware {class MacMailboxIo;} }
namespace rtl8852be { namespace network {
enum class ProbeFirmwareStage {idle,allocation,identity,probePower,probeDmac,probePci,probeCpu,probeDownload,
    calibration,probeCapabilities,probeStop,runtimePower,runtimeDmac,runtimePci,runtimeCpu,runtimeDownload,
    runtimeCapabilities,macRadio,macSystem,macDmac,macCmac,macReports,pciPost,prepared,stopped,failed};
enum class ProbeFirmwareError {none,ownership,allocation,identity,firmware,power,dma,preparation,pci,calibration,capabilities,mac,cleanup,cancelled};
struct ProbeFirmwareResult {
    ProbeFirmwareStage stage{},failedStage{};ProbeFirmwareError error{};
    uint8_t cut{};uint64_t epoch{};
    bool allocated{},prepared{},probeStopped{},cpuRunning{},stopped{},requiresRecovery{},downloadRetained{};
    // Explicit follow-on work; prepare() does not claim whole-driver readiness.
    bool earlyH2cPending=true,offloadConfigurationPending=true,radioCalibrationPending=true;
};
// Concrete two-cycle power/firmware service, not an abstract hardware backend.
// Device/map/workloop and immutable firmware bytes are borrowed through release().
// Caller owns exclusive PCI access; no runtime IRQ/timer callbacks may run during
// prepare/stop. All methods except allocate/release/destructor require the gate.
class MacProbeAndFirmware {
    struct State;State *state_{};ProbeFirmwareResult result_{};
    void *commands_{};void (*invalidateCommands_)(void *){};
    bool offloadSubmitted_{};uint8_t offloadSequence_{};
    static bool offloadCompleted(void *,const FirmwareEvent &,uint64_t,uint64_t);
    bool fail(ProbeFirmwareError);bool stopCycle(unsigned,bool);bool readCut(uint8_t &);
    bool startCycle(unsigned,const CalibrationSnapshot *);bool clearRings();
public:
    MacProbeAndFirmware()=default;~MacProbeAndFirmware();
    MacProbeAndFirmware(const MacProbeAndFirmware&)=delete;
    MacProbeAndFirmware &operator=(const MacProbeAndFirmware&)=delete;
    bool allocate(IOPCIDevice &,IOMemoryMap &,IOWorkLoop &,const uint8_t *,size_t);
    bool prepare();bool stop();void cancel();
    // After runtime CH12/RX dispatch starts; shared command owner MUST outlive
    // stop(). Completion uses the same bus/epoch, never a private H2C allocator.
    template<class Commands> bool beginFirmwareConfiguration(Commands &commands){
        if(!result_.prepared||offloadSubmitted_||commands.epoch()!=result_.epoch||!commands.service())return false;
        const uint8_t payload[8]={0x09,0,0,0,0x5e,0,0,0};
        commands_=&commands;invalidateCommands_=[](void *p){static_cast<Commands *>(p)->invalidate();};
        offloadSubmitted_=true;
        if(!commands.submit({1,9,0x14},false,true,payload,sizeof(payload),{this,offloadCompleted},result_.epoch,300000,offloadSequence_))
            return fail(ProbeFirmwareError::firmware);
        return true;
    }
    // Outside gate only, after successful stop or before any power attempt.
    // Refuses to destroy retained DMA / ambiguous live hardware state.
    bool release();
    const ProbeFirmwareResult &result()const{return result_;}
    const CalibrationSnapshot *calibration()const;
    const firmware::CapabilitySnapshot *capabilities()const;
    const firmware::Plan *firmwarePlan()const;
    firmware::Mailbox<firmware::MacMailboxIo> *mailbox();
    uint8_t cut()const{return result_.cut;}uint64_t epoch()const{return result_.epoch;}
};
} }
