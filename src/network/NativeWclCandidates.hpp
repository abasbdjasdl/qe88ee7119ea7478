// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace rtl8852be { namespace network { namespace nativewcl {
// A byte view of one observed Sequoia 15.4.1 message, NOT a C++ ABI layout.
// Evidence and the exact KC hash are recorded in docs/native-wcl-evidence.md.
// Caller must first verify that KC profile and supply readable, locally owned
// bytes. A 988-byte shape alone proves neither version nor pointer validity;
// never pass an unvalidated kernel/user pointer directly to this helper.
constexpr size_t messageBytes=988,countOffset=0x214,recordOffset=0x218,recordBytes=18;
enum class DecodeResult : uint8_t { ok,invalidPointer,invalidLength,unsupportedCount };
struct Candidate {
    uint16_t security{},channelSpec{}; // Opaque Apple values, not net80211 enums.
    uint8_t saePkCapable{},bssid[6]{},oweTransitionAddress[6]{};
};
struct Observation {uint32_t count{};Candidate candidate{};};
inline void clear(Observation &out){
    auto *p=reinterpret_cast<uint8_t*>(&out);
    for(size_t i=0;i<sizeof(out);++i)p[i]=0;
}
inline bool readable(size_t length,size_t offset,size_t bytes){
    return offset<=length&&bytes<=length-offset;
}
inline uint16_t little16(const uint8_t *p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
inline uint32_t little32(const uint8_t *p){
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
inline DecodeResult writeResult(Observation &out,const Observation &value,DecodeResult result){
    auto *dst=reinterpret_cast<uint8_t*>(&out);
    const auto *src=reinterpret_cast<const uint8_t*>(&value);
    for(size_t i=0;i<sizeof(out);++i)dst[i]=src[i];
    return result;
}
inline DecodeResult decode(const void *data,size_t length,Observation &out){
    // Delay all writes to out until reads finish, including when out aliases
    // the input storage. Padding and every failed result are also zeroed.
    Observation decoded;clear(decoded);
    if(!data)return writeResult(out,decoded,DecodeResult::invalidPointer);
    if(length!=messageBytes||!readable(length,countOffset,4))return writeResult(out,decoded,DecodeResult::invalidLength);
    const auto *p=static_cast<const uint8_t*>(data);
    const auto count=little32(p+countOffset);
    // The audited producer emits one candidate on success. Zero is an empty
    // observation; it is not cancellation, association success, or authorization.
    // Later fields start at 0x2cc: never treat the whole message tail as records.
    if(count>1)return writeResult(out,decoded,DecodeResult::unsupportedCount);
    if(!count)return writeResult(out,decoded,DecodeResult::ok);
    if(!readable(length,recordOffset,recordBytes)||
       !readable(length,recordOffset,1)||!readable(length,recordOffset+2,2)||
       !readable(length,recordOffset+4,6)||!readable(length,recordOffset+10,6)||
       !readable(length,recordOffset+16,2))return writeResult(out,decoded,DecodeResult::invalidLength);
    decoded.count=1;
    decoded.candidate.saePkCapable=p[recordOffset];
    // +1 is unknown, not an asserted zero/reserved byte.
    decoded.candidate.security=little16(p+recordOffset+2);
    decoded.candidate.channelSpec=little16(p+recordOffset+16);
    for(unsigned i=0;i<6;++i){
        decoded.candidate.bssid[i]=p[recordOffset+4+i];
        decoded.candidate.oweTransitionAddress[i]=p[recordOffset+10+i];
    }
    return writeResult(out,decoded,DecodeResult::ok);
}
} } }
