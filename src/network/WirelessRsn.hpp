// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace rtl8852be { namespace network { namespace selection {
// Deliberately narrow acceptance: RSN v1, CCMP group/pairwise and PSK AKM.
// Other suites and required PMF must not be silently converted to WPA2/CCMP.
inline bool wpa2PskRsn(const uint8_t *ie,size_t length){
    if(!length)return true; // Explicit WPA2 request without an override IE.
    if(!ie||length<20||length>257||ie[0]!=48||size_t(ie[1])+2!=length)return false;
    size_t p=2;
    auto word=[&](uint16_t &v){if(length-p<2)return false;v=ie[p]|uint16_t(ie[p+1])<<8;p+=2;return true;};
    auto suite=[&](uint8_t kind){if(length-p<4)return false;
        bool ok=ie[p]==0&&ie[p+1]==0x0f&&ie[p+2]==0xac&&ie[p+3]==kind;p+=4;return ok;};
    uint16_t n=0;
    if(!word(n)||n!=1||!suite(4)||!word(n)||n!=1||!suite(4)||!word(n)||n!=1||!suite(2))return false;
    if(p==length)return true;
    uint16_t caps=0;if(!word(caps))return false;
    // Only the baseline capabilities already handled by this driver are accepted.
    // MFPR/MFPC, preauthentication and peerkey are not implemented in this profile.
    if(caps&~uint16_t(0x003c))return false;
    if(p==length)return true;
    if(!word(n)||n!=0)return false; // No PMKID cache handoff yet.
    return p==length; // No group management cipher extension yet.
}
} } }
