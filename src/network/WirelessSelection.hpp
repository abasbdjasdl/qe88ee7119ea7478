// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace rtl8852be { namespace network { namespace selection {
enum class Security : uint8_t { open, wpa2Psk };
struct Join {
    uint8_t ssid[32]{}; uint32_t ssidLength{};
    uint8_t bssid[6]{}; bool specificBssid{};
    Security security{Security::open};
    uint8_t pmk[32]{}; uint32_t pmkLength{};
};
inline void wipe(void *p,size_t n){auto *q=static_cast<volatile uint8_t*>(p);while(n--)*q++=0;}
inline bool valid(const Join &j){
    if(!j.ssidLength||j.ssidLength>sizeof(j.ssid))return false;
    if(j.specificBssid){unsigned any=0;for(auto b:j.bssid)any|=b;if(!any||(j.bssid[0]&1))return false;}
    if(j.security==Security::open)return j.pmkLength==0;
    return j.security==Security::wpa2Psk&&j.pmkLength==sizeof(j.pmk);
}
// The native interface and hardware work loop share this single pending request.
// An accepted request is NOT an association-success notification.
class Pending {
    Join join_{};bool waiting_{};uint64_t generation_{};
public:
    ~Pending(){clear();}
    Pending()=default;Pending(const Pending&)=delete;Pending&operator=(const Pending&)=delete;
    bool submit(const Join &j){
        if(waiting_||!valid(j))return false;
        join_=j;waiting_=true;if(!++generation_)++generation_;return true;
    }
    bool take(bool stationIdle,bool dataDrained,bool actionPending,Join &out){
        if(!waiting_||!stationIdle||!dataDrained||actionPending)return false;
        out=join_;clear();return true;
    }
    void clear(){wipe(&join_,sizeof(join_));waiting_=false;}
    bool waiting()const{return waiting_;}
    uint64_t generation()const{return generation_;}
};
} } }
