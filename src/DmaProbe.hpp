// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace rtl8852be { namespace dma {
constexpr size_t pageBytes=4096, ringEntries=256, bdBytes=8, packetBytes=2044;
struct Mapping {uint64_t address{},length{},offset{};unsigned segments{};};
inline bool validMapping(const Mapping &m) {
    return m.segments==1 && m.length==pageBytes && m.offset==pageBytes &&
        m.address!=0 && !(m.address&4095) && m.address<=0x100000000ULL-pageBytes;
}
inline bool encodeBd(uint8_t *out,size_t capacity,uint64_t address,uint32_t length) {
    if(!out || capacity<8)return false;
    for(unsigned i=0;i<8;++i)out[i]=0;
    if(!length || length>0xffff || !address || address>0xffffffffULL ||
       length>0x100000000ULL-address)return false;
    // rtw89_pci_fwcmd_submit: length, LS bit 14, 32-bit DMA address, LE.
    out[0]=static_cast<uint8_t>(length);out[1]=static_cast<uint8_t>(length>>8);
    out[3]=0x40;
    for(unsigned i=0;i<4;++i)out[4+i]=static_cast<uint8_t>(address>>(8*i));
    return true;
}
enum class Status {notRun,skipped,allocateFailed,prepareFailed,mapInvalid,overlap,
    encodeFailed,syncFailed,verifyFailed,validated,cleanupFailed,busMasterChanged};
inline const char *statusName(Status s) {
    switch(s){
    case Status::notRun:return "NOT_RUN";
    case Status::skipped:return "SKIPPED_BUS_MASTER_ACTIVE";
    case Status::allocateFailed:return "DMA_ALLOCATE_FAILED";
    case Status::prepareFailed:return "DMA_PREPARE_FAILED";
    case Status::mapInvalid:return "DMA_MAPPING_INVALID";
    case Status::overlap:return "DMA_MAPPINGS_OVERLAP";
    case Status::encodeFailed:return "DMA_DESCRIPTOR_INVALID";
    case Status::syncFailed:return "DMA_SYNCHRONIZE_FAILED";
    case Status::verifyFailed:return "DMA_CPU_VERIFY_FAILED";
    case Status::validated:return "DMA_MEMORY_VALIDATED";
    case Status::cleanupFailed:return "DMA_CLEANUP_FAILED";
    case Status::busMasterChanged:return "BUS_MASTER_CHANGED";
    }
    return "UNKNOWN";
}
struct Result {
    Status status{Status::notRun},operationStatus{Status::notRun};
    Mapping ring{},packet{};
    unsigned failedBuffer{},allocations{},prepared{},synchronized{};
    uint32_t osError{};
    bool cleanupOk{},cpuVerified{},busMasterBefore{},busMasterAfter{};
};
template<class Backend, class Extension>
Result probe(Backend &b, Extension extension) {
    Result r;r.busMasterBefore=(b.command()&4)!=0;
    if(r.busMasterBefore){r.status=Status::skipped;return r;}
    // Always close both slots, including one partially initialized by a failed
    // prepare. The backend owns each IOKit object and prepared-state pairing.
    do {
        bool failed=false;
        for(unsigned i=0;i<2;++i){
            r.failedBuffer=i;
            if(!b.allocate(i)){r.status=Status::allocateFailed;failed=true;break;}
            ++r.allocations;
            if(!b.prepare(i)){r.status=Status::prepareFailed;failed=true;break;}
            ++r.prepared;
            auto m=b.mapping(i);
            (i?r.packet:r.ring)=m;
            if(!validMapping(m)){r.status=Status::mapInvalid;failed=true;break;}
        }
        if(failed)break;
        if(r.ring.address==r.packet.address){r.status=Status::overlap;break;}
        auto *ring=b.bytes(0),*packet=b.bytes(1);
        if(!ring||!packet){r.status=Status::encodeFailed;break;}
        for(size_t i=0;i<pageBytes;++i){ring[i]=0;packet[i]=static_cast<uint8_t>((i*17+0x5a)&255);}
        if(!encodeBd(ring,pageBytes,r.packet.address,packetBytes)){r.status=Status::encodeFailed;break;}
        // This is synthetic data, not firmware or a valid command payload. The
        // descriptor is never submitted and no queue register is programmed.
        for(unsigned i=0;i<2;++i){
            r.failedBuffer=i;
            if(!b.synchronize(i)){r.status=Status::syncFailed;failed=true;break;}
            ++r.synchronized;
        }
        if(failed)break;
        r.cpuVerified=true;
        uint8_t expected[8]{};encodeBd(expected,8,r.packet.address,packetBytes);
        for(size_t i=0;i<pageBytes;++i){
            if(ring[i]!=(i<8?expected[i]:0) || packet[i]!=static_cast<uint8_t>((i*17+0x5a)&255)){
                r.cpuVerified=false;break;
            }
        }
        r.status=r.cpuVerified?Status::validated:Status::verifyFailed;
    } while(false);
    if(r.status==Status::validated)extension(b,static_cast<const Result &>(r));
    r.osError=b.lastError();r.operationStatus=r.status;
    // Reverse acquisition order. Both close calls execute even if one fails.
    const bool packetClosed=b.close(1),ringClosed=b.close(0);
    r.cleanupOk=packetClosed&&ringClosed;
    r.busMasterAfter=(b.command()&4)!=0;
    if(!r.cleanupOk)r.status=Status::cleanupFailed;
    if(r.busMasterAfter)r.status=Status::busMasterChanged;
    return r;
}
struct NoExtension {
    template<class Backend> void operator()(Backend &,const Result &) const {}
};
template<class Backend> Result probe(Backend &b){return probe(b,NoExtension{});}
} }
