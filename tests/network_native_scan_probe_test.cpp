// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/NativeScanProbe.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace probe=rtl8852be::network::scanprobe;
static unsigned checks;
static void check(bool ok,int line){++checks;if(!ok){std::fprintf(stderr,"line %d\n",line);std::abort();}}
#define CHECK(x) check(bool(x),__LINE__)
static bool zero(const uint8_t *p,size_t n){for(size_t i=0;i<n;++i)if(p[i])return false;return true;}
int main(){
    // Independent wire examples: wildcard SSID, Supported Rates <=8, then
    // Extended Supported Rates. Preserve basic-rate bits from the real source.
    const uint8_t rates[]={0x82,0x84,0x8b,0x96,12,18,24,36,48,72,96,108};
    const uint8_t expected[]={0,0,1,8,0x82,0x84,0x8b,0x96,12,18,24,36,50,4,48,72,96,108};
    struct Guard {uint8_t before[8],bytes[probe::maxBytes],after[8];} out;
    std::memset(&out,0xa5,sizeof(out));
    auto r=probe::encode(rates,12,out.bytes,sizeof(out.bytes));
    CHECK(r.error==probe::Error::none&&r.length==sizeof(expected));
    CHECK(!std::memcmp(out.bytes,expected,sizeof(expected)));
    for(auto b:out.before)CHECK(b==0xa5);for(auto b:out.after)CHECK(b==0xa5);
    for(size_t n=1;n<=12;++n){
        r=probe::encode(rates,n,out.bytes,sizeof(out.bytes));
        CHECK(r.error==probe::Error::none&&r.length==n+4+(n>8?2:0));
        size_t at=0,seen=0,counted=0;
        while(at<r.length){
            const unsigned id=out.bytes[at++],length=out.bytes[at++];
            CHECK(at+length<=r.length);
            if(id==0){CHECK(!seen&&!length);seen|=1;}
            else {CHECK((id==1&&length<=8&&!(seen&2))||(id==50&&n>8&&length==n-8&&!(seen&4)));
                seen|=id==1?2:4;
                for(unsigned i=0;i<length;++i)CHECK(out.bytes[at+i]==rates[counted++]);}
            at+=length;
        }
        CHECK(counted==n&&seen==(n>8?7u:3u));
        CHECK(zero(out.bytes+r.length,sizeof(out.bytes)-r.length));
        for(size_t cap=0;cap<r.length;++cap){
            std::memset(out.bytes,0xa5,sizeof(out.bytes));
            auto shortResult=probe::encode(rates,n,out.bytes,cap);
            CHECK(shortResult.error==probe::Error::buffer&&!shortResult.length&&zero(out.bytes,cap));
            for(size_t i=cap;i<sizeof(out.bytes);++i)CHECK(out.bytes[i]==0xa5);
        }
    }
    CHECK(probe::encode(rates,12,nullptr,18).error==probe::Error::buffer);
    for(unsigned invalid=0;invalid<5;++invalid){
        uint8_t bad[13];std::memcpy(bad,rates,12);bad[12]=2;
        size_t n=12;if(invalid==0)n=0;if(invalid==1)n=13;
        if(invalid==2)bad[3]=0;if(invalid==3)bad[3]=0xff;if(invalid==4)bad[3]=2;
        std::memset(out.bytes,0xa5,sizeof(out.bytes));r=probe::encode(bad,n,out.bytes,sizeof(out.bytes));
        CHECK(r.error==probe::Error::rates&&!r.length&&zero(out.bytes,sizeof(out.bytes)));
    }
    for(size_t offset=0;offset<probe::maxBytes;++offset){
        std::memset(out.bytes,2,sizeof(out.bytes));
        r=probe::encode(out.bytes+offset,1,out.bytes,sizeof(out.bytes));
        CHECK(r.error==probe::Error::overlap&&!r.length&&zero(out.bytes,sizeof(out.bytes)));
    }
    // All 256 encodings: accept only genuine legacy rates, including basic bit.
    for(unsigned value=0;value<256;++value){
        const uint8_t b=uint8_t(value);bool known=false;
        for(auto expectedRate:rates)if((expectedRate&127)==(b&127))known=true;
        r=probe::encode(&b,1,out.bytes,sizeof(out.bytes));
        CHECK((r.error==probe::Error::none)==known);
        if(known)CHECK(r.length==5&&out.bytes[4]==b);else CHECK(!r.length&&zero(out.bytes,sizeof(out.bytes)));
    }
    std::printf("Native wildcard probe body: %u checks passed. No frame transmitted.\n",checks);
}
