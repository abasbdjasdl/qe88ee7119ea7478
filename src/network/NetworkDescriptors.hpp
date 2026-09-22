// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <stddef.h>
#include "Rtw89DescriptorCore.hpp"
namespace rtl8852be { namespace network {
using TxInfo=reference::rtw89_tx_desc_info;
using RxInfo=reference::rtw89_rx_desc_info;
enum class DescriptorStatus {ok,nullPointer,truncated,invalidLength,unrepresentable,corruptFrame};
inline uint32_t little32(const uint8_t *p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline bool txFieldsFit(const TxInfo &p){
#include "DescriptorLimits.inc"
    return true;
}
// Output is untouched on failure; length is published only after encoding.
inline DescriptorStatus encodeTx(const TxInfo &p,uint8_t *out,size_t capacity,size_t &length){
    length=0;if(!out)return DescriptorStatus::nullPointer;
    if(!p.pkt_size)return DescriptorStatus::invalidLength;
    if(!txFieldsFit(p))return DescriptorStatus::unrepresentable;
    const size_t needed=p.en_wd_info?48:24;
    if(capacity<needed)return DescriptorStatus::truncated;
    alignas(4) uint8_t encoded[48]{};
    reference::rtw89_core_fill_txdesc(nullptr,const_cast<TxInfo *>(&p),encoded);
    for(size_t i=0;i<needed;++i)out[i]=encoded[i];length=needed;return DescriptorStatus::ok;
}
struct RxPacket {RxInfo info{};const uint8_t *payload{};size_t length{},offset{};};
// Same effective-decryption condition as pinned rtw89_core_update_rx_status.
// HW_DEC alone is insufficient: SW_DEC requests host software processing.
inline bool hardwareDecrypted(const RxInfo &info){return info.hw_dec&&!info.sw_dec&&!info.icv_err;}
// Header-only inspection for the first PCI fragment. Payload is never exposed
// until the complete-frame decoder has checked the advertised packet length.
inline DescriptorStatus decodeRxHeader(const uint8_t *data,size_t size,size_t descriptorOffset,RxPacket &out){
    out={};if(!data)return DescriptorStatus::nullPointer;
    if(descriptorOffset>size||size-descriptorOffset<16)return DescriptorStatus::truncated;
    const auto word=little32(data+descriptorOffset);
    const size_t descriptorBytes=(word&0x80000000u)?32:16;
    if(size-descriptorOffset<descriptorBytes)return DescriptorStatus::truncated;
    alignas(4) uint8_t descriptor[32]{};
    for(size_t i=0;i<descriptorBytes;++i)descriptor[i]=data[descriptorOffset+i];
    RxInfo info{};
    // The upstream AX routine consumes a pre-populated shift field.
    info.shift=(word>>14)&3;
    reference::rtw89_chip_info chip{reference::RTL8852B};reference::rtw89_dev device{&chip};
    reference::rtw89_core_query_rxdesc(&device,&info,descriptor,0);
    const size_t overhead=size_t(info.offset)+info.rxd_len;
    if(!info.pkt_size||info.pkt_type==15)return DescriptorStatus::invalidLength;
    if(overhead>size-descriptorOffset)return DescriptorStatus::truncated;
    if(info.icv_err||info.crc32_err)return DescriptorStatus::corruptFrame;
    out.info=info;out.offset=descriptorOffset+overhead;out.length=info.pkt_size;
    return DescriptorStatus::ok;
}
inline DescriptorStatus decodeRx(const uint8_t *data,size_t size,size_t descriptorOffset,RxPacket &out){
    auto status=decodeRxHeader(data,size,descriptorOffset,out);
    if(status!=DescriptorStatus::ok)return status;
    if(out.length>size-out.offset){out={};return DescriptorStatus::truncated;}
    out.payload=data+out.offset;
    return DescriptorStatus::ok;
}
static_assert(sizeof(reference::rtw89_txwd_body)==24,"8852B TXWD must be 24 bytes");
static_assert(sizeof(reference::rtw89_txwd_info)==24,"8852B TXWI must be 24 bytes");
static_assert(sizeof(reference::rtw89_rxdesc_short)==16&&sizeof(reference::rtw89_rxdesc_long)==32,"AX RX descriptor ABI");
} }
