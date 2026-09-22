// SPDX-License-Identifier: BSD-3-Clause
#include "FirmwareCapabilities.hpp"
#include "MacFirmwareMailboxIo.hpp"
template class rtl8852be::firmware::FirmwareCapabilities<
    rtl8852be::firmware::Mailbox<rtl8852be::firmware::MacMailboxIo>>;
