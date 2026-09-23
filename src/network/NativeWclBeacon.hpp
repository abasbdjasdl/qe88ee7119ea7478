// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "NativeScanCache.hpp"

namespace rtl8852be { namespace network { namespace nativewclbeacon {

// OFFLINE byte draft for one hash-pinned KC, never a C++ private-ABI struct.
// This helper does not emit a message, start a scan, or establish WCL readiness.
// Evidence and unresolved lifecycle requirements: docs/native-wcl-notifications.md.
constexpr size_t metadataBytes=64,maxIeBytes=2048,maxPayloadBytes=metadataBytes+maxIeBytes;
constexpr uint32_t scanResultEvent=201;
constexpr char kernelSha256[]="d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1";
enum class TargetProfile : uint8_t { unknown,darwin24_4_0_d8b50fc2 };
enum class Status : uint8_t {
    knownPartial,unsupportedProfile,invalidBuffer,bufferTooSmall,overlap,
    unsupportedChannel,invalidBssid,invalidSsid,invalidSignal,invalidIe
};
struct Result {
    Status status{Status::invalidBuffer};
    size_t payloadBytes{};
    bool emissionReady{false}; // Always false, including knownPartial.
};

namespace detail {
inline size_t writableBytes(size_t capacity){return capacity<maxPayloadBytes?capacity:maxPayloadBytes;}
inline void clear(void *destination,size_t length){
    auto *p=static_cast<uint8_t*>(destination);for(size_t i=0;i<length;++i)p[i]=0;
}
inline void copy(uint8_t *destination,const uint8_t *source,size_t length){
    for(size_t i=0;i<length;++i)destination[i]=source[i];
}
inline bool overlaps(const void *a,size_t na,const void *b,size_t nb){
    if(!na||!nb)return false;
    const auto aa=reinterpret_cast<uintptr_t>(a),bb=reinterpret_cast<uintptr_t>(b);
    // Subtraction after ordering avoids wraparound and unrelated-pointer UB.
    return aa<=bb?bb-aa<na:aa-bb<nb;
}
inline Result reject(Status status,void *destination,size_t capacity){
    if(destination)clear(destination,writableBytes(capacity));
    return {status,0,false};
}
inline void little16(uint8_t *p,uint16_t value){p[0]=uint8_t(value);p[1]=uint8_t(value>>8);}
inline void little32(uint8_t *p,uint32_t value){
    p[0]=uint8_t(value);p[1]=uint8_t(value>>8);p[2]=uint8_t(value>>16);p[3]=uint8_t(value>>24);
}
inline bool validBssid(const uint8_t *address){
    uint8_t any=0;for(unsigned i=0;i<6;++i)any|=address[i];
    return any&&!(address[0]&1);
}
inline bool completeIes(const nativescan::Entry &entry){
    bool ssidSeen=false;
    for(size_t offset=0;offset<entry.ieLength;){
        if(entry.ieLength-offset<2)return false;
        const uint8_t tag=entry.ies[offset],length=entry.ies[offset+1];
        offset+=2;if(length>entry.ieLength-offset)return false;
        if(tag==0){
            if(ssidSeen||length>32||length!=entry.ssidLength)return false;
            for(size_t i=0;i<length;++i)if(entry.ies[offset+i]!=entry.ssid[i])return false;
            ssidSeen=true;
        }
        offset+=length;
    }
    // A hidden SSID has a real zero-length SSID IE. Missing SSID IEs do not
    // establish even a hidden name, and must not enable our supplied-SSID flag.
    return ssidSeen;
}
}

// Caller supplies a readable, locally owned Entry and writable destination of
// capacity bytes, with both kept stable under its scan-cache/controller gate.
// Entry is borrowed by reference: no Store/Entry/whole-payload stack copies.
// Any overlap with the Entry is rejected. Rejection wipes the writable prefix
// min(capacity,2112); this also erases that region of an illegally aliased Entry.
// Bytes beyond that prefix are outside this helper's output. On all failures
// payloadBytes=0, preventing a prior successful draft from being reused.
inline Result encode(TargetProfile profile,const nativescan::Entry &entry,
                     void *destination,size_t capacity){
    if(!destination)return detail::reject(Status::invalidBuffer,destination,capacity);
    if(detail::overlaps(destination,detail::writableBytes(capacity),&entry,sizeof(entry)))
        return detail::reject(Status::overlap,destination,capacity);
    if(profile!=TargetProfile::darwin24_4_0_d8b50fc2)
        return detail::reject(Status::unsupportedProfile,destination,capacity);
    if(capacity<metadataBytes)return detail::reject(Status::bufferTooSmall,destination,capacity);
    if(entry.channel.band!=nativescan::Band::ghz2||entry.channel.number<1||entry.channel.number>11)
        return detail::reject(Status::unsupportedChannel,destination,capacity);
    if(!detail::validBssid(entry.bssid))return detail::reject(Status::invalidBssid,destination,capacity);
    if(entry.ssidLength>sizeof(entry.ssid))return detail::reject(Status::invalidSsid,destination,capacity);
    if(!nativescan::valid(entry.signal))return detail::reject(Status::invalidSignal,destination,capacity);
    if(entry.ieLength>maxIeBytes||!detail::completeIes(entry))
        return detail::reject(Status::invalidIe,destination,capacity);
    const size_t length=metadataBytes+entry.ieLength;
    if(capacity<length)return detail::reject(Status::bufferTooSmall,destination,capacity);

    // Clear padding, unknown bytes, and unused tail from an earlier draft.
    detail::clear(destination,detail::writableBytes(capacity));
    auto *out=static_cast<uint8_t*>(destination);
    detail::little32(out,uint32_t(entry.ieLength));
    detail::little16(out+4,uint16_t(0x1000u|entry.channel.number));
    detail::copy(out+6,entry.ssid,entry.ssidLength);out[0x26]=entry.ssidLength;
    out[0x27]=entry.channel.number;detail::copy(out+0x29,entry.bssid,6);
    uint32_t flags=0x2; // SSID is verified against this observation's actual IE.
    if(entry.signal.unit==nativescan::SignalUnit::dbm){
        detail::little32(out+0x30,uint32_t(int32_t(entry.signal.value)));flags|=0x4000;
    }
    // A percent/unknown signal is not converted or marked as valid dBm.
    detail::little16(out+0x38,entry.beaconInterval);
    detail::little16(out+0x3a,entry.capability);
    detail::little32(out+0x3c,flags);
    detail::copy(out+metadataBytes,entry.ies,entry.ieLength);
    return {Status::knownPartial,length,false};
}

static_assert(maxPayloadBytes==2112,"exact-KC WCL scan-result bound");
} } }
