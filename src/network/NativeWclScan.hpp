// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace rtl8852be { namespace network { namespace nativewclscan {

// Pure decoding of a locally owned buffer for ONE KC, never a private ABI
// struct or permission to start a scan. See docs/native-wcl-scan.md for the
// instruction evidence and separately pinned Broadcom firmware semantics.
constexpr size_t messageBytes=0x1550,channelOffset=0x58,channelStride=12;
constexpr size_t maxChannels=11;
constexpr uint16_t channelMask=0x07ff;
constexpr uint32_t maxDwellMs=1000,maxHomeMs=60000;
constexpr char kernelSha256[]="d8b50fc25bbe4c9f6923a9344ae34e760e1c98b06b23513e4a73e494019865e1";

enum class TargetProfile : uint8_t {unknown,darwin24_4_0_d8b50fc2};
enum class PrivateMacPolicy : uint8_t {unknown,unsupported,enabled};
enum class Mode : uint8_t {none,active,passive};
enum class BssType : uint8_t {none,infrastructure=2,any=3};
enum class Status : uint8_t {
    knownSubset,invalidBuffer,invalidLength,unsupportedProfile,unsupportedPolicy,
    unsupportedMode,unsupportedFlags,unsupportedFilters,invalidChannels,
    unsupportedChannel,unsupportedTiming
};
struct Policy {
    // These are trusted driver snapshots, NEVER values from the scan input.
    // No implicit default-all mask. Bit 0 is channel 1, bit 10 is channel 11.
    TargetProfile profile{};
    PrivateMacPolicy privateMac{};
    uint16_t permittedActiveChannels{},permittedPassiveChannels{};
};
struct Request {
    // +0 is copied by the producer but no consumer version gate was proved.
    // It is not a scan ID and cannot replace verifying the runtime KC profile.
    uint32_t opaqueSourceHeader{};
    uint32_t activeDwellMs{},passiveDwellMs{},homeRestMs{},homeAwayMs{};
    uint16_t requestedFlags{};
    Mode mode{};
    BssType bssType{};
    uint8_t channelCount{},channels[maxChannels]{};
    bool refreshPrivateMacRequested{},allowProhibitedRequested{},homeRestDefault{};
};
struct Result {
    Status status{Status::invalidBuffer};
    bool emissionReady{false}; // Always false, even for knownSubset.
};

