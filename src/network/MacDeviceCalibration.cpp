// SPDX-License-Identifier: BSD-3-Clause
#include "MacDeviceCalibration.hpp"
template class rtl8852be::network::DeviceCalibration<
    rtl8852be::network::EfuseReader<rtl8852be::network::MacEfuseIo>,
    rtl8852be::network::DavEfuseReader<rtl8852be::network::MacDavEfuseIo>>;
