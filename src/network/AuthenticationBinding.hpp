// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "StationController.hpp"
namespace rtl8852be { namespace network { namespace authevents {
inline bool currentAssociation(station::State state,station::Token current,
                               station::Token recorded,bool selectionPending){
    if(selectionPending||!current.epoch||!current.operation||!station::same(current,recorded))return false;
    switch(state){
    case station::State::authenticating:
    case station::State::installingAssociationCmac:
    case station::State::joining:
    case station::State::installingAssociationCam:
    case station::State::associated:
    case station::State::authorized:return true;
    default:return false;
    }
}
} } }
