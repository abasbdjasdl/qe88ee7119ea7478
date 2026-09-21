// SPDX-License-Identifier: BSD-3-Clause
// RTL8852B transport facts from rtw89 core.c/fw.c/txrx.h (BSD option).
// Copyright(c) 2019-2022 Realtek Corporation
#pragma once
#include "FirmwarePlan.hpp"
namespace rtl8852be { namespace transport {
constexpr size_t txwdBytes=24,h2cBytes=8,maxPacketBytes=txwdBytes+firmware::packetBytes;
constexpr unsigned ringSize=256,maxPackets=ringSize-1;
inline void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=static_cast<uint8_t>(v>>(8*i));}
enum class PacketStatus {ok,layoutError,tooManyPackets,notInitialized,badIndex,smallBuffer,overlap};
struct PacketInfo {size_t bytes{},payloadBytes{},sourceOffset{};unsigned section{};bool header{};};
// An immutable source container must outlive this object. Initialization parses
// the source; callers cannot supply a forged Plan. No allocation or I/O occurs.
class Packets {
    const uint8_t *data{};size_t size{};firmware::Plan layout{};
public:
    firmware::Status layoutStatus{firmware::Status::truncated};
    PacketStatus initialize(const uint8_t *source,size_t bytes,uint8_t cut){
        data=nullptr;size=0;layout=firmware::Plan{};
        layoutStatus=firmware::parse(source,bytes,cut,layout);
        if(layoutStatus!=firmware::Status::ok)return PacketStatus::layoutError;
        if(!layout.packetCount || layout.packetCount>=maxPackets){layout=firmware::Plan{};return PacketStatus::tooManyPackets;}
        data=source;size=bytes;return PacketStatus::ok;
    }
    unsigned count()const{return data?layout.packetCount+1:0;}
    const firmware::Plan &plan()const{return layout;}
    PacketStatus encode(unsigned ordinal,uint8_t *out,size_t capacity,PacketInfo &info,uint8_t sequence=0)const{
        info=PacketInfo{};
        if(!data)return PacketStatus::notInitialized;
        if(ordinal>=count())return PacketStatus::badIndex;
        firmware::Chunk chunk{};
        const bool header=ordinal==0;
        if(!header&&!firmware::chunkAt(layout,ordinal-1,chunk))return PacketStatus::badIndex;
        const size_t payload=header?h2cBytes+layout.baseHeaderBytes:chunk.bytes;
        const size_t length=txwdBytes+payload;
        if(!out||capacity<length||length>maxPacketBytes)return PacketStatus::smallBuffer;
        const auto dst=reinterpret_cast<uintptr_t>(out),src=reinterpret_cast<uintptr_t>(data);
        if(dst>=src?dst-src<size:src-dst<length)return PacketStatus::overlap;
        for(size_t i=0;i<length;++i)out[i]=0;
        auto *body=out+txwdBytes;
        if(header){
            // Header command: MAC category 1, FWDL class 3, function/type 0;
            // no ACK flags and no sequence increment in the FWDL path.
            put32(body,0x0d|(uint32_t(sequence)<<24));put32(body+4,static_cast<uint32_t>(payload));
            for(size_t i=0;i<layout.baseHeaderBytes;++i)body[h2cBytes+i]=data[layout.imageOffset+i];
            const auto w7=firmware::le32(body+h2cBytes+28);
            put32(body+h2cBytes+28,(w7&0xffff0000u)|firmware::packetBytes);
        }else{
            for(size_t i=0;i<chunk.bytes;++i)body[i]=data[chunk.offset+i];
        }
        // 8852B uses rtw89_core_fill_txdesc, NOT the RX-short-style _fwcmd_v1.
        // H2C DMA channel=12, no WD info/page, FW_DL only for section payloads.
        put32(out,(12u<<16)|(header?0u:(1u<<20)));
        put32(out+8,static_cast<uint32_t>(payload));
        // core_tx_update_desc_info derives SW_SEQ from bytes 22/23 before the
        // FWCMD specialization. Preserve that for long packets, zero for tails
        // shorter than a MAC header instead of reading beyond the payload.
        const uint32_t swseq=payload>=24?((uint32_t(body[22])|(uint32_t(body[23])<<8))>>4):0;
        put32(out+12,swseq);
        info={length,payload,header?layout.imageOffset:chunk.offset,header?0:chunk.section,header};
        return PacketStatus::ok;
    }
};
} }
