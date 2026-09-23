// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/NativeWclScan.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <new>

using namespace rtl8852be::network::nativewclscan;
static size_t checks;
#define CHECK(x) do { ++checks; assert(x); } while(0)
static void word(uint8_t *p,size_t off,uint32_t v,unsigned n=4){
    for(unsigned i=0;i<n;++i)p[off+i]=uint8_t(v>>(i*8));
}
static Policy trusted(){
    return {TargetProfile::darwin24_4_0_d8b50fc2,PrivateMacPolicy::unsupported,channelMask,channelMask};
}
static void fixture(uint8_t *p,uint32_t mode=1,uint16_t flags=8){
    std::memset(p,0,messageBytes);word(p,0,1);p[4]=1;word(p,8,100);
    word(p,0x10,3);word(p,0x40,mode);word(p,0x44,flags,2);
    word(p,0x48,40);word(p,0x4c,110);word(p,0x50,45);word(p,0x54,3);
    constexpr uint8_t channels[]={1,6,11};
    for(size_t i=0;i<3;++i){
        word(p,channelOffset+i*channelStride,1);
        word(p,channelOffset+i*channelStride+4,channels[i]);
        word(p,channelOffset+i*channelStride+8,0x0a);
    }
}
static bool zero(const void *p,size_t n){
    const auto *b=static_cast<const uint8_t*>(p);
    for(size_t i=0;i<n;++i)if(b[i])return false;return true;
}
static void rejected(const void *bytes,size_t length,Status expected,Policy policy=trusted()){
    struct Guarded {uint32_t pre;Request request;uint32_t post;} out;
    std::memset(&out,0xa5,sizeof(out));
    const Result result=decode(bytes,length,policy,out.request);
    CHECK(result.status==expected);CHECK(!result.emissionReady);
    CHECK(zero(&out.request,sizeof(out.request)));
    CHECK(out.pre==0xa5a5a5a5&&out.post==0xa5a5a5a5);
}
static Request accepted(const uint8_t *p,Policy policy=trusted()){
    Request out;std::memset(&out,0xa5,sizeof(out));
    const Result result=decode(p,messageBytes,policy,out);
    CHECK(result.status==Status::knownSubset);CHECK(!result.emissionReady);
    CHECK(out.channelCount&&out.channelCount<=maxChannels);
    CHECK(out.mode==Mode::active||out.mode==Mode::passive);
    CHECK(out.requestedFlags==0||out.requestedFlags==8);
    CHECK(out.allowProhibitedRequested==(out.requestedFlags==8));
    CHECK(out.activeDwellMs&&out.activeDwellMs<=maxDwellMs);
    CHECK(out.passiveDwellMs&&out.passiveDwellMs<=maxDwellMs);
    CHECK(out.homeRestDefault==(out.homeRestMs==0));
    for(size_t i=out.channelCount;i<maxChannels;++i)CHECK(!out.channels[i]);
    return out;
}
static uint32_t randomWord(uint32_t &state){state^=state<<13;state^=state>>17;state^=state<<5;return state;}

