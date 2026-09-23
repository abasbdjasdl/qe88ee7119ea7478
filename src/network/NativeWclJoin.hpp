// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeWclCandidates.hpp"
#include "WirelessRsn.hpp"
namespace rtl8852be { namespace network { namespace nativewcl {

// Same pinned 15.4.1 KC/profile as NativeWclCandidates. This decodes locally
// owned bytes only; it does not validate a kernel pointer, select a network,
// install a key, or announce association. The frontend must first verify the
// runtime profile, then bind the result to a current scan/request generation.
// This deliberately admits only the embedded, raw 32-byte WPA2-PSK PMK case.
enum class JoinResult : uint8_t {
    ok,invalidPointer,invalidLength,invalidSsid,invalidCandidate,
    unsupportedMode,unsupportedAuthentication,unsupportedPolicy,
    unsupportedKey,unsupportedRsn
};
inline JoinResult joinResult(selection::Join &out,selection::Join &decoded,JoinResult result){
    // Writes occur after the final input read, allowing out to alias the input.
    // Clear failure output/padding and the temporary credential on every path.
    if(result==JoinResult::ok){
        auto *dst=reinterpret_cast<uint8_t*>(&out);
        const auto *src=reinterpret_cast<const uint8_t*>(&decoded);
        for(size_t i=0;i<sizeof(out);++i)dst[i]=src[i];
    }else selection::wipe(&out,sizeof(out));
    selection::wipe(&decoded,sizeof(decoded));
    return result;
}
inline JoinResult decodeWpa2Join(const void *bytes,size_t length,selection::Join &out){
    selection::Join decoded;selection::wipe(&decoded,sizeof(decoded));
    if(!bytes)return joinResult(out,decoded,JoinResult::invalidPointer);
    if(length!=messageBytes)return joinResult(out,decoded,JoinResult::invalidLength);
    const auto *p=static_cast<const uint8_t*>(bytes);
    if(little16(p+0x0c)!=2)return joinResult(out,decoded,JoinResult::unsupportedMode);
    if(little32(p+0x10)!=1||little32(p+0x14)!=8||little32(p+0x18)!=0)
        return joinResult(out,decoded,JoinResult::unsupportedAuthentication);
    const uint32_t ssidLength=little32(p+0x1c);
    if(!ssidLength||ssidLength>32)return joinResult(out,decoded,JoinResult::invalidSsid);
    // The target consumer reads a u16 at +0x1e0 and individual bytes at +0x1e4
    // and +0x1e8. Adjacent bytes have no established policy-field meaning.
    // These are separate policy fields, NOT a version or a single flags word.
    // In this KC the producer sets 1e4 bit1 when its policy selector is zero.
    // The audited direct single-link WPA2 consumer ignores that bit: its other
    // 1e4 masks are 0x0c (SAE-PK state), 0x01 and 0x20. Admit only {0,2}; do
    // not discard other policy/transition bits or claim all menu joins work.
    if(little16(p+0x1e0)||(p[0x1e4]!=0&&p[0x1e4]!=2)||p[0x1e8])
        return joinResult(out,decoded,JoinResult::unsupportedPolicy);
    if(little32(p+0x44)!=32||little32(p+0x48)!=6||little32(p+0x1ec))
        return joinResult(out,decoded,JoinResult::unsupportedKey);
    // The producer trims/caps to 257, which is NOT validation of IE syntax.
    // In particular, never use personalIE's empty-IE compatibility default.
    const uint16_t rsnLength=little16(p+0xd4);
    selection::PersonalCiphers ciphers;
    if(rsnLength<20||rsnLength>257||
       !selection::personalIE(p+0xd6,rsnLength,false,ciphers)||
       ciphers.pairwise!=selection::Cipher::ccmp||ciphers.group!=selection::Cipher::ccmp)
        return joinResult(out,decoded,JoinResult::unsupportedRsn);
    Observation candidate;
    if(decode(p,length,candidate)!=DecodeResult::ok||candidate.count!=1||candidate.candidate.saePkCapable)
        return joinResult(out,decoded,JoinResult::invalidCandidate);
    for(auto b:candidate.candidate.oweTransitionAddress)
        if(b)return joinResult(out,decoded,JoinResult::invalidCandidate);
    decoded.ssidLength=ssidLength;
    for(size_t i=0;i<ssidLength;++i)decoded.ssid[i]=p[0x20+i];
    decoded.specificBssid=true;
    for(unsigned i=0;i<6;++i)decoded.bssid[i]=candidate.candidate.bssid[i];
    decoded.security=selection::Security::wpa2Psk;
    decoded.pairwise=decoded.group=selection::Cipher::ccmp;decoded.pmkLength=32;
    if(!selection::valid(decoded))return joinResult(out,decoded,JoinResult::invalidCandidate);
    for(unsigned i=0;i<32;++i)decoded.pmk[i]=p[0x50+i];
    return joinResult(out,decoded,JoinResult::ok);
}
} } }
