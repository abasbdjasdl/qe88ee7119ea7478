// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "MacFirmwareBootIo.hpp"
#include "MacDmaBuffer.hpp"
#include "../FirmwareBank.hpp"
namespace rtl8852be { namespace firmwareboot {
// 256-entry download ring, independent from the 64-entry runtime rings. Source
// firmware and Packets stay immutable/alive until run returns. Allocate on heap.
class MacFirmwareDownload {
    MacFirmwareBootIo &io_;IOPCIDevice &device_;IOWorkLoop &loop_;
    network::MacDmaBuffer pages_[transport::maxPackets+1];unsigned count_{};
    bool allocated_{},visible_{},attempted_{};
    struct Bank;
    struct Backend;
    bool markVisible();bool releaseStopped();static bool releaseCallback(void *);
public:
    transport::BankResult bankResult{};transport::PciDownloadResult pciResult{};
    transport::TransferResult result{};
    MacFirmwareDownload(MacFirmwareBootIo &io,IOPCIDevice &device,IOWorkLoop &loop):io_(io),device_(device),loop_(loop){}
    MacFirmwareDownload(const MacFirmwareDownload&)=delete;MacFirmwareDownload &operator=(const MacFirmwareDownload&)=delete;
    bool allocate(const transport::Packets &); // outside workloop gate
    transport::TransferResult run(const transport::Packets &); // inside gate
    bool releaseUnpublished(); // never bypasses device-visible ownership
    bool retained()const{return visible_;}
};
} }
