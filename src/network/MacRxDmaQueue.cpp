// SPDX-License-Identifier: BSD-3-Clause
#include "MacDmaBuffer.hpp"
#include "RxDmaQueue.hpp"
namespace rtl8852be { namespace network {
template class RxDmaQueue<MacDmaBuffer>;
template bool RxDmaQueue<MacDmaBuffer>::allocate<IOPCIDevice,IOWorkLoop>(IOPCIDevice *,IOWorkLoop *);
} }
