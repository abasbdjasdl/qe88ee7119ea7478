// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "RfkInitialization.hpp"
namespace rtl8852be { namespace channel {
using rfk::u8;
struct Channel {u8 band{},width{},center{},primary{};};
inline bool validChannel(Channel c){
    if(!rfk::validChannel({c.band,c.width,c.center})||!rfk::validChannel({c.band,0,c.primary}))return false;
    const int distance=int(c.primary)-c.center;
    if(c.width==0)return distance==0;
    if(c.band==0&&c.primary==14)return false;
    return c.width==1?(distance==2||distance==-2):(distance==2||distance==-2||distance==6||distance==-6);
}
} }
