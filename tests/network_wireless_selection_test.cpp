#include "../src/network/WirelessSelection.hpp"
#include <assert.h>
#include <stdio.h>
using namespace rtl8852be::network::selection;
int main(){
    Pending p;Join a{},out{};a.ssid[0]='A';a.ssidLength=1;
    assert(p.submit(a));assert(!p.submit(a));
    assert(!p.take(false,true,false,out));assert(!p.take(true,false,false,out));
    assert(!p.take(true,true,true,out));assert(p.take(true,true,false,out));
    assert(out.ssid[0]=='A'&&!p.waiting());assert(!p.take(true,true,false,out));
    a.security=Security::wpa2Psk;a.pmkLength=31;assert(!p.submit(a));
    a.pmkLength=32;a.pmk[0]=123;assert(p.submit(a));p.clear();assert(!p.waiting());
    assert(!p.take(true,true,false,out));a.ssidLength=33;assert(!p.submit(a));
    a.ssidLength=32;a.ssid[1]=0;assert(p.submit(a));assert(p.take(true,true,false,out));
    assert(out.ssidLength==32&&out.pmk[0]==123);wipe(&out,sizeof(out));
    for(auto b:out.pmk)assert(!b);
    a.specificBssid=true;assert(!p.submit(a));a.bssid[0]=1;assert(!p.submit(a));
    a.bssid[0]=2;assert(p.submit(a));assert(p.generation()==4);p.clear();
    a.security=static_cast<Security>(255);assert(!p.submit(a));
    puts("PASS: validation, binary SSID, PMK size, BSSID, single pending join, drain/action gate, cancellation and wipe");
}
