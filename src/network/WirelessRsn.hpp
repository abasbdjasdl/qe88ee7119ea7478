// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WirelessSelection.hpp"
namespace rtl8852be { namespace network { namespace selection {
struct PersonalCiphers { Cipher pairwise{Cipher::ccmp},group{Cipher::ccmp}; };
// One explicit personal AKM and one pairwise suite. No implicit SAE/EAP fallback.
inline bool personalIE(const uint8_t *ie,size_t length,bool wpa1,PersonalCiphers &out){
    out={};PersonalCiphers parsed;
    if(wpa1)parsed.pairwise=parsed.group=Cipher::tkip;
    if(!length){out=parsed;return true;}
    if(!ie||length<20||length>257||size_t(ie[1])+2!=length)return false;
    size_t p=2;
    if(wpa1){
        if(ie[0]!=221||length<24||ie[2]!=0||ie[3]!=0x50||ie[4]!=0xf2||ie[5]!=1)return false;
        p=6;
    }else if(ie[0]!=48)return false;
    auto word=[&](uint16_t &v){if(length-p<2)return false;v=ie[p]|uint16_t(ie[p+1])<<8;p+=2;return true;};
    auto suite=[&](uint8_t &kind){if(length-p<4)return false;
        bool ok=ie[p]==0&&ie[p+1]==(wpa1?0x50:0x0f)&&ie[p+2]==(wpa1?0xf2:0xac);
        kind=ie[p+3];p+=4;return ok;};
    auto cipher=[&](Cipher &c){uint8_t kind;if(!suite(kind))return false;
        if(kind==2){c=Cipher::tkip;return true;}if(kind==4){c=Cipher::ccmp;return true;}return false;};
    uint16_t n=0;uint8_t akm=0;
    if(!word(n)||n!=1||!cipher(parsed.group)||!word(n)||n!=1||!cipher(parsed.pairwise)||
       !word(n)||n!=1||!suite(akm)||akm!=2)return false;
    if(p<length){
        uint16_t caps;if(!word(caps)||(caps&~uint16_t(wpa1?0:0x003c)))return false;
        if(p<length){if(wpa1||!word(n)||n!=0)return false;}
    }
    if(p!=length)return false;
    out=parsed;return true;
}
// Compatibility predicate for callers that explicitly require CCMP-only.
inline bool wpa2PskRsn(const uint8_t *ie,size_t length){
    PersonalCiphers c;
    return personalIE(ie,length,false,c)&&c.pairwise==Cipher::ccmp&&c.group==Cipher::ccmp;
}
} } }
