// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/network/WirelessRsn.hpp"
#include <cassert>
#include <cstdio>
using rtl8852be::network::selection::wpa2PskRsn;
int main(){
    uint8_t ie[]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
    assert(wpa2PskRsn(ie,sizeof(ie)));
    for(size_t n=1;n<sizeof(ie);++n)assert(!wpa2PskRsn(ie,n));
    ie[20]=0x40;assert(!wpa2PskRsn(ie,sizeof(ie))); // Required PMF.
    ie[20]=0;ie[19]=8;assert(!wpa2PskRsn(ie,sizeof(ie))); // SAE must not become PSK.
    ie[19]=1;assert(!wpa2PskRsn(ie,sizeof(ie))); // 802.1X must not become PSK.
    ie[19]=2;ie[13]=2;assert(!wpa2PskRsn(ie,sizeof(ie))); // TKIP.
    ie[13]=4;ie[8]=2;assert(!wpa2PskRsn(ie,sizeof(ie))); // Truncated suite list.
    assert(wpa2PskRsn(nullptr,0));assert(!wpa2PskRsn(nullptr,22));
    puts("RSN adapter: truncation and unsupported authentication rejected");
}