int main(){
    // No real AP names, addresses, passwords, keys or local configuration.
    std::array<uint8_t,messageBytes+2> storage{};auto *p=storage.data()+1;
    for(uint32_t mode:{1u,2u})for(uint16_t flags:{uint16_t(0),uint16_t(8)})
        for(uint32_t bss:{2u,3u})for(uint8_t refresh:{uint8_t(0),uint8_t(1)}){
            fixture(p,mode,flags);word(p,0x10,bss);p[4]=refresh;
            const Request out=accepted(p);
            CHECK(out.mode==(mode==1?Mode::active:Mode::passive));CHECK(uint32_t(out.bssType)==bss);
            CHECK(out.channelCount==3&&out.channels[0]==1&&out.channels[1]==6&&out.channels[2]==11);
            CHECK(out.refreshPrivateMacRequested==(refresh!=0));
            CHECK(out.activeDwellMs==40&&out.passiveDwellMs==110&&out.homeRestMs==45&&out.homeAwayMs==100);
        }
    fixture(p);
    rejected(nullptr,messageBytes,Status::invalidBuffer);
    for(size_t n=0;n<messageBytes;++n)rejected(p,n,Status::invalidLength);
    for(size_t n:{messageBytes+1,SIZE_MAX})rejected(p,n,Status::invalidLength);
    Policy policy=trusted();policy.profile=TargetProfile::unknown;
    rejected(p,messageBytes,Status::unsupportedProfile,policy);
    policy=trusted();policy.profile=TargetProfile(255);
    rejected(p,messageBytes,Status::unsupportedProfile,policy);
    for(auto privateMac:{PrivateMacPolicy::unknown,PrivateMacPolicy::enabled,PrivateMacPolicy(255)}){
        policy=trusted();policy.privateMac=privateMac;
        for(unsigned refresh:{0u,1u}){fixture(p);p[4]=uint8_t(refresh);rejected(p,messageBytes,Status::unsupportedPolicy,policy);}
    }
    for(unsigned flag=2;flag<256;++flag){fixture(p);p[4]=uint8_t(flag);rejected(p,messageBytes,Status::unsupportedPolicy);}
    for(unsigned bit=11;bit<16;++bit){
        policy=trusted();policy.permittedActiveChannels|=uint16_t(1u<<bit);
        fixture(p);rejected(p,messageBytes,Status::unsupportedPolicy,policy);
        policy=trusted();policy.permittedPassiveChannels|=uint16_t(1u<<bit);
        rejected(p,messageBytes,Status::unsupportedPolicy,policy);
    }
    for(size_t offset:{size_t(5),size_t(6),size_t(7),size_t(0x0c),size_t(0x0d),size_t(0x0e),size_t(0x0f),
                       size_t(0x1a),size_t(0x1b),size_t(0x46),size_t(0x47)}){
        fixture(p);p[offset]=1;rejected(p,messageBytes,Status::unsupportedPolicy);
    }
    for(uint32_t mode:{0u,3u,4u,5u,9u,0xffffffffu}){
        fixture(p);word(p,0x40,mode);rejected(p,messageBytes,Status::unsupportedMode);
    }
    for(uint32_t bss:{0u,1u,4u,0xffffffffu}){
        fixture(p);word(p,0x10,bss);rejected(p,messageBytes,Status::unsupportedMode);
    }
    for(unsigned bit=0;bit<16;++bit)if(bit!=3)for(unsigned allowed:{0u,8u}){
        fixture(p);word(p,0x44,(1u<<bit)|allowed,2);rejected(p,messageBytes,Status::unsupportedFlags);
    }
    fixture(p);word(p,0x44,0xffff,2);rejected(p,messageBytes,Status::unsupportedFlags);
    for(size_t offset=0x14;offset<0x1a;++offset){fixture(p);p[offset]=2;rejected(p,messageBytes,Status::unsupportedFilters);}
    for(size_t offset=0x1c;offset<0x40;++offset){fixture(p);p[offset]=1;rejected(p,messageBytes,Status::unsupportedFilters);}
    for(size_t offset=0x1318;offset<messageBytes;++offset){fixture(p);p[offset]=1;rejected(p,messageBytes,Status::unsupportedFilters);}
    for(uint32_t count:{0u,12u,400u,401u,0xffffffffu}){
        fixture(p);word(p,0x54,count);rejected(p,messageBytes,Status::invalidChannels);
    }
    for(uint32_t count:{1u,11u}){
        fixture(p);word(p,0x54,count);
        for(uint32_t i=0;i<count;++i){
            word(p,channelOffset+i*12,1);word(p,channelOffset+i*12+4,count-i);word(p,channelOffset+i*12+8,0x0a);
        }
        const Request out=accepted(p);CHECK(out.channelCount==count);
        for(uint32_t i=0;i<count;++i)CHECK(out.channels[i]==count-i); // No sorting/truncation.
    }
    for(uint32_t channel:{0u,12u,13u,14u,36u,0x1001u,0xffffffffu}){
        fixture(p);word(p,channelOffset+4,channel);rejected(p,messageBytes,Status::unsupportedChannel);
    }
    for(uint32_t version:{0u,2u,0xffffffffu}){
        fixture(p);word(p,channelOffset,version);rejected(p,messageBytes,Status::unsupportedChannel);
    }
    for(uint32_t flags:{0u,2u,8u,0x12u,0x2002u,0x0eu,0xffffffffu}){
        fixture(p);word(p,channelOffset+8,flags);rejected(p,messageBytes,Status::unsupportedChannel);
    }
    for(unsigned bit=0;bit<32;++bit){
        fixture(p);word(p,channelOffset+8,0x0a^(1u<<bit));rejected(p,messageBytes,Status::unsupportedChannel);
    }
    fixture(p);word(p,channelOffset+channelStride+4,1);rejected(p,messageBytes,Status::invalidChannels);
    for(uint32_t mode:{1u,2u})for(uint16_t flags:{uint16_t(0),uint16_t(8)}){
        fixture(p,mode,flags);policy=trusted();
        if(mode==1)policy.permittedActiveChannels&=uint16_t(~(1u<<5));
        else policy.permittedPassiveChannels&=uint16_t(~(1u<<5));
        // "allow prohibited" is not permission to overrule trusted policy.
        rejected(p,messageBytes,Status::unsupportedChannel,policy);
    }
    policy=trusted();policy.permittedActiveChannels=0;
    fixture(p,2);accepted(p,policy);fixture(p,1);rejected(p,messageBytes,Status::unsupportedChannel,policy);
    policy=trusted();policy.permittedPassiveChannels=0;
    fixture(p,1);accepted(p,policy);fixture(p,2);rejected(p,messageBytes,Status::unsupportedChannel,policy);
    for(size_t offset:{size_t(0x48),size_t(0x4c)}){
        for(uint32_t value:{0u,maxDwellMs+1,0x80000000u,0xffffffffu}){
            fixture(p);word(p,offset,value);rejected(p,messageBytes,Status::unsupportedTiming);
        }
        for(uint32_t value:{1u,maxDwellMs}){fixture(p);word(p,offset,value);accepted(p);}
    }
    for(size_t offset:{size_t(8),size_t(0x50)}){
        for(uint32_t value:{maxHomeMs+1,0x80000000u,0xffffffffu}){
            fixture(p);word(p,offset,value);rejected(p,messageBytes,Status::unsupportedTiming);
        }
        for(uint32_t value:{0u,1u,maxHomeMs}){fixture(p);word(p,offset,value);accepted(p);}
    }
    // Header0 is retained as opaque, never mistaken for scan ID/version check.
    for(uint32_t value:{0u,1u,2u,0xffffffffu}){fixture(p);word(p,0,value);CHECK(accepted(p).opaqueSourceHeader==value);}
    fixture(p);std::memset(p+channelOffset+3*channelStride,0xa5,0x1318-channelOffset-3*channelStride);
    CHECK(accepted(p).channelCount==3); // Inactive channel slots are not a plan.

    alignas(Request) uint8_t alias[messageBytes]{};
    for(size_t offset:{size_t(0),size_t(4),size_t(0x40),channelOffset,size_t(0x1318),messageBytes-sizeof(Request)}){
        CHECK(offset%alignof(Request)==0);auto *out=new (alias+offset) Request;
        fixture(alias);const Result result=decode(alias,sizeof(alias),trusted(),*out);
        CHECK(result.status==Status::knownSubset&&!result.emissionReady);
        CHECK(out->channelCount==3&&out->channels[0]==1&&out->channels[1]==6&&out->channels[2]==11);
        CHECK(out->mode==Mode::active&&out->allowProhibitedRequested&&out->activeDwellMs==40);
        fixture(alias);word(alias,0x54,400);const Result failure=decode(alias,sizeof(alias),trusted(),*out);
        CHECK(failure.status==Status::invalidChannels&&!failure.emissionReady&&zero(out,sizeof(*out)));
        fixture(alias);word(alias,0x48,0);CHECK(decode(alias,sizeof(alias),trusted(),*out).status==Status::unsupportedTiming);
        CHECK(zero(out,sizeof(*out)));
    }
    // Deterministic mutation fuzz, plus fully random exact-size buffers. Only
    // byte storage is fed to the parser; no pointer or enum fabrication in input.
    uint32_t state=0xa652915du;
    for(unsigned trial=0;trial<12000;++trial){
        fixture(p,1+(randomWord(state)&1),uint16_t((randomWord(state)&1)*8));
        const unsigned changes=randomWord(state)%16;
        for(unsigned i=0;i<changes;++i)p[randomWord(state)%messageBytes]=uint8_t(randomWord(state));
        const size_t length=trial%7?messageBytes:randomWord(state)%(messageBytes+2);
        Request out;std::memset(&out,0xa5,sizeof(out));const Result result=decode(p,length,trusted(),out);
        CHECK(!result.emissionReady);
        if(result.status==Status::knownSubset){
            CHECK(length==messageBytes);CHECK(out.channelCount>0&&out.channelCount<=11);
            CHECK(out.requestedFlags==0||out.requestedFlags==8);
            CHECK(out.mode==Mode::active||out.mode==Mode::passive);
            uint16_t seen=0;
            for(unsigned i=0;i<out.channelCount;++i){
                CHECK(out.channels[i]>=1&&out.channels[i]<=11);
                const auto bit=uint16_t(1u<<(out.channels[i]-1));CHECK(!(seen&bit));seen|=bit;
            }
            CHECK(out.activeDwellMs&&out.activeDwellMs<=maxDwellMs);
            CHECK(out.passiveDwellMs&&out.passiveDwellMs<=maxDwellMs);
        }else CHECK(zero(&out,sizeof(out)));
    }
    for(unsigned trial=0;trial<512;++trial){
        for(size_t i=0;i<messageBytes;++i)p[i]=uint8_t(randomWord(state));
        Request out;const Result result=decode(p,messageBytes,trusted(),out);
        CHECK(result.status!=Status::knownSubset&&!result.emissionReady&&zero(&out,sizeof(out)));
    }
    std::printf("WCL scan decoder: %zu checks, exact lengths, faithful mode/policy, alias, zeroed failures, 12512 fuzz cases; no hardware emission\n",checks);
}
