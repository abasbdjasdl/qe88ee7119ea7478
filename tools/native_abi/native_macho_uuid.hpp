// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace r16_native_identity {
inline uint32_t little32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
// Only read a loader-owned, retained dependency's mapped Mach-O header. This
// function validates lengths; it cannot validate arbitrary kernel pointers.
inline bool readMachOUUID(const uint8_t *data,size_t size,uint8_t *uuid) {
    if(!data||!uuid||size<32||little32(data)!=0xfeedfacf||little32(data+4)!=0x01000007)return false;
    const auto commands=little32(data+16),bytes=little32(data+20);
    if(bytes>size-32||bytes>65536||commands>bytes/8)return false;
    size_t offset=32,end=32+bytes;const uint8_t *found=nullptr;
    for(uint32_t i=0;i<commands;++i){
        if(end-offset<8)return false;
        const auto kind=little32(data+offset),length=little32(data+offset+4);
        if(length<8||(length&7)||length>end-offset)return false;
        if(kind==0x1b){
            if(found||length!=24)return false;
            found=data+offset+8;
        }
        offset+=length;
    }
    if(offset!=end||!found)return false;
    for(size_t i=0;i<16;++i)uuid[i]=found[i];
    return true;
}
inline int uuidHex(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
inline bool readUUIDString(const char *text,size_t size,uint8_t *uuid) {
    if(!text||!uuid||size!=37||text[36])return false;
    uint8_t result[16]{};size_t cursor=0;
    for(size_t i=0;i<16;++i){
        if(i==4||i==6||i==8||i==10){if(text[cursor++]!='-')return false;}
        const int hi=uuidHex(text[cursor++]),lo=uuidHex(text[cursor++]);
        if(hi<0||lo<0)return false;
        result[i]=uint8_t((hi<<4)|lo);
    }
    for(size_t i=0;i<16;++i)uuid[i]=result[i];
    return true;
}
}
