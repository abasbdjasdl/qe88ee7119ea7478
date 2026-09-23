// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/NativeWclCandidates.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace rtl8852be::network::nativewcl;
static bool allZero(const Observation &out){
    const auto *p=reinterpret_cast<const unsigned char*>(&out);
    for(size_t i=0;i<sizeof(out);++i)if(p[i])return false;
    return true;
}
static void countBytes(unsigned char *p,uint32_t n){
    for(unsigned i=0;i<4;++i)p[countOffset+i]=static_cast<unsigned char>(n>>(8*i));
}
int main(){
    unsigned char message[messageBytes+1];
    std::memset(message,0xa5,sizeof(message)); // Opaque prefix/trailer must not leak.
    countBytes(message,1);
    message[0x218]=1;message[0x219]=0xff; // Unknown byte is explicitly nonzero.
    message[0x21a]=0xef;message[0x21b]=0xbe;
    const unsigned char bssid[6]={2,1,2,3,4,5},owe[6]={2,6,7,8,9,10};
    std::memcpy(message+0x21c,bssid,6);std::memcpy(message+0x222,owe,6);
    message[0x228]=0x24;message[0x229]=0xd1;
    Observation out;
    assert(decode(message,messageBytes,out)==DecodeResult::ok);
    assert(out.count==1&&out.candidate.security==0xbeef&&out.candidate.channelSpec==0xd124);
    assert(out.candidate.saePkCapable==1);
    assert(!std::memcmp(out.candidate.bssid,bssid,6)&&!std::memcmp(out.candidate.oweTransitionAddress,owe,6));
    struct AliasStorage {unsigned char prefix[countOffset];Observation result;unsigned char extra[messageBytes];} alias;
    static_assert(offsetof(AliasStorage,result)==countOffset,"Alias must cover fields read by decode");
    auto *aliased=reinterpret_cast<unsigned char*>(&alias);
    std::memcpy(aliased,message,messageBytes);
    assert(decode(aliased,messageBytes,alias.result)==DecodeResult::ok);
    assert(alias.result.count==1&&alias.result.candidate.security==0xbeef&&alias.result.candidate.channelSpec==0xd124);
    assert(!std::memcmp(alias.result.candidate.bssid,bssid,6));
    std::memcpy(aliased,message,messageBytes);countBytes(aliased,2);
    assert(decode(aliased,messageBytes,alias.result)==DecodeResult::unsupportedCount&&allZero(alias.result));
    // Results own their bytes; overwriting the caller buffer does not change them.
    std::memset(message+recordOffset,0,recordBytes);
    assert(out.candidate.security==0xbeef&&!std::memcmp(out.candidate.bssid,bssid,6));
    countBytes(message,0);
    std::memset(message+recordOffset,0xff,recordBytes);
    std::memset(&out,0xa5,sizeof(out));
    assert(decode(message,messageBytes,out)==DecodeResult::ok&&allZero(out));
    const uint32_t unsupportedCounts[]={2u,10u,25u,0xffffffffu};
    for(auto n:unsupportedCounts){
        countBytes(message,n);std::memset(&out,0xa5,sizeof(out));
        assert(decode(message,messageBytes,out)==DecodeResult::unsupportedCount&&allZero(out));
    }
    countBytes(message,1);
    const size_t badLengths[]={0,countOffset,countOffset+3,recordOffset+recordBytes-1,messageBytes-1,messageBytes+1};
    for(auto length:badLengths){
        std::memset(&out,0xa5,sizeof(out));
        assert(decode(message,length,out)==DecodeResult::invalidLength&&allZero(out));
    }
    std::memset(&out,0xa5,sizeof(out));
    assert(decode(nullptr,messageBytes,out)==DecodeResult::invalidPointer&&allZero(out));
    assert(!readable(8,static_cast<size_t>(-1),2));
    assert(!readable(8,7,static_cast<size_t>(-1)));
    std::puts("Native WCL candidate byte-view tests passed; no native dispatch or join performed");
}
