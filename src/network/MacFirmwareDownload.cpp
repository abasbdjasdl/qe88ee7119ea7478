// SPDX-License-Identifier: BSD-3-Clause
#include "MacFirmwareDownload.hpp"
namespace rtl8852be { namespace firmwareboot {
struct MacFirmwareDownload::Bank {
    MacFirmwareDownload &owner;
    uint16_t command(){return owner.io_.command();}
    // All allocation and prepare work was completed outside the gate. The
    // existing packet-bank encoder only checks/uses these prepared mappings.
    bool allocate(unsigned i){return i<=owner.count_&&owner.pages_[i].mapping().bytes;}
    bool prepare(unsigned i){return allocate(i);}
    dma::Mapping mapping(unsigned i){const auto m=owner.pages_[i].mapping();return {m.physical,m.capacity,m.capacity,1};}
    uint8_t *bytes(unsigned i){return owner.pages_[i].mapping().bytes;}
    bool synchronize(unsigned i){return owner.pages_[i].syncForDevice();}
};
struct MacFirmwareDownload::Backend : transport::PciDownload<MacFirmwareBootIo> {
    MacFirmwareDownload &owner;
    explicit Backend(MacFirmwareDownload &o):transport::PciDownload<MacFirmwareBootIo>(o.io_,Bank{o}.mapping(0),o.count_,true,releaseCallback,&o),owner(o){}
    bool cancelled(){return owner.io_.cancelled();}
    bool startDownload(){return owner.markVisible()&&transport::PciDownload<MacFirmwareBootIo>::startDownload();}
    bool publish(unsigned n){return !cancelled()&&transport::PciDownload<MacFirmwareBootIo>::publish(n);}
    bool quiesceAndProveIdle(){
        owner.io_.beginCleanup();const bool ok=transport::PciDownload<MacFirmwareBootIo>::quiesceAndProveIdle();owner.io_.endCleanup();return ok;
    }
};
bool MacFirmwareDownload::allocate(const transport::Packets &packets){
    if(allocated_||attempted_||visible_||loop_.inGate()||!packets.count()||packets.count()>transport::maxPackets)return false;
    count_=packets.count();
    for(unsigned i=0;i<=count_;++i)if(!pages_[i].allocate(&device_,&loop_,4096,4096)){releaseUnpublished();return false;}
    allocated_=true;return true;
}
bool MacFirmwareDownload::markVisible(){
    if(!allocated_||!io_.inGate()||io_.cancelled())return false;
    // Mark before any address can be made accessible with PCI bus mastering.
    visible_=true;for(unsigned i=0;i<=count_;++i)if(!pages_[i].markDeviceVisible())return false;return true;
}
bool MacFirmwareDownload::releaseUnpublished(){
    if(visible_)return false;bool ok=true;
    for(unsigned i=0;i<=count_;++i)ok=pages_[i].release()&&ok;
    if(ok)allocated_=false;return ok;
}
bool MacFirmwareDownload::releaseStopped(){
    // Only PciDownload's confirmed idle + BM-off path calls this hook. It has
    // restored/cleared the download ring, so newest-eight retention ends here.
    const auto command=io_.command();if(command==0xffff||(command&6)!=2)return false;
    bool ok=true;for(unsigned i=0;i<=count_;++i)ok=pages_[i].releaseAfterDmaStopped()&&ok;
    if(ok){visible_=false;allocated_=false;}return ok;
}
bool MacFirmwareDownload::releaseCallback(void *p){return static_cast<MacFirmwareDownload *>(p)->releaseStopped();}
transport::TransferResult MacFirmwareDownload::run(const transport::Packets &packets){
    if(attempted_||!allocated_||!io_.inGate()||io_.cancelled()||packets.count()!=count_){
        result.status=transport::TransferStatus::prepareFailed;return result;}
    attempted_=true;Bank bank{*this};bankResult=transport::prepareBank(bank,packets);
    if(bankResult.status!=transport::BankStatus::ready){result.status=transport::TransferStatus::prepareFailed;
        result.buffersReleased=releaseUnpublished();result.retainBuffers=!result.buffersReleased;return result;}
    Backend backend(*this);result=transport::transfer(backend,packets);pciResult=backend.result;
    return result;
}
} }
