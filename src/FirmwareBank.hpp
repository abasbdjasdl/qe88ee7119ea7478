// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "FirmwarePackets.hpp"
#include "DmaProbe.hpp"
namespace rtl8852be { namespace transport {
enum class BankStatus {notRun,ready,invalidPlan,unsafeCommand,allocationFailed,prepareFailed,
    mappingInvalid,mappingOverlap,encodeFailed,syncFailed,verifyFailed};
inline const char *bankStatusName(BankStatus s){
    switch(s){
    case BankStatus::notRun:return "NOT_RUN";
    case BankStatus::ready:return "FIRMWARE_BANK_READY";
    case BankStatus::invalidPlan:return "BANK_INVALID_PLAN";
    case BankStatus::unsafeCommand:return "BANK_UNSAFE_PCI_COMMAND";
    case BankStatus::allocationFailed:return "BANK_ALLOCATION_FAILED";
    case BankStatus::prepareFailed:return "BANK_PREPARE_FAILED";
    case BankStatus::mappingInvalid:return "BANK_MAPPING_INVALID";
    case BankStatus::mappingOverlap:return "BANK_MAPPING_OVERLAP";
    case BankStatus::encodeFailed:return "BANK_ENCODE_FAILED";
    case BankStatus::syncFailed:return "BANK_SYNC_FAILED";
    case BankStatus::verifyFailed:return "BANK_VERIFY_FAILED";
    }return "UNKNOWN";
}
struct BankResult {BankStatus status{BankStatus::notRun};unsigned allocated{},prepared{},synced{},packets{},failedSlot{};uint64_t ringAddress{};size_t payloadBytes{};};
// Slot 0 is the ring, slots 1..count are distinct packet pages. Allocation and
// cleanup are separate so a future transport cannot free a device-owned bank.
template<class Memory> BankResult prepareBank(Memory &m,const Packets &packets){
    BankResult r;r.packets=packets.count();
    if(!r.packets||r.packets>maxPackets){r.status=BankStatus::invalidPlan;return r;}
    if(m.command()&4){r.status=BankStatus::unsafeCommand;return r;}
    for(unsigned i=0;i<=r.packets;++i){
        r.failedSlot=i;
        if(!m.allocate(i)){r.status=BankStatus::allocationFailed;return r;}++r.allocated;
        if(!m.prepare(i)){r.status=BankStatus::prepareFailed;return r;}++r.prepared;
        const auto map=m.mapping(i);
        if(!dma::validMapping(map)){r.status=BankStatus::mappingInvalid;return r;}
        for(unsigned j=0;j<i;++j)if(map.address==m.mapping(j).address){r.status=BankStatus::mappingOverlap;return r;}
        auto *bytes=m.bytes(i);
        if(!bytes){r.status=BankStatus::encodeFailed;return r;}
        for(size_t j=0;j<4096;++j)bytes[j]=0;
        if(!i){r.ringAddress=map.address;continue;}
        PacketInfo info;
        if(packets.encode(i-1,bytes,4096,info)!=PacketStatus::ok||
           !dma::encodeBd(m.bytes(0)+(i-1)*8,8,map.address,static_cast<uint32_t>(info.bytes))){
            r.status=BankStatus::encodeFailed;return r;
        }
        r.payloadBytes+=info.bytes;
    }
    // Sync packet data first, then the ring descriptors referencing it.
    for(unsigned n=0;n<=r.packets;++n){
        const unsigned i=(n+1)%(r.packets+1);r.failedSlot=i;
        if(!m.synchronize(i)){r.status=BankStatus::syncFailed;return r;}++r.synced;
    }
    uint8_t expected[maxPacketBytes]{};
    for(unsigned i=1;i<=r.packets;++i){
        r.failedSlot=i;PacketInfo info;
        if(packets.encode(i-1,expected,sizeof(expected),info)!=PacketStatus::ok){r.status=BankStatus::encodeFailed;return r;}
        const auto *actual=m.bytes(i);
        for(size_t j=0;j<4096;++j)if(actual[j]!=(j<info.bytes?expected[j]:0)){r.status=BankStatus::verifyFailed;return r;}
        uint8_t bd[8]{};dma::encodeBd(bd,8,m.mapping(i).address,static_cast<uint32_t>(info.bytes));
        for(unsigned j=0;j<8;++j)if(m.bytes(0)[(i-1)*8+j]!=bd[j]){r.status=BankStatus::verifyFailed;return r;}
    }
    for(size_t i=r.packets*8;i<4096;++i)if(m.bytes(0)[i]){r.status=BankStatus::verifyFailed;return r;}
    if(m.command()&4){r.status=BankStatus::unsafeCommand;return r;}
    r.status=BankStatus::ready;return r;
}
} }
