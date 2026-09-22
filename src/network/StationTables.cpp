// SPDX-License-Identifier: GPL-2.0-or-later
// Native instantiation uses the same shared CH12 command bus as role/join/BT.
#include "StationTables.hpp"
#include "MacFirmwareCommands.hpp"
template class rtl8852be::station::tables::Programmer<rtl8852be::network::MacCommandTransport>;