namespace detail {
inline uint16_t little16(const uint8_t *p){return uint16_t(p[0])|uint16_t(uint16_t(p[1])<<8);}
inline uint32_t little32(const uint8_t *p){
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
inline void clear(void *p,size_t n){auto *b=static_cast<uint8_t*>(p);for(size_t i=0;i<n;++i)b[i]=0;}
inline bool zero(const uint8_t *p,size_t n){for(size_t i=0;i<n;++i)if(p[i])return false;return true;}
inline Result finish(Request &out,Request &decoded,Status status){
    // All input/policy reads finish before the first output write. Thus an
    // appropriately aligned, live Request can alias any part of the input.
    // Copy the initialized representation so padding cannot reveal old bytes.
    if(status==Status::knownSubset){
        auto *to=reinterpret_cast<uint8_t*>(&out);
        const auto *from=reinterpret_cast<const uint8_t*>(&decoded);
        for(size_t i=0;i<sizeof(out);++i)to[i]=from[i];
    }else clear(&out,sizeof(out));
    clear(&decoded,sizeof(decoded));
    return {status,false};
}
}

// Caller supplies readable, stable local storage of length bytes and writable
// out. Kernel-pointer validation and request/generation ownership are outside
// this pure helper. Policy is passed by value before out can overwrite it.
// This never reads saved SSIDs/keys, keeps input pointers, allocates, or calls a
// radio. Channels retain input order. A rejected channel is never dropped.
inline Result decode(const void *bytes,size_t length,Policy policy,Request &out){
    Request decoded;detail::clear(&decoded,sizeof(decoded));
    if(!bytes)return detail::finish(out,decoded,Status::invalidBuffer);
    if(length!=messageBytes)return detail::finish(out,decoded,Status::invalidLength);
    if(policy.profile!=TargetProfile::darwin24_4_0_d8b50fc2)
        return detail::finish(out,decoded,Status::unsupportedProfile);
    // The exact Apple consumer permits refresh=1 to be a no-op when private
    // scan MAC is unsupported. Do not silently ignore an enabled/unknown state.
    if(policy.privateMac!=PrivateMacPolicy::unsupported||
       (policy.permittedActiveChannels&~channelMask)||(policy.permittedPassiveChannels&~channelMask))
        return detail::finish(out,decoded,Status::unsupportedPolicy);
    const auto *p=static_cast<const uint8_t*>(bytes);
    if(p[4]>1||!detail::zero(p+5,3)||detail::little32(p+0x0c)||
       !detail::zero(p+0x1a,2)||!detail::zero(p+0x46,2))
        return detail::finish(out,decoded,Status::unsupportedPolicy);
    const uint32_t mode=detail::little32(p+0x40),bssType=detail::little32(p+0x10);
    if((mode!=1&&mode!=2)||(bssType!=2&&bssType!=3))
        return detail::finish(out,decoded,Status::unsupportedMode);
    const uint16_t flags=detail::little16(p+0x44);
    if(flags!=0&&flags!=8)return detail::finish(out,decoded,Status::unsupportedFlags);
    // Zero-length filters with nonzero backing data are rejected as well.
    // The normal undirected profile never requires carrying a name or BSSID.
    if(!detail::zero(p+0x14,6)||detail::little32(p+0x1c)||!detail::zero(p+0x20,32)||
       detail::little32(p+0x1318)||!detail::zero(p+0x131c,560)||detail::little32(p+0x154c))
        return detail::finish(out,decoded,Status::unsupportedFilters);
    const uint32_t count=detail::little32(p+0x54);
    if(!count||count>maxChannels)return detail::finish(out,decoded,Status::invalidChannels);
    uint16_t seen=0;
    const uint16_t permitted=mode==1?policy.permittedActiveChannels:policy.permittedPassiveChannels;
    for(uint32_t i=0;i<count;++i){
        const auto *entry=p+channelOffset+i*channelStride;
        const uint32_t channel=detail::little32(entry+4);
        // Exact canonical apple80211_channel: version1, 20MHz + 2GHz.
        // No implicit band inference, chanspec reinterpretation or flags mask.
        if(detail::little32(entry)!=1||detail::little32(entry+8)!=0x0a||channel<1||channel>11)
            return detail::finish(out,decoded,Status::unsupportedChannel);
        const uint16_t bit=uint16_t(1u<<(channel-1));
        if(seen&bit)return detail::finish(out,decoded,Status::invalidChannels);
        // WCL flag8 maps to WL_SCANFLAGS_PROHIBITED. It never expands our
        // trusted regulatory/hardware mask, even when the request asks for it.
        if(!(permitted&bit))return detail::finish(out,decoded,Status::unsupportedChannel);
        seen|=bit;decoded.channels[i]=uint8_t(channel);
    }
    const uint32_t active=detail::little32(p+0x48),passive=detail::little32(p+0x4c);
    const uint32_t home=detail::little32(p+0x50),away=detail::little32(p+8);
    // Local bounded profile limits, not claimed Apple ABI maxima. Never clamp
    // a request or silently substitute the backend's fixed dwell duration.
    if(!active||active>maxDwellMs||!passive||passive>maxDwellMs||home>maxHomeMs||away>maxHomeMs)
        return detail::finish(out,decoded,Status::unsupportedTiming);
    decoded.opaqueSourceHeader=detail::little32(p);
    decoded.activeDwellMs=active;decoded.passiveDwellMs=passive;
    decoded.homeRestMs=home;decoded.homeAwayMs=away;decoded.homeRestDefault=home==0;
    decoded.requestedFlags=flags;decoded.allowProhibitedRequested=(flags&8)!=0;
    decoded.mode=mode==1?Mode::active:Mode::passive;decoded.bssType=BssType(bssType);
    decoded.channelCount=uint8_t(count);decoded.refreshPrivateMacRequested=p[4]!=0;
    return detail::finish(out,decoded,Status::knownSubset);
}

static_assert(channelOffset+400*channelStride==0x1318,"pinned-KC channel array boundary");
static_assert(0x131c+560==0x154c&&0x154c+4==messageBytes,"pinned-KC filter/tail boundary");
static_assert(sizeof(Request)<=128,"small bounded decoder temporary, no wire-buffer stack copy");
} } }
