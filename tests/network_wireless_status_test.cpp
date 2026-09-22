// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/WirelessStatus.hpp"
#include <cassert>
#include <cstdio>
using namespace rtl8852be::network::wireless;
int main(){
    assert(linkState(true,false,false,false,false,true,false,true)==Link::authenticating);
    assert(linkState(true,false,false,false,false,true,true,true)==Link::connected);
    assert(linkState(true,false,false,true,false,true,true,true)==Link::connecting);
    assert(linkState(false,false,false,false,false,true,true,true)==Link::off);
    assert(linkState(true,true,false,false,false,true,true,true)==Link::faulted);
    assert(linkState(true,false,false,false,true,false,false,false)==Link::scanning);
    Snapshot s;Network n;n.ssidLength=32;n.ssid[0]=0;n.ssid[31]=255;
    for(unsigned i=0;i<64;++i){n.bssid[5]=i;assert(append(s,n));}
    assert(s.count==64&&!s.cacheTruncated&&s.cached[63].ssid[31]==255);
    assert(!append(s,n)&&s.cacheTruncated&&s.count==64);
    Snapshot bad;n.ssidLength=33;assert(!append(bad,n)&&bad.count==0);
    n.ssidLength=0;assert(append(bad,n)); // Hidden SSID remains a real observation.
    puts("Wireless status: authorization, pending switch, binary SSID and bounded cache passed");
}
