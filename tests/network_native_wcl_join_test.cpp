// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/NativeWclJoin.hpp"
#ifdef NDEBUG
#undef NDEBUG // Assertions invoke the decoder and must execute in optimized builds.
#endif
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <new>
using namespace rtl8852be::network;
using nativewcl::JoinResult;
static void word(uint8_t *p,size_t off,uint32_t v,unsigned n=4){for(unsigned i=0;i<n;++i)p[off+i]=uint8_t(v>>(i*8));}
static constexpr uint8_t rsn[]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
static constexpr uint8_t name[]={0,0xff,'x','y'};
static constexpr uint8_t mac[]={2,1,2,3,4,5};
static void fixture(uint8_t *p){
    memset(p,0,nativewcl::messageBytes);word(p,0x0c,2,2);word(p,0x10,1);word(p,0x14,8);
    word(p,0x1c,sizeof(name));memcpy(p+0x20,name,sizeof(name));
    word(p,0x44,32);word(p,0x48,6);
    for(unsigned i=0;i<32;++i)p[0x50+i]=uint8_t(i^0xa5); // Synthetic, never a real credential.
    word(p,0xd4,sizeof(rsn),2);memcpy(p+0xd6,rsn,sizeof(rsn));
    word(p,nativewcl::countOffset,1);memcpy(p+nativewcl::recordOffset+4,mac,6);
}
static bool zero(const selection::Join &j){
    for(size_t i=0;i<sizeof(j);++i)if(reinterpret_cast<const uint8_t*>(&j)[i])return false;
    return true;
}
static void failure(const void *p,size_t n,JoinResult expected){
    selection::Join out;memset(&out,0xa5,sizeof(out));
    assert(nativewcl::decodeWpa2Join(p,n,out)==expected);assert(zero(out));
}
int main(){
    std::array<uint8_t,nativewcl::messageBytes+1> storage{};auto *p=storage.data()+1;fixture(p);
    selection::Join out;assert(nativewcl::decodeWpa2Join(p,nativewcl::messageBytes,out)==JoinResult::ok);
    assert(selection::valid(out)&&out.ssidLength==sizeof(name)&&out.specificBssid&&out.pmkLength==32);
    assert(!memcmp(out.ssid,name,sizeof(name))&&!memcmp(out.bssid,mac,6));
    assert(!memcmp(out.pmk,p+0x50,32)&&out.security==selection::Security::wpa2Psk);
    selection::wipe(&out,sizeof(out));
    failure(nullptr,nativewcl::messageBytes,JoinResult::invalidPointer);
    for(size_t n=0;n<nativewcl::messageBytes;++n)failure(p,n,JoinResult::invalidLength);
    failure(p,nativewcl::messageBytes+1,JoinResult::invalidLength);
    for(unsigned mode: {0u,1u,3u,65535u}){fixture(p);word(p,0x0c,mode,2);failure(p,nativewcl::messageBytes,JoinResult::unsupportedMode);}
    for(unsigned len:{0u,33u,0xffffffffu}){fixture(p);word(p,0x1c,len);failure(p,nativewcl::messageBytes,JoinResult::invalidSsid);}
    for(unsigned auth:{0u,1u,2u,4u,9u,0xffffffffu}){fixture(p);word(p,0x14,auth);failure(p,nativewcl::messageBytes,JoinResult::unsupportedAuthentication);}
    fixture(p);word(p,0x10,2);failure(p,nativewcl::messageBytes,JoinResult::unsupportedAuthentication);
    fixture(p);word(p,0x18,1);failure(p,nativewcl::messageBytes,JoinResult::unsupportedAuthentication);
    for(size_t offset:{size_t(0x1e0),size_t(0x1e1),size_t(0x1e4),size_t(0x1e8)}){
        for(unsigned bit=1;bit<=128;bit<<=1){
            fixture(p);word(p,offset,bit,1);failure(p,nativewcl::messageBytes,JoinResult::unsupportedPolicy);
        }
    }
    // Only the demonstrated u16/byte reads are policy fields. Nonzero adjacent
    // bytes must not become an invented u32 flags word in this offline view.
    for(size_t offset:{size_t(0x1e2),size_t(0x1e3),size_t(0x1e5),size_t(0x1e6),
                      size_t(0x1e7),size_t(0x1e9),size_t(0x1ea),size_t(0x1eb)}){
        fixture(p);p[offset]=0xa5;
        assert(nativewcl::decodeWpa2Join(p,nativewcl::messageBytes,out)==JoinResult::ok);
        assert(selection::valid(out)&&!memcmp(out.pmk,p+0x50,32));
        selection::wipe(&out,sizeof(out));
    }
    for(unsigned cipher:{0u,5u,7u,9u,10u}){fixture(p);word(p,0x48,cipher);failure(p,nativewcl::messageBytes,JoinResult::unsupportedKey);}
    for(unsigned len:{0u,16u,31u,33u,48u,64u,65u,0xffffffffu}){fixture(p);word(p,0x44,len);failure(p,nativewcl::messageBytes,JoinResult::unsupportedKey);}
    fixture(p);word(p,0x1ec,32);failure(p,nativewcl::messageBytes,JoinResult::unsupportedKey);
    for(unsigned len:{0u,19u,23u,257u,258u,65535u}){fixture(p);word(p,0xd4,len,2);failure(p,nativewcl::messageBytes,JoinResult::unsupportedRsn);}
    for(unsigned akm:{1u,5u,6u,8u,18u}){fixture(p);p[0xd6+19]=uint8_t(akm);failure(p,nativewcl::messageBytes,JoinResult::unsupportedRsn);}
    fixture(p);p[0xd6+7]=2;failure(p,nativewcl::messageBytes,JoinResult::unsupportedRsn); // TKIP group.
    fixture(p);p[0xd6+13]=2;failure(p,nativewcl::messageBytes,JoinResult::unsupportedRsn); // TKIP pairwise.
    fixture(p);p[0xd6+20]=0x80;failure(p,nativewcl::messageBytes,JoinResult::unsupportedRsn); // PMF unimplemented.
    fixture(p);p[0xd6+20]=0x40;failure(p,nativewcl::messageBytes,JoinResult::unsupportedRsn); // PMF required.
    // Structurally complete mixed PSK/SAE and nonzero PMKID lists cannot be
    // reduced to a supported PSK subset by this narrow extraction boundary.
    fixture(p);word(p,0xd4,26,2);p[0xd6+1]=24;word(p,0xd6+14,2,2);
    p[0xd6+20]=0;p[0xd6+21]=15;p[0xd6+22]=172;p[0xd6+23]=8;
    p[0xd6+24]=p[0xd6+25]=0;
    failure(p,nativewcl::messageBytes,JoinResult::unsupportedRsn);
    fixture(p);word(p,0xd4,40,2);p[0xd6+1]=38;word(p,0xd6+22,1,2);
    memset(p+0xd6+24,0x5a,16);
    failure(p,nativewcl::messageBytes,JoinResult::unsupportedRsn);
    for(unsigned count:{0u,2u,0xffffffffu}){fixture(p);word(p,nativewcl::countOffset,count);failure(p,nativewcl::messageBytes,JoinResult::invalidCandidate);}
    fixture(p);p[nativewcl::recordOffset]=1;failure(p,nativewcl::messageBytes,JoinResult::invalidCandidate);
    fixture(p);p[nativewcl::recordOffset+10]=2;failure(p,nativewcl::messageBytes,JoinResult::invalidCandidate);
    fixture(p);p[nativewcl::recordOffset+4]|=1;failure(p,nativewcl::messageBytes,JoinResult::invalidCandidate);
    fixture(p);memset(p+nativewcl::recordOffset+4,0,6);failure(p,nativewcl::messageBytes,JoinResult::invalidCandidate);
    // Real output alias: constructing then replacing the byte representation
    // is safe because the decoder only writes (does not read) the old Join.
    alignas(selection::Join) uint8_t alias[nativewcl::messageBytes]{};
    for(size_t offset:{size_t(0),size_t(0x20),size_t(0x50),size_t(0xd4),
                      size_t(0x214),sizeof(alias)-sizeof(selection::Join)}){
        assert(offset%alignof(selection::Join)==0);
        auto *same=new (alias+offset) selection::Join;fixture(alias);
        assert(nativewcl::decodeWpa2Join(alias,sizeof(alias),*same)==JoinResult::ok);
        assert(!memcmp(same->ssid,name,sizeof(name))&&!memcmp(same->bssid,mac,6));
        for(unsigned i=0;i<32;++i)assert(same->pmk[i]==uint8_t(i^0xa5));
        selection::wipe(same,sizeof(*same));
        fixture(alias);word(alias,0x44,64);
        assert(nativewcl::decodeWpa2Join(alias,sizeof(alias),*same)==JoinResult::unsupportedKey&&zero(*same));
        fixture(alias);word(alias,nativewcl::countOffset,2);
        assert(nativewcl::decodeWpa2Join(alias,sizeof(alias),*same)==JoinResult::invalidCandidate&&zero(*same));
    }
    puts("WCL WPA2 credential extraction passed: exact bytes, strict security, no empty-key fallback, bounds and zeroed failures");
}
