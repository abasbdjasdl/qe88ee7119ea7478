// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace rtl8852be {
// Always terminated, never allocates, and records clipping explicitly.
template <size_t N> struct ReportBuffer {
    static_assert(N > 0, "need a terminator");
    char bytes[N]{};
    size_t used{};
    bool clipped{};
    void append(const char *s) {
        if (!s) return;
        while (*s) {
            if (used + 1 >= N) { clipped = true; break; }
            bytes[used++] = *s++;
        }
        bytes[used] = 0;
    }
    void hex(const uint8_t *data, size_t count) {
        const char digits[] = "0123456789abcdef";
        for (size_t i = 0; i < count; ++i) {
            char b[] = {digits[data[i] >> 4], digits[data[i] & 15], 0};
            append(b);
        }
    }
};
}
