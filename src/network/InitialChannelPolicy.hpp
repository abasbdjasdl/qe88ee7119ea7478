// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "TxPowerPlan.hpp"
namespace rtl8852be { namespace network {
// Deliberately narrow initial networking profile, not a country database.
// Linux v6.12 net/wireless/reg.c world_regdom permits initiation on channels
// 1..11. Use only those 20 MHz channels and the pinned rtw89 WORLD power table.
// Additional conducted ceiling is 0 dBm; it can only reduce the chip table.
// No 5 GHz/DFS, channel 12..14, AP mode, country-IE expansion, or wide channels.
// This policy is immutable for a live firmware epoch; a missing epoch denies TX.
class InitialChannelPolicy {
    uint64_t epoch_{};
    static bool query(void *owner,uint8_t band,uint8_t channel,int16_t &halfDbm){
        auto &self=*static_cast<InitialChannelPolicy *>(owner);
        if(!self.epoch_||band||channel<1||channel>11)return false;
        halfDbm=0;return true;
    }
public:
    bool initialize(uint64_t firmwareEpoch){if(epoch_||!firmwareEpoch)return false;epoch_=firmwareEpoch;return true;}
    bool allows(channel::Channel c)const{return epoch_&&c.band==0&&c.width==0&&c.center==c.primary&&c.primary>=1&&c.primary<=11;}
    power::Policy powerPolicy(){return {power::RTW89_WW,epoch_,this,query};}
    void invalidate(){epoch_=0;}
};
} }
