// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation (upstream BSD option)
#pragma once
#include "RadioTables.hpp"
#include "FirmwareProtocol.hpp"
namespace rtl8852be { namespace network {
// Caller-owned non-stack storage: maximum 3 * 500 little-endian words.
// This prepares commands only; upload follows successful RF programming and
// completion of the final SWSI write. No firmware ACK is a DMA release.
struct RadioFirmwarePages {
    const uint8_t *data{};size_t words{};uint8_t path{};
    unsigned pages()const{return unsigned((words+499)/500);}
    bool encode(unsigned page,uint8_t sequence,uint8_t *out,size_t capacity,size_t &written)const{
        written=0;
        if(!data||!words||words>1500||path>1||page>=pages())return false;
        const size_t start=page*500,count=words-start>500?500:words-start;
        const auto src=reinterpret_cast<uintptr_t>(data),dst=reinterpret_cast<uintptr_t>(out);
        if(!out||(dst>=src?dst-src<words*4:src-dst<capacity))return false;
        return encodeH2c({2,uint8_t(path?9:8),uint8_t(page)},sequence,false,false,data+start*4,count*4,out,capacity,written);
    }
};
class RadioFirmwareCollector {
    uint8_t *storage_;size_t capacity_,count_{};
public:
    RadioFirmwareCollector(uint8_t *storage,size_t capacity):storage_(storage),capacity_(capacity){}
    bool cancelled()const{return false;}
    bool bbWrite(uint32_t,uint32_t){return false;}bool gainRecord(uint32_t,uint32_t){return false;}bool delayUs(unsigned){return false;}
    bool rfWrite(uint8_t,uint32_t address,uint32_t value){
        if((address&~0x100ffu)||(value&~0xfffffu))return false;
        if(address<0x100)return true;
        if(count_>=1500||count_>=capacity_/4)return false;
        if(storage_)store32(storage_+count_*4,(address<<20)|value);
        ++count_;return true;
    }
    size_t count()const{return count_;}
};
inline RadioTableStatus prepareRadioFirmware(const RadioTable &table,uint8_t rfe,uint8_t cut,uint8_t *storage,size_t capacity,RadioFirmwarePages &out){
    out={};if(!storage||table.kind!=RadioTableKind::radio)return RadioTableStatus::invalid;
    const auto src=reinterpret_cast<uintptr_t>(table.registers),dst=reinterpret_cast<uintptr_t>(storage);
    if(table.count>65536||(dst>=src?dst-src<table.count*sizeof(RadioRegister):src-dst<capacity))return RadioTableStatus::invalid;
    // Reject invalid/oversized table before writing any output bytes.
    RadioFirmwareCollector count(nullptr,capacity);
    auto check=applyRadioTable(table,rfe,cut,count);if(check.status!=RadioTableStatus::ok)return check.status;
    RadioFirmwareCollector store(storage,capacity);auto result=applyRadioTable(table,rfe,cut,store);
    if(result.status==RadioTableStatus::ok)out={storage,store.count(),table.path};
    return result.status;
}
} }
