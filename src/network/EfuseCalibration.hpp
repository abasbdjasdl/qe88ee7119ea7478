// SPDX-License-Identifier: BSD-3-Clause
// Copyright(c) 2019-2022 Realtek Corporation (upstream BSD option)
// AX eFuse decoding and 8852BE board fields adapted from pinned efuse.c and
// rtw8852b.c. No OTP programming operation is provided.
#pragma once
#include "Rtw8852bEfuseLayout.hpp"
#include <stddef.h>
namespace rtl8852be { namespace network {
enum class EfuseStatus {ok,invalid,overlap,truncated,outOfRange};
// DDV: 1216 physical / 2048 logical; DAV: 96 / 16. Use the supplied bank's
// actual bounds, never the DDV sizes when decoding the smaller DAV allocation.
inline EfuseStatus decodeEfuse(const uint8_t *physical,size_t physicalBytes,uint8_t *logical,size_t logicalBytes,size_t securityBytes=4){
    if(!physical||!logical||!logicalBytes||securityBytes>physicalBytes/2)return EfuseStatus::invalid;
    const auto in=reinterpret_cast<uintptr_t>(physical),out=reinterpret_cast<uintptr_t>(logical);
    if(out>=in?out-in<physicalBytes:in-out<logicalBytes)return EfuseStatus::overlap;
    const size_t end=physicalBytes-securityBytes;
    // First pass checks the whole bank. Output stays untouched on any failure.
    for(unsigned pass=0;pass<2;++pass){
        if(pass)for(size_t i=0;i<logicalBytes;++i)logical[i]=0xff;
        size_t cursor=securityBytes;
        while(cursor<end){
            const uint8_t first=physical[cursor];if(first==0xff)break;
            if(end-cursor<2)return EfuseStatus::truncated;
            const uint8_t second=physical[cursor+1];if(second==0xff)break;
            const unsigned block=((second>>4)&15)|((first&15)<<4);cursor+=2;
            for(unsigned word=0;word<4;++word){
                if(second&(1u<<word))continue;
                const size_t address=size_t(block)*8+word*2;
                if(end-cursor<2)return EfuseStatus::truncated;
                if(address>=logicalBytes||logicalBytes-address<2)return EfuseStatus::outOfRange;
                if(pass){logical[address]=physical[cursor];logical[address+1]=physical[cursor+1];}cursor+=2;
            }
        }
    }
    return EfuseStatus::ok;
}
inline int8_t signedByte(uint8_t value){return static_cast<int8_t>(value<128?int(value):int(value)-256);}
inline int8_t signedNibble(uint8_t value){value&=15;return static_cast<int8_t>(value<8?int(value):int(value)-16);}
struct BoardCalibration {
    uint8_t mac[6]{},rfe{},xtal{},channelPlan{},country[2]{},thermal[2]{};
    int8_t tssiCck[2][6]{},tssiMcs[2][19]{},gainOffset[2][5]{};
    bool identityValid{},xtalValid{},gainOffsetValid{};
};
static_assert(offsetof(reference::rtw8852b_efuse,e)==0x400,"PCIe MAC offset");
static_assert(offsetof(reference::rtw8852b_efuse,rfe_type)==0x2ca,"RFE offset");
static_assert(offsetof(reference::rtw8852b_efuse,xtal_k)==0x2b9,"crystal offset");
inline bool parseBoardCalibration(const uint8_t *logical,size_t bytes,BoardCalibration &out){
    out={};if(!logical||bytes<sizeof(reference::rtw8852b_efuse))return false;
    // Byte accesses avoid unaligned or aliased packed-structure loads.
    BoardCalibration board{};
    for(size_t i=0;i<6;++i)board.mac[i]=logical[offsetof(reference::rtw8852b_efuse,e)+i];
    board.rfe=logical[offsetof(reference::rtw8852b_efuse,rfe_type)];board.xtal=logical[offsetof(reference::rtw8852b_efuse,xtal_k)];
    board.channelPlan=logical[offsetof(reference::rtw8852b_efuse,channel_plan)];
    for(unsigned i=0;i<2;++i)board.country[i]=logical[offsetof(reference::rtw8852b_efuse,country_code)+i];
    board.thermal[0]=logical[offsetof(reference::rtw8852b_efuse,path_a_therm)];board.thermal[1]=logical[offsetof(reference::rtw8852b_efuse,path_b_therm)];
    const size_t tssi[]={offsetof(reference::rtw8852b_efuse,path_a_tssi),offsetof(reference::rtw8852b_efuse,path_b_tssi)};
    for(unsigned path=0;path<2;++path){
        for(unsigned i=0;i<6;++i)board.tssiCck[path][i]=signedByte(logical[tssi[path]+i]);
        for(unsigned i=0;i<5;++i)board.tssiMcs[path][i]=signedByte(logical[tssi[path]+6+i]);
        for(unsigned i=0;i<14;++i)board.tssiMcs[path][5+i]=signedByte(logical[tssi[path]+18+i]);
    }
    const size_t gains[]={offsetof(reference::rtw8852b_efuse,rx_gain_2g_cck),offsetof(reference::rtw8852b_efuse,rx_gain_2g_ofdm),
        offsetof(reference::rtw8852b_efuse,rx_gain_5g_low),offsetof(reference::rtw8852b_efuse,rx_gain_5g_mid),offsetof(reference::rtw8852b_efuse,rx_gain_5g_high)};
    for(unsigned i=0;i<5;++i){const uint8_t value=logical[gains[i]];
        board.gainOffset[0][i]=signedNibble(value>>4);board.gainOffset[1][i]=signedNibble(value);
        board.gainOffsetValid|=value!=0xff;}
    uint8_t any=0;for(auto b:board.mac)any|=b;
    board.identityValid=any&&!(board.mac[0]&1)&&board.rfe!=0xff;board.xtalValid=board.xtal!=0xff;
    out=board;return true;
}
struct PhyCalibration {
    int8_t tssiTrim[2][8]{},gainComp[2][5]{};
    uint8_t thermalTrim[2]{},paBiasTrim[2]{};
    bool powerValid{},tssiValid{},thermalValid{},paBiasValid{},gainCompValid{};
};
inline bool parsePhyCalibration(const uint8_t *phycap,size_t bytes,PhyCalibration &out){
    out={};if(!phycap||bytes<128)return false;PhyCalibration calibration{};
    calibration.powerValid=phycap[0x5e9-0x580]==0xaa;
    const unsigned tssi[]={0x5d6,0x5ab},thermal[]={0x5df,0x5dc},bias[]={0x5de,0x5db};
    const unsigned gains[2][5]={{0x5bb,0x5ba,0,0x5b9,0x5b8},{0x590,0x58f,0,0x58e,0x58d}};
    for(unsigned path=0;path<2;++path){
        for(unsigned i=0;i<8;++i){auto value=phycap[tssi[path]-0x580-i];calibration.tssiTrim[path][i]=signedByte(value);calibration.tssiValid|=value!=0xff;}
        calibration.thermalTrim[path]=phycap[thermal[path]-0x580];calibration.thermalValid|=calibration.thermalTrim[path]!=0xff;
        calibration.paBiasTrim[path]=phycap[bias[path]-0x580];calibration.paBiasValid|=calibration.paBiasTrim[path]!=0xff;
        for(unsigned i=0;i<5;++i)if(gains[path][i]){const auto value=phycap[gains[path][i]-0x580];
            calibration.gainComp[path][i]=signedNibble(value);calibration.gainCompValid|=value!=0xff;}
    }
    if(!calibration.tssiValid)for(auto &path:calibration.tssiTrim)for(auto &v:path)v=0;
    out=calibration;return true;
}
} }
