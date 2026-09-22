// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "MacNetworkController.hpp"
class R16RTL8852BE final : public R16NetworkController {
    OSDeclareDefaultStructors(R16RTL8852BE)
protected:
    rtl8852be::network::MacNetworkBootService *createBootService() override;
};
