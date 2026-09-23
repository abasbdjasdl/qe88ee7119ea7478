// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <memory>
#include "../src/network/NativeWclBeacon.hpp"

namespace ns=rtl8852be::network::nativescan;
namespace nb=rtl8852be::network::nativewclbeacon;
constexpr auto profile=nb::TargetProfile::darwin24_4_0_d8b50fc2;

static uint16_t little16(const uint8_t *p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
static uint32_t little32(const uint8_t *p){
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
static bool allZero(const uint8_t *p,size_t n){for(size_t i=0;i<n;++i)if(p[i])return false;return true;}
static void fresh(ns::Entry &e){
    memset(&e,0,sizeof(e));e.channel={ns::Band::ghz2,6};e.signal={ns::SignalUnit::dbm,-63};
    e.bssid[0]=2;e.bssid[5]=9;e.ssidLength=3;e.ssid[0]='A';e.ssid[1]=0;e.ssid[2]=0xff;
    e.ies[0]=0;e.ies[1]=3;memcpy(e.ies+2,e.ssid,3);e.ies[5]=1;e.ies[6]=1;e.ies[7]=0x82;
    e.ieLength=8;e.beaconInterval=100;e.capability=0x431;e.observedAtUs=123;
}
static void rejected(const ns::Entry &e,nb::Status expected,uint8_t *buffer,
                     size_t capacity=nb::maxPayloadBytes,nb::TargetProfile p=profile){
    memset(buffer,0xa5,nb::maxPayloadBytes+8);
    auto result=nb::encode(p,e,buffer,capacity);
    assert(result.status==expected&&result.payloadBytes==0&&!result.emissionReady);
    const size_t n=capacity<nb::maxPayloadBytes?capacity:nb::maxPayloadBytes;
    assert(allZero(buffer,n));for(size_t i=n;i<nb::maxPayloadBytes+8;++i)assert(buffer[i]==0xa5);
}
static void fillIes(ns::Entry &e,size_t n){
    assert(n>=2&&n<=ns::maxIeBytes);e.ssidLength=0;e.ies[0]=0;e.ies[1]=0;
    size_t off=2;
    while(off<n){
        assert(n-off>=2);size_t len=n-off-2;if(len>255)len=255;
        if(n-off-(len+2)==1)--len;
        e.ies[off]=221;e.ies[off+1]=uint8_t(len);
        for(size_t i=0;i<len;++i)e.ies[off+2+i]=uint8_t(i);
        off+=len+2;
    }
    e.ieLength=uint16_t(n);
}
int main(){
    auto entry=std::make_unique<ns::Entry>();auto output=std::make_unique<uint8_t[]>(nb::maxPayloadBytes+8);
    auto *out=output.get();auto &e=*entry;fresh(e);
    memset(out,0xa5,nb::maxPayloadBytes+8);
    auto result=nb::encode(profile,e,out,nb::maxPayloadBytes);
    assert(result.status==nb::Status::knownPartial&&result.payloadBytes==72&&!result.emissionReady);
    assert(little32(out)==8&&little16(out+4)==0x1006&&out[0x26]==3&&out[0x27]==6);
    assert(memcmp(out+6,e.ssid,3)==0&&memcmp(out+0x29,e.bssid,6)==0);
    assert(little32(out+0x30)==uint32_t(int32_t(-63))&&little16(out+0x38)==100);
    assert(little16(out+0x3a)==0x431&&little32(out+0x3c)==0x4002);
    assert(out[0x28]==0&&out[0x2f]==0&&allZero(out+0x34,4));
    assert(allZero(out+9,29)&&memcmp(out+64,e.ies,8)==0&&allZero(out+72,nb::maxPayloadBytes-72));
    for(size_t i=nb::maxPayloadBytes;i<nb::maxPayloadBytes+8;++i)assert(out[i]==0xa5);

    // Exact-sized output proves no dependence on an always-2112-byte allocation.
    auto exact=std::make_unique<uint8_t[]>(result.payloadBytes);
    assert(nb::encode(profile,e,exact.get(),result.payloadBytes).status==nb::Status::knownPartial);
    for(uint8_t channel=1;channel<=11;++channel){
        e.channel.number=channel;assert(nb::encode(profile,e,out,2112).status==nb::Status::knownPartial);
        assert(little16(out+4)==uint16_t(0x1000|channel)&&out[0x27]==channel);
    }
    for(auto unit:{ns::SignalUnit::unknown,ns::SignalUnit::percent}){
        fresh(e);e.signal={unit,0};assert(nb::encode(profile,e,out,2112).status==nb::Status::knownPartial);
        assert(little32(out+0x30)==0&&little32(out+0x3c)==2);
    }
    for(int value:{0,100}){
        fresh(e);e.signal={ns::SignalUnit::percent,int16_t(value)};
        assert(nb::encode(profile,e,out,2112).status==nb::Status::knownPartial&&little32(out+0x3c)==2);
    }
    for(int value:{-127,0}){
        fresh(e);e.signal={ns::SignalUnit::dbm,int16_t(value)};
        assert(nb::encode(profile,e,out,2112).status==nb::Status::knownPartial);
        assert(little32(out+0x30)==uint32_t(int32_t(value))&&little32(out+0x3c)==0x4002);
    }
    for(auto signal:{ns::Signal{ns::SignalUnit::dbm,-128},ns::Signal{ns::SignalUnit::dbm,1},
                    ns::Signal{ns::SignalUnit::percent,-1},ns::Signal{ns::SignalUnit::percent,101},
                    ns::Signal{ns::SignalUnit::unknown,1},ns::Signal{ns::SignalUnit(255),0}}){
        fresh(e);e.signal=signal;rejected(e,nb::Status::invalidSignal,out);
    }
    for(uint8_t channel:{uint8_t(0),uint8_t(12),uint8_t(14),uint8_t(255)}){
        fresh(e);e.channel.number=channel;rejected(e,nb::Status::unsupportedChannel,out);
    }
    fresh(e);e.channel={ns::Band::ghz5,36};rejected(e,nb::Status::unsupportedChannel,out);
    e.channel={ns::Band(255),6};rejected(e,nb::Status::unsupportedChannel,out);
    fresh(e);memset(e.bssid,0,6);rejected(e,nb::Status::invalidBssid,out);
    fresh(e);e.bssid[0]=1;rejected(e,nb::Status::invalidBssid,out);
    fresh(e);memset(e.bssid,0xff,6);rejected(e,nb::Status::invalidBssid,out);
    fresh(e);e.ssidLength=33;rejected(e,nb::Status::invalidSsid,out);

    // Hidden and full-length binary SSIDs are admitted only with matching IEs.
    fresh(e);fillIes(e,2);result=nb::encode(profile,e,out,2112);
    assert(result.status==nb::Status::knownPartial&&result.payloadBytes==66&&out[0x26]==0&&little32(out)==2);
    fresh(e);e.ssidLength=32;e.ies[1]=32;e.ieLength=34;
    for(unsigned i=0;i<32;++i)e.ssid[i]=e.ies[i+2]=uint8_t(i);
    assert(nb::encode(profile,e,out,2112).status==nb::Status::knownPartial);
    fresh(e);e.ies[2]^=1;rejected(e,nb::Status::invalidIe,out);
    fresh(e);e.ies[1]=2;rejected(e,nb::Status::invalidIe,out);
    fresh(e);e.ieLength=1;rejected(e,nb::Status::invalidIe,out);
    fresh(e);e.ieLength=0;rejected(e,nb::Status::invalidIe,out);
    fresh(e);e.ieLength=4;rejected(e,nb::Status::invalidIe,out);
    fresh(e);e.ies[8]=0;e.ies[9]=3;memcpy(e.ies+10,e.ssid,3);e.ieLength=13;
    rejected(e,nb::Status::invalidIe,out); // Even identical duplicate SSID is rejected.
    fresh(e);e.ssidLength=0;e.ies[0]=1;e.ies[1]=1;e.ies[2]=0x82;e.ieLength=3;
    rejected(e,nb::Status::invalidIe,out); // No SSID IE is not evidence of hidden SSID.
    fresh(e);e.ies[0]=221;rejected(e,nb::Status::invalidIe,out); // Named SSID absent.
    fresh(e);e.ies[1]=33;e.ieLength=35;rejected(e,nb::Status::invalidIe,out);
    fresh(e);fillIes(e,2048);result=nb::encode(profile,e,out,2112);
    assert(result.status==nb::Status::knownPartial&&result.payloadBytes==2112&&little32(out)==2048);
    assert(memcmp(out+64,e.ies,2048)==0);
    fillIes(e,2049);rejected(e,nb::Status::invalidIe,out);
    fillIes(e,2304);rejected(e,nb::Status::invalidIe,out);
    e.ieLength=UINT16_MAX;rejected(e,nb::Status::invalidIe,out); // No out-of-range IE reads.

    fresh(e);rejected(e,nb::Status::unsupportedProfile,out,2112,nb::TargetProfile::unknown);
    rejected(e,nb::Status::unsupportedProfile,out,2112,nb::TargetProfile(255));
    for(size_t cap:{size_t(0),size_t(1),size_t(63),size_t(64),size_t(71)})
        rejected(e,nb::Status::bufferTooSmall,out,cap);
    for(size_t cap:{size_t(0),size_t(1),size_t(63),size_t(64),size_t(71)}){
        auto small=std::make_unique<uint8_t[]>(cap);
        result=nb::encode(profile,e,small.get(),cap);
        assert(result.status==nb::Status::bufferTooSmall&&allZero(small.get(),cap));
    }
    result=nb::encode(profile,e,nullptr,SIZE_MAX);
    assert(result.status==nb::Status::invalidBuffer&&!result.payloadBytes&&!result.emissionReady);
    assert(nb::encode(profile,e,out,SIZE_MAX).status==nb::Status::knownPartial);
    // All written bytes are bounded even if the caller reports excess capacity.
    for(size_t i=2112;i<2120;++i)assert(out[i]==0xa5);

    // Reject complete and partial aliases before reading then wiping the Entry.
    fresh(e);auto *entryBytes=reinterpret_cast<uint8_t*>(&e);
    result=nb::encode(profile,e,entryBytes,sizeof(e));
    assert(result.status==nb::Status::overlap&&!result.payloadBytes&&allZero(entryBytes,2112));
    fresh(e);result=nb::encode(profile,e,e.ies,128);
    assert(result.status==nb::Status::overlap&&allZero(e.ies,128));
    fresh(e);result=nb::encode(profile,e,entryBytes+sizeof(e)-1,1);
    assert(result.status==nb::Status::overlap&&entryBytes[sizeof(e)-1]==0);
    // Placement in one backing allocation exercises destination before Entry.
    struct alignas(ns::Entry) Aliased {uint8_t prefix[64];ns::Entry entry;};
    auto alias=std::make_unique<Aliased>();fresh(alias->entry);
    result=nb::encode(profile,alias->entry,reinterpret_cast<uint8_t*>(alias.get()),65);
    assert(result.status==nb::Status::overlap&&allZero(alias->prefix,64));
    fresh(alias->entry);result=nb::encode(profile,alias->entry,alias->prefix,64);
    assert(result.status==nb::Status::bufferTooSmall); // Exactly adjacent, no overlap.

    // Malformed/random IE tails cannot produce a stale successful result.
    uint32_t random=0x136ac09d;
    for(unsigned trial=0;trial<1500;++trial){
        fresh(e);random=random*1664525u+1013904223u;e.ieLength=uint16_t(random%2049);
        for(unsigned i=0;i<e.ieLength;++i){random=random*1664525u+1013904223u;e.ies[i]=uint8_t(random>>24);}
        memset(out,0xa5,2120);result=nb::encode(profile,e,out,2112);
        assert(!result.emissionReady);
        if(result.status==nb::Status::knownPartial){
            assert(result.payloadBytes==64+e.ieLength&&little32(out)==e.ieLength);
            assert(memcmp(out+64,e.ies,e.ieLength)==0);
        }else assert(result.payloadBytes==0&&allZero(out,2112));
    }
    puts("Native WCL beacon offline encoding bounds and alias tests passed");
}
