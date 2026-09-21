// SPDX-License-Identifier: BSD-3-Clause
// AX command formats adapted from Realtek rtw89 fw.c/fw.h/mac.h, BSD option,
// commit d1fced1b8a741dc9f92b47c69489c24385945f6e. No device I/O here.
// Copyright(c) 2020-2022 Realtek Corporation
#pragma once
#include "PciDataPath.hpp"
namespace rtl8852be { namespace network {
struct CommandId {uint8_t category{},commandClass{},function{};};
inline bool sameCommand(const CommandId &a,const CommandId &b){
    return a.category==b.category&&a.commandClass==b.commandClass&&a.function==b.function;
}
// Sequence is supplied by the serialized hardware command queue. Encoding is
// not sending and does not advance that queue. DMA completion and firmware ACK
// are separate events; neither is proof of association or Internet access.
inline bool encodeH2c(CommandId id,uint8_t sequence,bool receiveAck,bool doneAck,
                      const uint8_t *payload,size_t length,uint8_t *out,size_t capacity,size_t &written){
    written=0;
    if(!out||(!payload&&length)||id.category>3||id.commandClass>63||length>0x3fff-8||capacity<length+8)return false;
    // Permit in-place payload prepending without relying on aligned accesses.
    // Backwards copy handles payload==out; otherwise buffers must not overlap.
    if(payload==out){for(size_t i=length;i>0;--i)out[i+7]=payload[i-1];}
    else for(size_t i=0;i<length;++i)out[8+i]=payload[i];
    store32(out,uint32_t(id.category)|(uint32_t(id.commandClass)<<2)|
            (uint32_t(id.function)<<8)|(uint32_t(sequence)<<24));
    receiveAck=receiveAck || sequence%4==0; // upstream periodic receive ACK
    store32(out+4,uint32_t(length+8)|(receiveAck?0x4000:0)|(doneAck?0x8000:0));
    written=length+8;return true;
}
struct FirmwareEvent {CommandId id{};const uint8_t *payload{};size_t length{};};
inline bool decodeC2h(const uint8_t *packet,size_t bytes,FirmwareEvent &out){
    out={};if(!packet||bytes<8)return false;
    const auto w0=little32(packet);const size_t total=little32(packet+4)&0x3fff;
    if(total<8||total>bytes)return false;
    out.id={uint8_t(w0&3),uint8_t((w0>>2)&63),uint8_t((w0>>8)&255)};
    out.payload=packet+8;out.length=total-8;return true;
}
struct FirmwareAck {CommandId command{};uint8_t sequence{},returnCode{};bool done{};};
inline bool decodeAck(const FirmwareEvent &event,FirmwareAck &out){
    out={};
    if(event.id.category!=1||event.id.commandClass!=0||event.id.function>1||!event.payload||event.length<4)return false;
    const uint32_t word=little32(event.payload);
    out.command={uint8_t(word&3),uint8_t((word>>2)&63),uint8_t((word>>8)&255)};
    out.done=event.id.function==1;
    out.sequence=uint8_t(word>>(out.done?24:16));
    out.returnCode=out.done?uint8_t(word>>16):0;return true;
}
struct RoleCommand {uint8_t macid{},selfRole{},updateMode{},wifiRole{};};
inline bool encodeRole(const RoleCommand &p,uint8_t sequence,uint8_t *out,size_t capacity,size_t &written){
    written=0;if(p.selfRole>3||p.updateMode>7||p.wifiRole>15)return false;
    uint8_t payload[4];store32(payload,uint32_t(p.macid)|(uint32_t(p.selfRole)<<8)|
        (uint32_t(p.updateMode)<<10)|(uint32_t(p.wifiRole)<<13));
    return encodeH2c({1,8,4},sequence,false,true,payload,4,out,capacity,written);
}
struct JoinCommand {uint8_t macid{},band{},wmm{},port{},netType{},wifiRole{},selfRole{};bool disconnect{},trigger{};};
inline bool encodeJoin(const JoinCommand &p,uint8_t sequence,uint8_t *out,size_t capacity,size_t &written){
    written=0;if(p.band>1||p.wmm>3||p.port>7||p.netType>3||p.wifiRole>15||p.selfRole>3)return false;
    uint8_t payload[4];store32(payload,uint32_t(p.macid)|(uint32_t(p.disconnect)<<8)|
        (uint32_t(p.band)<<9)|(uint32_t(p.wmm)<<10)|(uint32_t(p.trigger)<<12)|
        (uint32_t(p.port)<<21)|(uint32_t(p.netType)<<24)|(uint32_t(p.wifiRole)<<26)|(uint32_t(p.selfRole)<<30));
    // AX/8852B uses the 4-byte join command, not BE's extended 12-byte version.
    return encodeH2c({1,8,0},sequence,false,true,payload,4,out,capacity,written);
}
} }
