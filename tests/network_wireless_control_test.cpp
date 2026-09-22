// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/WirelessControl.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace rtl8852be::network;
int main(){
    control::Join in{};selection::Join out;
    in.version=1;in.ssidLength=32;in.ssid[31]=255;
    std::memset(in.pmk,0xaa,32);assert(control::decode(in,out));
    assert(out.ssidLength==32&&out.ssid[31]==255&&out.pmk[0]==0);
    in.security=1;in.pmkLength=32;assert(control::decode(in,out)&&out.pmk[31]==0xaa);
    in.security=2;in.pairwise=1;in.group=1;
    assert(control::decode(in,out)&&out.security==selection::Security::wpaPsk&&out.group==selection::Cipher::tkip);
    for(unsigned field=0;field<9;++field){
        auto bad=in;
        switch(field){case 0:bad.version=2;break;case 1:bad.security=3;break;
        case 2:bad.pairwise=2;break;case 3:bad.group=2;break;
        case 4:bad.ssidLength=33;break;case 5:bad.pmkLength=31;break;
        case 6:bad.specificBssid=2;break;case 7:bad.reserved=1;break;
        case 8:bad.reservedBytes[1]=1;break;}
        assert(!control::decode(bad,out));
        const auto *bytes=reinterpret_cast<const unsigned char*>(&out);
        for(unsigned i=0;i<sizeof(out);++i)assert(bytes[i]==0);
    }
    in.specificBssid=1;in.bssid[0]=1;assert(!control::decode(in,out));
    in.bssid[0]=2;assert(control::decode(in,out));
    wireless::Snapshot s;s.count=65;s.currentValid=false;s.selectionPending=true;
    s.current.ssid[0]=42;s.cached[0].ssidLength=32;s.cached[0].ssid[31]=255;
    s.cached[0].signalPercent=255;s.cached[0].privacy=true;s.cached[0].fiveGhz=true;
    control::Status status;std::memset(&status,0xaa,sizeof(status));control::encode(s,status);
    assert(status.version==1&&status.count==64&&status.flags==10);
    assert(status.current.ssid[0]==0&&status.cached[0].ssid[31]==255);
    assert(status.cached[0].signalPercent==100&&status.cached[0].flags==3);
    for(auto n:status.cached)assert(!n.reserved[0]&&!n.reserved[1]);
    selection::wipe(&in,sizeof(in));selection::wipe(&out,sizeof(out));
    puts("Wireless control ABI: sizes, invalid input, key clearing, bounded pointer-free output passed");
}
