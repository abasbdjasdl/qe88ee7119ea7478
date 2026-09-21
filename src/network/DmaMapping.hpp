// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace rtl8852be { namespace network {
// physical is the device-visible IOVM/bus address returned by IODMACommand,
// which may differ from the CPU's physical address when an IOMMU is present.
struct DataMapping {uint8_t *bytes{};uint64_t physical{};size_t capacity{};};
} }
